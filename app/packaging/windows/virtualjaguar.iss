; Inno Setup script for the Virtual Jaguar installer (#839).
; Built by app/packaging/windows/package.sh:
;   ISCC [/DArm64] /DAppVersion=4.0.0 /DSourceDir=<staged tree> /DOutputDir=<dir> /DOutputName=<name> virtualjaguar.iss
; Per-user by default (no admin prompt); the first dialog offers "all users".  The installed
; program is NOT portable (no portable.txt), so settings, saves and states live in
; %APPDATA%\VirtualJaguar and survive an uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir not set
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputName
  #define OutputName "VirtualJaguar-setup"
#endif

[Setup]
AppId={{6D1A0C3E-5B7F-4C42-9E17-0A8F3E2B5C91}
AppName=Virtual Jaguar
AppVersion={#AppVersion}
AppVerName=Virtual Jaguar {#AppVersion}
AppPublisher=Virtual Jaguar contributors
AppPublisherURL=https://jaguar.provenance-emu.com
AppSupportURL=https://github.com/libretro/virtualjaguar-libretro/issues
AppUpdatesURL=https://github.com/libretro/virtualjaguar-libretro/releases
DefaultDirName={autopf}\Virtual Jaguar
DefaultGroupName=Virtual Jaguar
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
; x64: also installs on ARM64 Windows (x64 emulation).  arm64: native ARM64 Windows only.
#ifdef Arm64
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#SourceDir}\..\virtualjaguar.ico
UninstallDisplayIcon={app}\virtualjaguar.exe
LicenseFile={#SourceDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
; Lets a newer installer replace a running copy cleanly.
CloseApplications=yes
; File types (the "associate" task below) change what Explorer shows: tell the shell.
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "associate"; Description: "Open Jaguar games and disc images (.j64 .jag .rom .abs .cof .cue .chd .cdi) with Virtual Jaguar"; GroupDescription: "File types:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

; AppUserModelID is the id the program sets for itself (platform_windows.cpp): the Start menu / desktop / pinned
; shortcut, the running window and its Jump List are then one taskbar button.
[Icons]
Name: "{autoprograms}\Virtual Jaguar"; Filename: "{app}\virtualjaguar.exe"; AppUserModelID: "io.github.libretro.VirtualJaguar"
Name: "{autodesktop}\Virtual Jaguar"; Filename: "{app}\virtualjaguar.exe"; Tasks: desktopicon; AppUserModelID: "io.github.libretro.VirtualJaguar"

; File types.  KEEP IN SYNC with src/platform/commands.h (VJ_ASSOC_LIST), Info.plist.in, the .desktop file and the
; mime XML: the CTest platform_assoc_sync checks the OpenWithProgids and SupportedTypes lines below.
; HKA = HKCU for the default per-user install, HKLM for "all users".
;  * Applications\virtualjaguar.exe (always): "Open with > Virtual Jaguar", and it lets Windows list recent games
;    in the Jump List (the program adds them with SHAddToRecentDocs).
;  * The associate task: a ProgId, an OpenWithProgids entry per extension (Virtual Jaguar is offered, nobody's
;    default is taken), and -- only for the Jaguar-specific .j64 / .jag -- the default handler.  Windows 10/11
;    keep the user's own choice (UserChoice) over this.
[Registry]
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Virtual Jaguar"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\virtualjaguar.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".j64"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".jag"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".rom"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".abs"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".cof"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".cue"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".chd"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\virtualjaguar.exe\SupportedTypes"; ValueType: string; ValueName: ".cdi"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\VirtualJaguar.Game"; ValueType: string; ValueName: ""; ValueData: "Atari Jaguar game or disc image"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\VirtualJaguar.Game\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\virtualjaguar.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\VirtualJaguar.Game\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\virtualjaguar.exe"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.j64\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.jag\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.rom\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.abs\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.cof\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.cue\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.chd\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.cdi\OpenWithProgids"; ValueType: string; ValueName: "VirtualJaguar.Game"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.j64"; ValueType: string; ValueName: ""; ValueData: "VirtualJaguar.Game"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.jag"; ValueType: string; ValueName: ""; ValueData: "VirtualJaguar.Game"; Flags: uninsdeletevalue; Tasks: associate

[Run]
Filename: "{app}\virtualjaguar.exe"; Description: "Launch Virtual Jaguar"; Flags: nowait postinstall skipifsilent
