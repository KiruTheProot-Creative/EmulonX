; Emulon X — Windows installer (Inno Setup 6).
; GitHub builds this automatically: see .github/workflows/build.yml.
; To build it yourself: install Inno Setup 6, build the plugin, put the files in ..\dist, then
;   ISCC.exe /DAppVersion=1.1.1 installer\EmulonX.iss
; SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef AppVersion
  #define AppVersion "1.1.1"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

[Setup]
; Never change this id: Windows uses it to recognise updates of the same program.
AppId={{FD936028-B870-420E-9A62-5FF89CBD3473}
AppName=Emulon X
AppVersion={#AppVersion}
AppVerName=Emulon X {#AppVersion}
AppPublisher=KiruTheProot-Creative
AppPublisherURL=https://github.com/KiruTheProot-Creative/EmulonX
AppSupportURL=https://github.com/KiruTheProot-Creative/EmulonX/issues
AppUpdatesURL=https://github.com/KiruTheProot-Creative/EmulonX/releases
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Emulon X installer

; The folder picked on the "where" page is the VST3 folder; the plugin goes inside it.
DefaultDirName={commoncf64}\VST3
UsePreviousAppDir=yes
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
; Keep the uninstaller out of the VST3 folder.
UninstallFilesDir={commonpf64}\Emulon X\Uninstall
UninstallDisplayName=Emulon X
UninstallDisplayIcon={commonpf64}\Emulon X\Uninstall\emulonx.ico

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes

LicenseFile=..\LICENSE
OutputDir=..\installer-out
OutputBaseFilename=EmulonX-Setup-{#AppVersion}
SetupIconFile=emulonx.ico
WizardStyle=modern
WizardImageFile=wizard-164.bmp,wizard-192.bmp,wizard-246.bmp,wizard-328.bmp
WizardSmallImageFile=small-55.bmp,small-64.bmp,small-83.bmp,small-110.bmp
WizardImageStretch=no
Compression=lzma2/max
SolidCompression=yes

[Messages]
WelcomeLabel2=This will install Emulon X {#AppVersion}, a free and open-source sampler, on your computer.%n%nPlease close your DAW (for example Ableton Live) before you continue.
SelectDirDesc=Where should the Emulon X plugin go?
SelectDirLabel3=The plugin (Emulon X.vst3) will be put into this VST3 folder. Ableton Live and most other DAWs look here automatically, so only change it if you keep your plugins somewhere else.
SelectDirBrowseLabel=To continue, click Next. To pick a different folder, click Browse.
FinishedLabel=Emulon X is installed.%n%nIn Ableton Live: Options > Preferences > Plug-Ins, make sure "Use VST3 Plug-In System Folders" is on and click Rescan. You'll find Emulon X under Plug-Ins > VST3.

[Types]
Name: "full"; Description: "Everything (recommended)"
Name: "plugin"; Description: "Only the plugin"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Emulon X plugin (VST3)"; Types: full plugin custom; Flags: fixed
Name: "banks"; Description: "Base banks (into Documents\Emulon X\Base Banks)"; Types: full plugin custom
Name: "standalone"; Description: "Standalone app (play Emulon X without a DAW)"; Types: full

[Dirs]
; Your folders in Documents. They are kept when Emulon X is uninstalled.
Name: "{userdocs}\Emulon X"; Flags: uninsneveruninstall
Name: "{userdocs}\Emulon X\Base Banks"; Flags: uninsneveruninstall
Name: "{userdocs}\Emulon X\Presets"; Flags: uninsneveruninstall

[Files]
Source: "{#SourceDir}\Emulon X.vst3\*"; DestDir: "{app}\Emulon X.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "BaseBanks\*.emxb"; DestDir: "{userdocs}\Emulon X\Base Banks"; Components: banks; Flags: ignoreversion skipifsourcedoesntexist uninsneveruninstall
Source: "{#SourceDir}\Emulon X.exe"; DestDir: "{commonpf64}\Emulon X"; Components: standalone; Flags: ignoreversion
Source: "emulonx.ico"; DestDir: "{commonpf64}\Emulon X\Uninstall"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{commonpf64}\Emulon X"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{commonpf64}\Emulon X"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Emulon X"; Filename: "{commonpf64}\Emulon X\Emulon X.exe"; Components: standalone

[Run]
Filename: "{commonpf64}\Emulon X\Emulon X.exe"; Description: "Open the Emulon X standalone app"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone

[UninstallDelete]
; Only remove the plugin bundle we put in the VST3 folder, never the folder itself.
Type: filesandordirs; Name: "{app}\Emulon X.vst3"
