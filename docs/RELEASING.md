CI and releases
===============

GitHub runs the port's checks and builds on two **self-hosted runners**: a
Windows PC and a Mac, both with the disc images the tests need. Self-hosted
jobs use no GitHub Actions minutes. The GitHub-hosted workflow
(`pc-port.yml`) stays manual-only.

| Workflow | When | What |
|---|---|---|
| [`ci.yml`](../.github/workflows/ci.yml) | a push to `main`, `modding` or a `v*` branch, or by hand | `tools/port/local_ci.py` on Windows (every stage on `main` and by hand, `--quick` otherwise), and its headless stages on Linux in a container on the Mac; the logs as artifacts |
| [`release.yml`](../.github/workflows/release.yml) | a tag `v<version>`, or by hand (the Mac alone) | every local CI stage, then `tools/port/release.py` on the Windows runner and then the Mac runner: release builds and a **draft** GitHub release |

Steps that download (configuring and building, which fetch dependencies; Gradle; the container's packages; GitHub's API) are retried after 30 seconds and 2 minutes, so a passing outage of a server doesn't fail a run.

**Pull requests don't run on the runners.** The repository is public, and a pull request's workflows run the pull request's own files, so one from a fork could run anything on these machines as you. None of the workflows on `main` has a `pull_request` trigger. A fork could add a workflow of its own, so the repository's Actions settings must require approval for fork pull requests from all outside contributors (Settings > Actions > General). Approve one only after reading every file it changes.

## The Windows runner

- **Where it lives:** a runner folder of its own on the Windows machine, registered to `LLCOOLT/paper-cut` with the label `ttyd-port` (the repository's old name; a rename keeps runners). The workflows ask for `[self-hosted, ttyd-port]`.
- **Other runners:** a runner for another repository (such as the decomp's, `LLCOOLT/ttyd`) needs a folder of its own.
- **Starting it:** run `run.cmd` in that folder. Jobs run only while it's running, as your user, with your tools on the path: Python, LLVM, Git, `gh`, the Android SDK. To keep it running without a terminal, install it as a service (`config.cmd`'s service option, as administrator). The service then needs the same tools on its path.
- **Disc data:** `.env` in the runner's folder sets `TTYD_LOCAL_DATA` to a checkout that has the disc images and extracted data (`orig/`, `data/`). `tools/port/ci_link_data.py` links them into the runner's checkout as directory junctions. They stay on this machine: git ignores them, and nothing uploads them.
- **Build folders:** the runner's checkout keeps its build folders between runs (`clean: false`). The first run builds everything from scratch, the later ones only what changed.

## Cutting a release

1. Write `docs/release_notes/<version>.md`: what the release is and what's in it. `release.py` adds the checksums and the "no game data" line.
2. Tag the commit and push the tag:
   ```bash
   git tag v0.1.0
   ```
   ```bash
   git push origin v0.1.0
   ```
3. The release workflow runs local CI on the Windows runner, then builds and packages, then creates a draft release with:
   - `papercut-<version>-windows-x64.zip`: `Paper Cut.exe` (the launcher) and the three versions' builds (`ttyd-jp`, `ttyd-us`, `ttyd-eu`), their libraries, the C runtime, README, LICENSE, `docs/MODDING.md`, the example mods (with `hello-native`'s library), and `HOW TO PLAY.txt`;
   - `papercut-<version>-<jp|us|eu>-android-arm64.apk`: debug-signed, one per version;
   - `SHA256SUMS.txt`.

   Then the Mac runner adds to the same draft:
   - `papercut-<version>-macos-<arch>.zip`: `Paper Cut.app`, holding the three versions' builds (signed ad hoc, not notarized), with the same extras;
   - `papercut-<version>-<jp|us|eu>-ios-unsigned.ipa`: one app per version, for AltStore, SideStore or Sideloadly;
   - `papercut-<version>-linux-x64.zip`: the three builds and the `papercut` script, built in an Ubuntu container ([`tools/port/linux-release.Dockerfile`](../tools/port/linux-release.Dockerfile));
   - `SHA256SUMS-macos.txt`, and those checksums in the notes.
4. Check the draft on GitHub (Releases), then publish it.

**If the Windows job fails after local CI passed** (packaging, a download), re-running it repeats the CI. Instead, on the Windows PC at the tagged commit, `python tools/port/release.py <version> --publish v<version>` makes the Windows and Android packages and the draft; then re-run only the run's Mac job (`gh run rerun <run id> --job <the apple job's id>`), which adds its packages to that draft.

To build the packages without publishing, run `python tools/port/release.py 0.1.0` (they go to `dist/`). `--targets` picks what to build (`desktop`, `android`, `ios`, `linux`; by default Windows builds `desktop,android` and a Mac `desktop,ios,linux`). Running the Release workflow by hand builds the Mac's packages alone, as an artifact, to try the Mac runner.

## The Mac runner

- **Registered** to `LLCOOLT/paper-cut` with the label `ttyd-port-mac` (only that: the Windows jobs ask for `ttyd-port`). Run it as a service (`./svc.sh install`, `./svc.sh start`) so it starts on its own; it must be awake and online while a release runs.
- **The runner's build** must match the Mac: the ARM64 runner on Apple silicon (an x64 runner there runs under Rosetta and builds for Intel).
- **Tools:** Xcode, Homebrew's `cmake ninja llvm python`, Rust with `rustup target add aarch64-apple-ios`, and Docker or OrbStack for the Linux packages. The workflow's first step prints each one's version.
- **Disc data**, for the Linux CI job's tests that play the game: `.env` sets `TTYD_LOCAL_DATA` to a folder outside `~/Documents` (which a service can't read), holding `orig/<version>/` (the disc images) and `data/<version>/` (each disc extracted with `tools/port/extract_disc.py`). Release builds hold none of it.
- **The Linux CI job** runs local CI's headless stages in the Ubuntu container (`tools/port/linux_container.py`), with that folder mounted at its own path.

## Code signing

Releases are unsigned on Windows and signed ad hoc on the Mac until the repository has signing secrets. `release.py` signs whenever its environment has them, and otherwise prints a line saying it didn't and goes on as before. The release workflow passes these repository secrets (Settings > Secrets and variables > Actions); an unset one is empty.

**Windows (Authenticode):** `release.py` signs `Paper Cut.exe`, the three builds, the example mod's library and any library not already signed (Microsoft's keep their own signature), with a SHA-256 signature and an RFC 3161 timestamp, before zipping. Then `signtool verify /pa` checks them.

| Secret | |
|---|---|
| `PAPERCUT_SIGN_PFX_BASE64`, `PAPERCUT_SIGN_PFX_PASSWORD` | a code-signing certificate as a `.pfx`, base64-encoded (`PAPERCUT_SIGN_PFX` takes a path instead, outside CI) |
| `PAPERCUT_SIGN_THUMBPRINT` | or a certificate in the runner's certificate store by its SHA-1 thumbprint: a hardware token's (EV certificates, and OV ones issued since June 2023, come on a token or in a cloud HSM) |
| `PAPERCUT_SIGN_TIMESTAMP` (a variable) | the timestamp server; DigiCert's by default |

`signtool` comes from the Windows SDK (`PAPERCUT_SIGNTOOL` overrides where). A token-held certificate needs its driver on the Windows runner, and the runner's user signed in to it.

**macOS (Developer ID and notarization):** the app's libraries and executables are signed inside out with the Developer ID, the hardened runtime and a secure timestamp, with one entitlement: library validation off, so code mods built by players still load. With notarization credentials too, the app is sent to Apple's notary service, waited for, stapled and checked with `spctl`, and the zip says nothing about Open Anyway.

| Secret | |
|---|---|
| `PAPERCUT_MACOS_SIGN_IDENTITY` | the identity's name, `Developer ID Application: <name> (<team ID>)` |
| `PAPERCUT_MACOS_CERT_P12_BASE64`, `PAPERCUT_MACOS_CERT_PASSWORD` | the certificate and its key as a `.p12`, base64-encoded: imported into a keychain of the run's own (without them, the Mac runner's login keychain must have it) |
| `PAPERCUT_NOTARY_KEY_BASE64`, `PAPERCUT_NOTARY_KEY_ID`, `PAPERCUT_NOTARY_ISSUER` | an App Store Connect API key (`.p8`) for `notarytool` |
| `PAPERCUT_NOTARY_APPLE_ID`, `PAPERCUT_NOTARY_PASSWORD`, `PAPERCUT_NOTARY_TEAM_ID` | or an Apple ID with an app-specific password |

The iOS `.ipa` files stay unsigned (AltStore, SideStore and Sideloadly sign them on install), and the APKs debug-signed.

## Nothing from the disc

The usual builds compile in a few pieces extracted from the disc when a checkout has them: the memory card banner and icon, and the DSP microcode (`build/<version>/include/assets`). A release must not contain them.

- **Separate builds:** `release.py` builds in its own folders (`build-release-<jp|us>`) with `-DTTYD_NO_DISC_ASSETS=ON`, which forces the stand-ins. The APKs get the same through Gradle's `-PnoDiscAssets`.
- **A check:** it then searches every executable and library it packages for bytes of each extracted asset, and stops the release if it finds one. (Where there are no extracted assets to compare with, as on the Mac, the builds had nothing to compile in.)
- **The disc image itself:** it never goes in a package. Players supply their own.
