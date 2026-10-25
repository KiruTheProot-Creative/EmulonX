; Emulon X Hub (the launcher) — Windows installer (Inno Setup 6). GitHub builds it: see .github/workflows/build.yml.
;   ISCC.exe /DAppVersion=0.1af /DNumericVersion=0.1.0 /DSourceDir=..\hub-dist installer\EmulonXHub.iss
; SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef AppVersion
  #define AppVersion "0.1af"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\hub-dist"
#endif

[Setup]
; Never change this id: Windows uses it to recognise updates of the Hub.
AppId={{418FA9BC-6948-48A4-A480-4EE7CD914ED1}
AppName=Emulon X Hub
AppVersion={#AppVersion}
AppVerName=Emulon X Hub {#AppVersion}
AppPublisher=KiruTheProot-Creative
AppPublisherURL=https://github.com/KiruTheProot-Creative/EmulonX
VersionInfoVersion={#NumericVersion}
VersionInfoDescription=Emulon X Hub installer
DefaultDirName={commonpf64}\Emulon X Hub
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DisableDirPage=auto
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=force
RestartApplications=no
LicenseFile=License.txt
OutputDir=..\installer-out
OutputBaseFilename=EmulonX-Hub-Setup-{#AppVersion}
SetupIconFile=emulonx.ico
UninstallDisplayIcon={app}\Emulon X Hub.exe
WizardStyle=modern
WizardImageFile=wizard-164.bmp,wizard-192.bmp,wizard-246.bmp,wizard-328.bmp
WizardSmallImageFile=small-55.bmp,small-64.bmp,small-83.bmp,small-110.bmp
Compression=lzma2/max
SolidCompression=yes

[Messages]
WelcomeLabel2=This will install the Emulon X Hub {#AppVersion}: one place to download and update Emulon X, Emulon X SD, skins and banks.

[Tasks]
Name: "desktopicon"; Description: "Put a shortcut on the desktop"

[Files]
Source: "{#SourceDir}\Emulon X Hub.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Emulon X Hub"; Filename: "{app}\Emulon X Hub.exe"
Name: "{autodesktop}\Emulon X Hub"; Filename: "{app}\Emulon X Hub.exe"; Tasks: desktopicon

[Run]
; Also runs after a quiet update from inside the Hub, so it opens again by itself.
Filename: "{app}\Emulon X Hub.exe"; Description: "Open the Emulon X Hub"; Flags: nowait postinstall runasoriginaluser

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C reg delete ""HKCU\Software\Microsoft\Windows\CurrentVersion\Run"" /v ""Emulon X Hub"" /f"; Flags: runhidden; RunOnceId: "RemoveAutostart"
