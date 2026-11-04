// HTTPS GETs for the settings page's features (the update check, the mod
// browser), with what each platform has, so no library comes along: WinHTTP
// on Windows, curl on macOS and Linux, NSURLSession on iOS
// (port/apple/http_apple.m), HttpURLConnection on Android
// (TtydActivity.httpGet). Only https:// addresses, and redirects only to
// https://. Each call blocks until the transfer ends: make it from a thread
// of its own, never the UI's. port/settings/http.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace port_http {

// A transfer's progress, read from another thread while it runs: the bytes
// so far, and the whole (0 while unknown; the caller may set it from what
// it expects, the server's length replaces it). cancel, set from another
// thread, stops it at the next chunk.
struct Progress {
  std::atomic<uint64_t> done{0}, total{0};
  std::atomic<bool> cancel{false};
};

// Takes each chunk of the body as it arrives; false stops the transfer (a
// write that failed).
using Sink = std::function<bool(const uint8_t* data, size_t size)>;

// An address these calls take: https://, a host, printable ASCII without
// spaces or quotes (curl gets it on a command line).
bool validUrl(const std::string& url);

// The body of url, chunk by chunk, into sink: at most maxBytes (more is an
// error). accept: an Accept header, or nullptr. false and why (in plain
// words) on any failure, a status other than 200 included.
bool get(const std::string& url, const Sink& sink, uint64_t maxBytes, Progress* progress, std::string& error,
         const char* accept = nullptr);

// The whole body of url into out (a small document: a release's details).
bool fetch(const std::string& url, std::vector<uint8_t>& out, uint64_t maxBytes, std::string& error,
           const char* accept = nullptr);

} // namespace port_http
