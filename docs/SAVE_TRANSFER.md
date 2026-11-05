# Moving saves between devices

The Saves tab of the settings page moves this game's save files from one device to another on the same network (Wi-Fi or cable), with no sync folder and no cable between them. Code: `port/settings/features/save_transfer.cpp`.

## Sending

**Send saves to another device** starts a one-time web server on this device:

- on its network address (`portOnlineLocalAddress`) and a port the system picks, different each time;
- behind a random 128-bit token in the address, 26 letters and digits: `http://192.168.1.20:50123/k3j...`. Without the token the server answers nothing about the saves;
- serving this device's save files of the game (every release's card, `<user>/<JAP|USA|EUR>/Card A/01-G8Mx-mariost_save_file.gci`), read once when it starts:
  - `/<token>`: a page with each file as a download link, for a phone's or computer's browser;
  - `/<token>/list`: `<size> <name>` lines, for another Paper Cut;
  - `/<token>/f/<name>`: a file.
  - `/<token>/d/<name>`: for a card holding Paper Cut's own badges or dishes, a copy for Dolphin or a GameCube with them sold for coins (docs/SAVE_FILES.md); the page shows it as a second link.
- It stops once every file was taken, after 5 minutes, or with Stop.

The address is shown as text (with Copy) and as a QR code ([Nayuki's QR Code generator](https://github.com/nayuki/QR-Code-generator), `libs/qrcodegen/`, v1.8.0).

## Receiving

**Receive saves** takes the address, typed or pasted (Paste reads the clipboard). It fetches the list and the files and keeps only real saves of this game (the name must match the save's own release). Then it asks which of each card file's four game files (File 1 to 4, each a slot and its backup slot) to replace, showing which are saves and which are empty on each side. Before anything is written, the save already here is copied to the dated backups (`<user>/Backups/<region>/`, the Restore a backup section). Choosing all four writes the received file as it is, byte for byte; choosing fewer replaces only those slots in the file already here. A file of the running game's card is written once the game isn't writing the card, then the game starts again to read it, as Restore does. On a phone, which can't restart itself, that card is received from the launcher.

## Which way works where

Paper Cut needs the address in its own Receive saves, so the base is typing or pasting it:

- **Phone to computer** (the usual case): Send on the phone, then type or paste the phone's address on the computer. A computer has no camera to scan with, and needs none.
- **Computer to phone:** Send on the computer, then type the address in the phone's Receive saves. Scanning the QR code with the phone's own camera app opens the server's page in the phone's browser, which downloads the `.gci` to the phone's Files app, not into Paper Cut. On iOS that is still useful: Paper Cut's card folder is in the Files app (On My iPhone, Paper Cut, Saves), so the file can be moved there. In-app camera scanning isn't built.
- **Computer to computer** and **phone to phone** work the same way, either side sending.

Phones may ask once whether Paper Cut may use the local network.

## Tests

`TTYD_SAVE_SEND=1` sends at start and prints the address (`TTYD_SAVE_SEND_BIND=127.0.0.1` listens on that address alone; `TTYD_SAVE_SEND_QR=<path>.bmp` writes the QR code); `TTYD_SAVE_RECEIVE=<address>` receives at start, replacing the game files in `TTYD_SAVE_RECEIVE_FILES` (`1234` by default).
