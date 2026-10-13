; Inno Setup script for the Virtual Jaguar installer (#839).
; Built by app/packaging/windows/package.sh:
;   ISCC /DAppVersion=4.0.0 /DSourceDir=<staged tree> /DOutputDir=<dir> /DOutputName=<name> virtualjaguar.iss
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
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
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

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Virtual Jaguar"; Filename: "{app}\virtualjaguar.exe"
Name: "{autodesktop}\Virtual Jaguar"; Filename: "{app}\virtualjaguar.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\virtualjaguar.exe"; Description: "Launch Virtual Jaguar"; Flags: nowait postinstall skipifsilent
