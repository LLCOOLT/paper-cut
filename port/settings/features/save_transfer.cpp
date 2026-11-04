// The Saves tab's "Send saves to another device" and "Receive saves": a save
// moved between two devices on the same network, with no sync folder and no
// cable (docs/SAVE_TRANSFER.md).
//
// Sending: a one-time web server on this device's network address and a
// port the system picks, behind a random 128-bit token in the address
// (http://<address>:<port>/<token>). It serves this device's save files of
// this game (every release's card, read once when it starts):
//   /<token>            a page with each file as a link (a phone's browser)
//   /<token>/list       "<size> <name>" lines (another Paper Cut)
//   /<token>/f/<name>   a file
// It stops once every file was taken, after 5 minutes, or with Stop. The
// address is shown as text and as a QR code (libs/qrcodegen).
//
// Receiving: the address typed or pasted; the files fetched, then which of
// each card file's four game files to replace is asked; the save here is
// backed up first (port/settings/features/saves.cpp), and a file of the
// running game's card is written once the game isn't writing it, the game
// then starting again to read it (port/include/port/restart.h).
//
// Tests: $TTYD_SAVE_SEND=1 sends at start and prints the address
// ($TTYD_SAVE_SEND_BIND: an address to listen on instead of every one, e.g.
// 127.0.0.1; $TTYD_SAVE_SEND_QR: the QR code as a .bmp there);
// $TTYD_SAVE_RECEIVE=<address> receives at start, replacing the game files
// in $TTYD_SAVE_RECEIVE_FILES ("1234" by default).
#ifdef _WIN32
#define NOMINMAX
#endif
#include "port/online_sock.h"
#include "port/lan_http.h"
#include "port/online.h"
#include "port/restart.h"
#include "port/save_convert.h"
#include "port/save_transfer.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "../../../libs/qrcodegen/qrcodegen.h"
}

namespace {

constexpr int kSeconds = 5 * 60;     // a sender's life
constexpr size_t kMaxFile = 1 << 20; // a .gci is 0x22040 bytes
constexpr char kReleases[] = {'J', 'E', 'P'};
constexpr int kGciHeader = 0x40, kMeta = 0x2000, kSlot = 0x4000; // port/src/common/save_convert_pc.c

const char* releaseName(char release) {
  return release == 'J' ? "Japan (JP)" : release == 'P' ? "Europe (EU)" : "North America (US)";
}

struct File {
  std::string name;
  std::vector<uint8_t> bytes;
};

std::vector<uint8_t> readAll(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::vector<uint8_t> bytes;
  if (data != nullptr) {
    bytes.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    SDL_free(data);
  }
  return bytes;
}

// The release whose save file this is by name and contents ("01-G8ME-mariost_save_file.gci"), 0 if none
char saveOf(const std::string& name, const std::vector<uint8_t>& bytes) {
  const char release = portSaveRelease(bytes.data(), bytes.size());
  char expected[64];
  portSaveFileName(release, expected, sizeof(expected));
  return release != 0 && name == expected ? release : 0;
}

// Game file k (0-3) of a card file: its slot and its backup slot (cardmgr.c)
bool hasGame(const std::vector<uint8_t>& gci, int k) {
  for (const int slot : {k, k + 4}) {
    const size_t at = kGciHeader + kMeta + size_t(slot) * kSlot;
    if (gci.size() < at + kSlot) {
      return false;
    }
    if (std::any_of(gci.begin() + long(at), gci.begin() + long(at + kSlot), [](uint8_t b) { return b != 0; })) {
      return true;
    }
  }
  return false;
}

bool sendAll(Socket s, const char* data, size_t size) {
  while (size > 0) {
    const int n = int(send(s, data, int(std::min<size_t>(size, 1 << 16)), 0));
    if (n <= 0) {
      return false;
    }
    data += n;
    size -= size_t(n);
  }
  return true;
}

// --- sending ---

struct Sender {
  std::mutex lock;
  std::atomic<bool> running{false}, stop{false};
  std::string url, status; // (lock)
  std::vector<File> files; // (the thread's while it runs)
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
  int qrSize = 0;
  Uint64 endsAt = 0;
  std::atomic<int> printed{0};
} sSend;

void setSendStatus(const std::string& text) {
  std::lock_guard<std::mutex> hold(sSend.lock);
  sSend.status = text;
}

std::string htmlPage(const std::string& token) {
  std::string page = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                     "content=\"width=device-width,initial-scale=1\"><title>Paper Cut saves</title><style>body{"
                     "font-family:system-ui,sans-serif;margin:24px;line-height:1.5}a{display:block;padding:12px;"
                     "margin:8px 0;border:1px solid #888;border-radius:8px;text-decoration:none}</style></head><body>"
                     "<h1>Paper Cut saves</h1><p>Download a file, then put it in Paper Cut's card folder (for a "
                     "phone's Paper Cut: type this page's address in its Saves tab, Receive saves, instead).</p>";
  for (const File& f : sSend.files) {
    page += "<a download href=\"/" + token + "/f/" + f.name + "\">" + f.name + " (" + releaseName(f.name[6]) + ")</a>";
  }
  return page + "</body></html>";
}

void respond(Socket s, int status, const char* type, const std::string& body, const char* extra = "") {
  char head[512];
  std::snprintf(head, sizeof(head),
                "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n%s"
                "Connection: close\r\n\r\n",
                status, status == 200 ? "OK" : "Not Found", type, body.size(), extra);
  if (sendAll(s, head, std::strlen(head))) {
    sendAll(s, body.data(), body.size());
  }
}

// One request: true when it took a file (served[i] set)
void serveOne(Socket c, const std::string& token, std::vector<bool>& served) {
  std::string request;
  char buf[2048];
  while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
    const int n = int(recv(c, buf, sizeof(buf), 0));
    if (n <= 0) {
      return;
    }
    request.append(buf, size_t(n));
  }
  if (request.rfind("GET ", 0) != 0) {
    respond(c, 404, "text/plain", "");
    return;
  }
  std::string path = request.substr(4, request.find(' ', 4) - 4);
  path = path.substr(0, path.find('?'));
  const std::string base = "/" + token;
  if (path == base || path == base + "/") {
    respond(c, 200, "text/html; charset=utf-8", htmlPage(token));
  } else if (path == base + "/list") {
    std::string list;
    for (const File& f : sSend.files) {
      list += std::to_string(f.bytes.size()) + " " + f.name + "\n";
    }
    respond(c, 200, "text/plain; charset=utf-8", list);
  } else if (path.rfind(base + "/f/", 0) == 0) {
    const std::string name = path.substr(base.size() + 3);
    for (size_t i = 0; i < sSend.files.size(); i++) {
      if (sSend.files[i].name == name) {
        const std::string disposition = "Content-Disposition: attachment; filename=\"" + name + "\"\r\n";
        respond(c, 200, "application/octet-stream",
                std::string(sSend.files[i].bytes.begin(), sSend.files[i].bytes.end()), disposition.c_str());
        served[i] = true;
        return;
      }
    }
    respond(c, 404, "text/plain", "");
  } else {
    respond(c, 404, "text/plain", ""); // (a wrong token: nothing about the files)
  }
}

void serve(Socket listener, std::string token) {
  std::vector<bool> served(sSend.files.size(), false);
  bool done = false;
  while (!sSend.stop && SDL_GetTicks() < sSend.endsAt && !done) {
    if (!onlineReadable(listener, 250)) {
      continue;
    }
    Socket c = accept(listener, nullptr, nullptr);
    if (c == BAD_SOCKET) {
      continue;
    }
    onlineNonBlocking(c, 0);
#ifdef _WIN32
    DWORD ms = 5000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{5, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    serveOne(c, token, served);
#ifdef _WIN32
    shutdown(c, SD_SEND);
#else
    shutdown(c, SHUT_WR);
#endif
    closeSocket(c);
    done = std::all_of(served.begin(), served.end(), [](bool b) { return b; });
  }
  closeSocket(listener);
  setSendStatus(done ? "Sent: the other device has the saves."
                : sSend.stop ? "Stopped."
                             : "No other device took the saves in 5 minutes: stopped.");
  std::printf("[save_transfer] sender: %s\n", done ? "sent" : sSend.stop ? "stopped" : "timed out");
  std::fflush(stdout);
  sSend.running = false;
}

void startSending() {
  if (sSend.running) {
    return;
  }
  sSend.files.clear();
  for (const char release : kReleases) {
    const std::string dir = portSavesCardDir(release);
    int count = 0;
    char** found = SDL_GlobDirectory(dir.c_str(), "*.gci", 0, &count);
    for (int i = 0; found != nullptr && i < count; i++) {
      File f{found[i], readAll(dir + "/" + found[i])};
      if (saveOf(f.name, f.bytes) != 0) {
        sSend.files.push_back(std::move(f));
      }
    }
    SDL_free(found);
  }
  if (sSend.files.empty()) {
    setSendStatus("This device has no saves of the game yet.");
    return;
  }
  // the token: 128 random bits, 26 letters and digits (base32)
  uint8_t secret[16];
  if (!port_lan::randomBytes(secret, sizeof(secret))) {
    setSendStatus("This device has no random numbers for the address.");
    return;
  }
  std::string token;
  for (int bit = 0; bit < 128; bit += 5) {
    int v = 0;
    for (int i = 0; i < 5; i++) {
      const int b = bit + i;
      v = v << 1 | (b < 128 ? (secret[b / 8] >> (7 - b % 8)) & 1 : 0);
    }
    token += "abcdefghijklmnopqrstuvwxyz234567"[v];
  }
  char address[64] = "";
  const char* bindTo = SDL_getenv("TTYD_SAVE_SEND_BIND");
  if (bindTo != nullptr) {
    std::snprintf(address, sizeof(address), "%s", bindTo);
  } else if (!portOnlineLocalAddress(address, sizeof(address))) {
    setSendStatus("This device isn't on a network.");
    return;
  }
  Socket listener = onlineNetStart() ? socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : BAD_SOCKET;
  sockaddr_in at{};
  at.sin_family = AF_INET;
  at.sin_port = 0; // the system's choice, a different one each time
  at.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bindTo != nullptr) {
    inet_pton(AF_INET, bindTo, &at.sin_addr);
  }
  socklen_t len = sizeof(at);
  if (listener == BAD_SOCKET || bind(listener, reinterpret_cast<sockaddr*>(&at), sizeof(at)) != 0 ||
      listen(listener, 4) != 0 || getsockname(listener, reinterpret_cast<sockaddr*>(&at), &len) != 0) {
    if (listener != BAD_SOCKET) {
      closeSocket(listener);
    }
    setSendStatus("Could not start sending (no network?).");
    return;
  }
  const std::string url = "http://" + std::string(address) + ":" + std::to_string(ntohs(at.sin_port)) + "/" + token;
  uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
  sSend.qrSize = qrcodegen_encodeText(url.c_str(), temp, sSend.qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                      qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)
                     ? qrcodegen_getSize(sSend.qr)
                     : 0;
  {
    std::lock_guard<std::mutex> hold(sSend.lock);
    sSend.url = url;
    sSend.status.clear();
  }
  sSend.stop = false;
  sSend.endsAt = SDL_GetTicks() + Uint64(kSeconds) * 1000;
  sSend.running = true;
  sSend.printed = 0;
  std::thread(serve, listener, token).detach();
}

// The QR code as a picture (tests): 8 pixels a module, the 4-module margin
bool saveQr(const char* path) {
  const int n = sSend.qrSize, scale = 8, side = (n + 8) * scale;
  SDL_Surface* s = n > 0 ? SDL_CreateSurface(side, side, SDL_PIXELFORMAT_RGB24) : nullptr;
  if (s == nullptr) {
    return false;
  }
  for (int y = 0; y < side; y++) {
    auto* row = static_cast<uint8_t*>(s->pixels) + size_t(y) * size_t(s->pitch);
    for (int x = 0; x < side; x++) {
      const int mx = x / scale - 4, my = y / scale - 4;
      const bool dark = mx >= 0 && my >= 0 && mx < n && my < n && qrcodegen_getModule(sSend.qr, mx, my);
      std::memset(row + x * 3, dark ? 0 : 255, 3);
    }
  }
  const bool ok = SDL_SaveBMP(s, path);
  SDL_DestroySurface(s);
  return ok;
}

void drawQr(float side) {
  const int n = sSend.qrSize;
  if (n <= 0) {
    return;
  }
  const float module = std::floor(side / float(n + 8));
  const float full = module * float(n + 8);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(at, ImVec2(at.x + full, at.y + full), IM_COL32(255, 255, 255, 255));
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      if (qrcodegen_getModule(sSend.qr, x, y)) {
        const ImVec2 a(at.x + (float(x) + 4) * module, at.y + (float(y) + 4) * module);
        draw->AddRectFilled(a, ImVec2(a.x + module, a.y + module), IM_COL32(0, 0, 0, 255));
      }
    }
  }
  ImGui::Dummy(ImVec2(full, full));
}

void sendSection(int) {
  if (!sSend.running) {
    ImGui::TextWrapped("%s", portTr("Shows an address and its QR code for another device on the same network: "
                                    "its Paper Cut takes the saves with Receive saves. For 5 minutes, once."));
    if (ImGui::Button(portTrLabel("Send saves to another device"))) {
      startSending();
    }
  } else {
    std::string url;
    {
      std::lock_guard<std::mutex> hold(sSend.lock);
      url = sSend.url;
    }
    ImGui::TextWrapped("%s", portTr("On the other device: Settings, Saves, Receive saves, and type this address."));
    drawQr(std::min(ImGui::GetContentRegionAvail().x, 260.f * ImGui::GetFontSize() / 16.f));
    ImGui::TextUnformatted(url.c_str());
    if (ImGui::SmallButton(portTrLabel("Copy the address"))) {
      SDL_SetClipboardText(url.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Stop##send"))) {
      sSend.stop = true;
    }
    const Uint64 now = SDL_GetTicks();
    const int left = now < sSend.endsAt ? int((sSend.endsAt - now) / 1000) : 0;
    ImGui::TextDisabled(portTr("Stops in %d:%02d, or once the other device has the saves."), left / 60, left % 60);
  }
  std::lock_guard<std::mutex> hold(sSend.lock);
  if (!sSend.status.empty() && !sSend.running) {
    ImGui::TextUnformatted(portTr(sSend.status.c_str()));
  }
}

// --- receiving ---

struct Incoming {
  File file;
  char release = 0;
  std::vector<uint8_t> here; // this device's file of that name, if any
  bool replace[4] = {};
};

struct Receiver {
  std::atomic<int> state{0}; // 0 waiting for an address, 1 fetching, 2 choosing, 3 a restart writes it
  std::string error, done;   // (set before state changes)
  std::vector<Incoming> files;
  char url[300] = "";
} sReceive;

// The address as typed: http:// added, a trailing "/" or page name dropped
std::string baseUrl(std::string url) {
  while (!url.empty() && (url.back() == ' ' || url.back() == '\n' || url.back() == '\r' || url.back() == '/')) {
    url.pop_back();
  }
  const size_t start = url.find_first_not_of(" \t\r\n");
  url = start == std::string::npos ? std::string() : url.substr(start);
  if (url.rfind("http://", 0) != 0 && url.find("://") == std::string::npos) {
    url = "http://" + url;
  }
  if (url.size() > 5 && url.compare(url.size() - 5, 5, "/list") == 0) {
    url.resize(url.size() - 5);
  }
  return url;
}

void fetchAll(std::string base) {
  std::vector<uint8_t> list;
  std::string why;
  std::vector<Incoming> files;
  if (base.rfind("https://", 0) == 0) {
    why = "use the http:// address the other device shows";
  } else if (port_lan::fetch(base + "/list", list, 65536, why)) {
    const std::string text(list.begin(), list.end());
    size_t at = 0;
    while (at < text.size()) {
      const size_t eol = text.find('\n', at);
      const std::string line = text.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
      at = eol == std::string::npos ? text.size() : eol + 1;
      const size_t space = line.find(' ');
      const std::string name = space == std::string::npos ? std::string() : line.substr(space + 1);
      if (name.empty() || name.find_first_of("/\\%?#") != std::string::npos || name.size() > 64) {
        continue;
      }
      Incoming in;
      if (!port_lan::fetch(base + "/f/" + name, in.file.bytes, kMaxFile, why)) {
        files.clear();
        break;
      }
      in.file.name = name;
      in.release = saveOf(name, in.file.bytes);
      if (in.release == 0) {
        continue; // not a save of this game
      }
      in.here = readAll(portSavesCardDir(in.release) + "/" + name);
      for (int k = 0; k < 4; k++) {
        in.replace[k] = hasGame(in.file.bytes, k);
      }
      files.push_back(std::move(in));
    }
    if (files.empty() && why.empty()) {
      why = "the other device sent no saves of this game";
    }
  }
  sReceive.files = std::move(files);
  sReceive.error = sReceive.files.empty() ? why : std::string();
  sReceive.state = sReceive.files.empty() ? 0 : 2;
  std::printf("[save_transfer] receiver: %s\n",
              sReceive.files.empty() ? ("failed: " + why).c_str()
                                     : (std::to_string(sReceive.files.size()) + " file(s)").c_str());
  std::fflush(stdout);
}

void startReceiving(const std::string& url) {
  if (sReceive.state == 1 || sReceive.state == 3) {
    return;
  }
  sReceive.error.clear();
  sReceive.done.clear();
  sReceive.state = 1;
  std::thread(fetchAll, baseUrl(url)).detach();
}

// The card file to write: the whole received one, or this device's with the chosen game files replaced
std::vector<uint8_t> merged(const Incoming& in) {
  const bool all = std::all_of(in.replace, in.replace + 4, [](bool b) { return b; });
  if (all || in.here.size() != in.file.bytes.size() || portSaveRelease(in.here.data(), in.here.size()) != in.release) {
    return in.file.bytes;
  }
  std::vector<uint8_t> out = in.here;
  for (int k = 0; k < 4; k++) {
    if (in.replace[k]) {
      for (const int slot : {k, k + 4}) {
        const size_t at = kGciHeader + kMeta + size_t(slot) * kSlot;
        std::memcpy(out.data() + at, in.file.bytes.data() + at, kSlot);
      }
    }
  }
  return out;
}

bool writeChosen() {
  int written = 0;
  std::string why;
  for (const Incoming& in : sReceive.files) {
    if (!std::any_of(in.replace, in.replace + 4, [](bool b) { return b; })) {
      continue;
    }
    if (!portSavesWriteFile(in.release, in.file.name, merged(in), why)) {
      sReceive.error = "Could not write it: " + why;
      sReceive.state = 2;
      std::printf("[save_transfer] receiver: write failed: %s\n", why.c_str());
      return false;
    }
    written++;
  }
  sReceive.done = "Received: the saves as they were are kept as backups.";
  sReceive.files.clear();
  sReceive.state = 0;
  std::printf("[save_transfer] receiver: wrote %d file(s)\n", written);
  std::fflush(stdout);
  return true;
}

// The chosen files written: here, or after the running game's card is idle and the game then starts again
void applyChosen(int inGame) {
  bool running = false;
  for (const Incoming& in : sReceive.files) {
    running |= inGame && in.release == PORT_GAME_ID[3] && std::any_of(in.replace, in.replace + 4, [](bool b) {
                 return b;
               });
  }
  if (!running) {
    writeChosen();
    return;
  }
  sReceive.state = 3;
  SDL_Environment* env = SDL_GetEnvironment(); // (a test's receive was for this run)
  SDL_UnsetEnvironmentVariable(env, "TTYD_SAVE_RECEIVE");
  portRestartRequestWith(0, [] { return writeChosen() ? 1 : 0; });
}

void receiveSection(int inGame) {
  const int state = sReceive.state;
  if (state == 0) {
    ImGui::TextWrapped("%s", portTr("The address the other device shows under Send saves to another device."));
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 26.f * ImGui::GetFontSize()));
    ImGui::InputTextWithHint("##address", "http://192.168.1.20:50123/...", sReceive.url, sizeof(sReceive.url));
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Paste")) && SDL_HasClipboardText()) {
      char* text = SDL_GetClipboardText();
      std::snprintf(sReceive.url, sizeof(sReceive.url), "%s", text != nullptr ? text : "");
      SDL_free(text);
    }
    ImGui::BeginDisabled(sReceive.url[0] == '\0');
    if (ImGui::Button(portTrLabel("Receive saves"))) {
      startReceiving(sReceive.url);
    }
    ImGui::EndDisabled();
  } else if (state == 1) {
    ImGui::TextDisabled("%s", portTr("Receiving..."));
  } else if (state == 2) {
    ImGui::TextWrapped("%s", portTr("Which game files to replace? The save here is kept as a backup first."));
    bool any = false, running = false;
    for (size_t i = 0; i < sReceive.files.size(); i++) {
      Incoming& in = sReceive.files[i];
      ImGui::PushID(int(i));
      ImGui::TextUnformatted(portTr(releaseName(in.release)));
      for (int k = 0; k < 4; k++) {
        const char* there = portTr(hasGame(in.file.bytes, k) ? "a save" : "empty");
        const char* here = in.here.empty() ? portTr("empty") : portTr(hasGame(in.here, k) ? "a save" : "empty");
        char label[160];
        std::snprintf(label, sizeof(label), portTr("File %d: the other device's (%s) over this one's (%s)"), k + 1,
                      there, here);
        ImGui::PushID(k);
        ImGui::Checkbox(label, &in.replace[k]);
        ImGui::PopID();
        any |= in.replace[k];
        running |= in.replace[k] && inGame && in.release == PORT_GAME_ID[3];
      }
      ImGui::PopID();
    }
    const bool locked = running && !portRestartSupported();
    ImGui::BeginDisabled(!any || locked);
    if (ImGui::Button(portTrLabel(running ? "Replace them and restart" : "Replace them"))) {
      applyChosen(inGame);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Cancel##receive"))) {
      sReceive.files.clear();
      sReceive.state = 0;
    }
    if (locked) {
      ImGui::TextDisabled("%s", portTr("Receive this card's saves from the launcher, before the game starts."));
    } else if (running) {
      ImGui::TextDisabled("%s", portTr("This is the running game's card: the game starts again to load it."));
    }
  } else {
    ImGui::TextDisabled("%s", portTr("Restarting when the game has finished saving..."));
  }
  if (state != 1 && !sReceive.error.empty()) {
    ImGui::TextWrapped("%s %s", portTr("Could not receive the saves:"), sReceive.error.c_str());
  } else if (state != 1 && !sReceive.done.empty()) {
    ImGui::TextWrapped("%s", portTr(sReceive.done.c_str()));
  }
}

// Tests ($TTYD_SAVE_SEND, $TTYD_SAVE_RECEIVE): from the first frame the user folder is known
void testOverlay(int) {
  static bool sStarted;
  if (portSettingsUserPath()[0] == '\0') {
    return;
  }
  if (!sStarted) {
    sStarted = true;
    if (SDL_getenv("TTYD_SAVE_SEND") != nullptr) {
      startSending();
      std::lock_guard<std::mutex> hold(sSend.lock);
      std::printf("[save_transfer] sending at %s (%zu file(s)) %s\n", sSend.url.c_str(), sSend.files.size(),
                  sSend.status.c_str());
      if (const char* qr = SDL_getenv("TTYD_SAVE_SEND_QR")) {
        std::printf("[save_transfer] QR code (%dx%d modules) %s %s\n", sSend.qrSize, sSend.qrSize,
                    saveQr(qr) ? "written to" : "NOT written to", qr);
      }
      std::fflush(stdout);
    }
    if (const char* url = SDL_getenv("TTYD_SAVE_RECEIVE")) {
      startReceiving(url);
    }
  }
  static bool sApplied;
  if (!sApplied && sReceive.state == 2 && SDL_getenv("TTYD_SAVE_RECEIVE") != nullptr) {
    sApplied = true;
    const char* which = SDL_getenv("TTYD_SAVE_RECEIVE_FILES");
    for (Incoming& in : sReceive.files) {
      for (int k = 0; k < 4; k++) {
        in.replace[k] = std::strchr(which != nullptr ? which : "1234", '1' + k) != nullptr;
      }
    }
    applyChosen(1);
  }
}

struct SaveTransferSettings {
  SaveTransferSettings() {
    portSettingsAddSection("Saves", "Send saves to another device", nullptr, 0, sendSection,
                           "transfer move phone qr code network wifi share send gci memory card");
    portSettingsAddSection("Saves", "Receive saves", nullptr, 0, receiveSection,
                           "transfer move phone qr code network wifi receive import gci memory card");
    if (SDL_getenv("TTYD_SAVE_SEND") != nullptr || SDL_getenv("TTYD_SAVE_RECEIVE") != nullptr) {
      portSettingsAddOverlay(testOverlay, nullptr);
    }
  }
} sSaveTransfer;

} // namespace
