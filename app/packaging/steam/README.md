# Steam desktop distribution

Steam uses three OS-filtered depots: Windows x64, macOS universal (Intel and
Apple Silicon), and Linux x86_64. App/depot IDs come from the owning Steamworks
account; do not infer a depot ID by adding to the App ID.

## Payloads and launch settings

| Depot OS filter | Staged root | Launch executable |
| --- | --- | --- |
| Windows | Contents of the Windows portable ZIP's `VirtualJaguar/`, with `portable.txt` removed from the staged copy | `virtualjaguar.exe` |
| macOS | Directory containing the signed/notarized `Virtual Jaguar.app` | `Virtual Jaguar.app` |
| Linux | Extracted x86_64 AppImage AppDir, including `AppRun` and `usr/` | `AppRun` |

Use the same qualified version/source for all three. On Linux, extracting the
project's AppImage with `--appimage-extract` avoids requiring FUSE in the Steam
install. Keep AppRun, file permissions, symlinks and bundled libraries intact.
On macOS, retain the app bundle's signature and stapled ticket (use `ditto` for
copies); verify the staged copy with `codesign --verify --deep --strict` and
`xcrun stapler validate`. The Mac App Store sandboxed package is a separate build.

Do not use the Windows MSIX or setup EXE as depot content. `portable.txt` would
put saves in the Steam-managed installation directory; removing it makes the
existing app use its per-user data path. Never stage personal `data/` folders,
credentials, game files or BIOS files. The generator rejects portable markers,
Steam login files and symlinks that leave the supplied payload, but is not a
substitute for inspecting the complete payload before uploading.

Set the launch executable and matching OS in Steamworks Installation settings.
Do not advertise Steam Cloud, Steam Achievements, full controller support or
Steam Deck verification until those features have actually been configured and
qualified. RetroAchievements is a separate optional integration. Link play is
for supported Jaguar link-cable games on a LAN.

## SteamPipe

Generate scripts with the IDs assigned by Steamworks and absolute payload paths:

```sh
python3 app/packaging/steam/manifests.py \
  --app-id "$STEAM_APP_ID" \
  --windows-depot "$STEAM_WINDOWS_DEPOT" --windows-root "$WINDOWS_PAYLOAD" \
  --macos-depot "$STEAM_MACOS_DEPOT" --macos-root "$MACOS_PAYLOAD" \
  --linux-depot "$STEAM_LINUX_DEPOT" --linux-root "$LINUX_PAYLOAD" \
  --description "Virtual Jaguar release candidate, source COMMIT" \
  --output /absolute/path/to/fresh-steampipe-preview
```

Scripts default to `Preview=1`; SteamCMD generates local manifests/logs without
uploading. Supply `--upload` and a new output directory to generate upload-mode
scripts. The script itself never authenticates, uploads, changes a live branch
or publishes. `SetLive` is always empty. Inspect the preview before uploading,
then select the uploaded build on a password-protected beta branch in Steamworks.

Use Valve's SteamCMD/ContentBuilder with its normal interactive login and Steam
Guard. Never put a password or Steam Guard token in these files, Git, shell
history or CI logs. Keep SteamCMD's authenticated `config.vdf` outside the repo
and outside every content root. Test actual Steam-client install/update/offline
launch, save retention, controllers, external file import and uninstall on each
OS before requesting build review.

References: [SteamPipe](https://partner.steamgames.com/doc/sdk/uploading),
[depots](https://partner.steamgames.com/doc/store/application/depots),
[graphical assets](https://partner.steamgames.com/doc/store/assets).
