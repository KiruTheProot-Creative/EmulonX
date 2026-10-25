; Emulon X SD (the skin designer) — Windows installer (Inno Setup 6). GitHub builds it: see .github/workflows/build.yml.
;   ISCC.exe /DAppVersion=0.1af /DNumericVersion=0.1.0 /DSourceDir=..\sd-dist installer\EmulonXSD.iss
; SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef AppVersion
  #define AppVersion "0.1af"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\sd-dist"
#endif

[Setup]
; Never change this id: Windows (and the Emulon X Hub) use it to recognise this program.
AppId={{B82616AB-20BE-463B-9A40-0B92677B574A}
AppName=Emulon X SD
AppVersion={#AppVersion}
AppVerName=Emulon X SD {#AppVersion}
AppPublisher=KiruTheProot-Creative
AppPublisherURL=https://github.com/KiruTheProot-Creative/EmulonX
VersionInfoVersion={#NumericVersion}
VersionInfoDescription=Emulon X SD installer
DefaultDirName={commonpf64}\Emulon X SD
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
LicenseFile=License.txt
OutputDir=..\installer-out
OutputBaseFilename=EmulonX-SD-Setup-{#AppVersion}
SetupIconFile=emulonx.ico
UninstallDisplayIcon={app}\Emulon X SD.exe
WizardStyle=modern
WizardImageFile=wizard-164.bmp,wizard-192.bmp,wizard-246.bmp,wizard-328.bmp
WizardSmallImageFile=small-55.bmp,small-64.bmp,small-83.bmp,small-110.bmp
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes

[Messages]
WelcomeLabel2=This will install Emulon X SD {#AppVersion}, the skin designer for Emulon X.

[Files]
Source: "{#SourceDir}\Emulon X SD.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Emulon X SD"; Filename: "{app}\Emulon X SD.exe"

[Registry]
; Double-clicking a skin file opens it in Emulon X SD.
Root: HKA; Subkey: "Software\Classes\.emxd"; ValueType: string; ValueData: "EmulonX.Skin"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\EmulonX.Skin"; ValueType: string; ValueData: "Emulon X skin"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\EmulonX.Skin\shell\open\command"; ValueType: string; ValueData: """{app}\Emulon X SD.exe"" ""%1"""

[Run]
Filename: "{app}\Emulon X SD.exe"; Description: "Open Emulon X SD"; Flags: nowait postinstall skipifsilent runasoriginaluser
