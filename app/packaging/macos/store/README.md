# Mac App Store build

Configure the standalone app with `-DVJ_MAC_APP_STORE=ON`. This is a separate
build directory and signing path from the Developer ID website release.

The Store edition uses native open/save panels and app-scoped security bookmarks.
Library folders, imported fonts/themes, and custom shader selections retain access
across launches. CUE/M3U and shader presets ask for a containing-folder grant so
referenced files can be read. Missing/revoked grants require reconnecting the volume
or selecting the folder again. Removing a library folder removes its persisted
bookmark; active readers keep their current scope until shutdown. Saves, cache and
settings use SDL's preference path inside the sandbox container. No automatic
migration from the website edition's data is claimed.

The shader runtime loads only from the signed app bundle. No external code-loading
exception, JIT, disable-library-validation or broad filesystem exception is used.
Discord Rich Presence is unavailable in this edition because its external Unix
socket is outside the sandbox. The website edition retains it. The privacy manifest
discloses optional RetroAchievements account identifiers and gameplay interaction
data for app functionality, linked to the account and never used for advertising
tracking. Required-reason APIs cover own preferences, container/user-selected file
metadata and elapsed-time measurement.

Network client/server access supports optional artwork/achievements, LAN netplay
and local debugging; USB/Bluetooth access supports game controllers.

## Build and qualify

Use the normal CMake source/dependency configuration from `app/README.md`, adding:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake -S app -B build-mac-store \
  -DCMAKE_BUILD_TYPE=Release -DVJ_MAC_APP_STORE=ON -DVJ_APP_SIGN_DEV_BUILD=OFF \
  -DVJ_BUILD_CHANNEL=release -DVJ_TV_DEFAULT=manual \
  -DVJ_LIBRASHADER_LIB=/absolute/path/to/librashader.dylib
DEVELOPER_DIR=/Library/Developer/CommandLineTools cmake --build build-mac-store -j8
DEVELOPER_DIR=/Library/Developer/CommandLineTools ctest --test-dir build-mac-store --output-on-failure
cmake --install build-mac-store --prefix /absolute/new/staging
bash app/packaging/macos/make-app.sh /absolute/new/staging \
  '/absolute/new/Virtual Jaguar.app' 3.10.0 1 11.0
bash app/packaging/macos/store/package.sh '/absolute/new/Virtual Jaguar.app' --test \
  'Developer ID Application: Joseph Mattiello (S32Z3HMYVQ)'
```

A test signature is for sandbox qualification only. It is not an App Store upload.
Build both architectures (or use the normal per-architecture build/merge helper)
with the Store option enabled before distribution signing. Inspect `otool -L` for
all nested Mach-O files; only system libraries and bundled code may be referenced.

Verify in the signed **bundle**, not the bare executable:

- On a fresh container, an arbitrary command-line path has no filesystem grant.
- Open a public demo through File > Open, then reopen it from Recents after relaunch.
- Add a test library folder, relaunch, and confirm scanning and launching still work.
- Rename that folder and relaunch: the bookmark follows it and updates configured paths.
- Disconnect/reconnect removable media; confirm missing access gives a recoverable path.
- Open a CUE/M3U set and grant its containing folder; verify sibling files and disc changes.
- Cancel each panel; import a font/theme, export a theme, and use an external shader folder.
- Check gamepad input, audio, save/load state, achievements login/logout and optional LAN.
- Capture final Mac screenshots at accepted sizes; keep private ROM paths out of images.

## Distribution signing

Use a Mac App Store application certificate, installer certificate and Mac App Store
profile for `io.github.libretro.VirtualJaguar` (team `S32Z3HMYVQ`). This helper refuses
website signing identities and an existing output path:

```sh
bash app/packaging/macos/store/package.sh '/absolute/new/Virtual Jaguar.app' \
  /absolute/path/Store.provisionprofile \
  'Apple Distribution: Joseph Mattiello (S32Z3HMYVQ)' \
  '3rd Party Mac Developer Installer: Joseph Mattiello (S32Z3HMYVQ)' \
  /absolute/new/VirtualJaguar-3.10.0-store.pkg
```

Upload the resulting signed `.pkg` with Apple's upload tools. App Store Connect
app ID is `6827855160`. Keep `releaseType=MANUAL`; build upload and review submission
must never change it. Complete reviewer contact, age/privacy/export declarations and
sandbox explanations against the qualified build before submitting for review.
