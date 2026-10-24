; Emulon X — Windows installer (Inno Setup 6).
; GitHub builds this automatically: see .github/workflows/build.yml.
; To build it yourself: install Inno Setup 6, build the plugin, put the files in ..\dist, then
;   ISCC.exe /DAppVersion=1.1.1ea /DNumericVersion=1.1.1 installer\EmulonX.iss
; SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef AppVersion
  #define AppVersion "1.1.1ea"
#endif
; Windows needs plain numbers for the file version (the tag like "ea" is only in the name).
#ifndef NumericVersion
  #define NumericVersion "1.1.1"
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
VersionInfoVersion={#NumericVersion}
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

; The GNU AGPL text (LICENSE) plus a short, less serious note at the end.
LicenseFile=License.txt
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
Name: "banks"; Description: "Base banks (the sounds that come with Emulon X)"; Types: full plugin custom
Name: "standalone"; Description: "Standalone app (play Emulon X without a DAW)"; Types: full

[Dirs]
; The folders picked on the "Choose folders" page. They are kept when Emulon X is uninstalled.
Name: "{code:BanksDir}"; Components: banks; Flags: uninsneveruninstall
Name: "{code:PresetsDir}"; Flags: uninsneveruninstall

[Files]
Source: "{#SourceDir}\Emulon X.vst3\*"; DestDir: "{app}\Emulon X.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "BaseBanks\*.emxb"; DestDir: "{code:BanksDir}"; Components: banks; Flags: ignoreversion skipifsourcedoesntexist uninsneveruninstall
Source: "{#SourceDir}\Emulon X.exe"; DestDir: "{code:StandaloneDir}"; Components: standalone; Flags: ignoreversion
Source: "emulonx.ico"; DestDir: "{commonpf64}\Emulon X\Uninstall"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{commonpf64}\Emulon X"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{commonpf64}\Emulon X"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Emulon X"; Filename: "{code:StandaloneDir}\Emulon X.exe"; Components: standalone

[Run]
Filename: "{code:StandaloneDir}\Emulon X.exe"; Description: "Open the Emulon X standalone app"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone

[UninstallDelete]
; Only remove the plugin bundle we put in the VST3 folder, never the folder itself.
Type: filesandordirs; Name: "{app}\Emulon X.vst3"

[Registry]
; Tells the plugin where the banks and presets were put (read on its next start).
Root: HKLM64; Subkey: "Software\Emulon X"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Emulon X"; ValueType: string; ValueName: "BaseBanksFolder"; ValueData: "{code:BanksDir}"; Components: banks
Root: HKLM64; Subkey: "Software\Emulon X"; ValueType: string; ValueName: "PresetsFolder"; ValueData: "{code:PresetsDir}"
Root: HKLM64; Subkey: "Software\Emulon X"; ValueType: string; ValueName: "StandaloneFolder"; ValueData: "{code:StandaloneDir}"; Components: standalone

[Code]
var
  FolderPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  { One page for every other folder, so nothing ends up somewhere you don't know about. }
  FolderPage := CreateInputDirPage(wpSelectComponents,
    'Choose folders', 'Where should the banks, your presets and the standalone app go?',
    'Emulon X finds these folders by itself. You can add more folders later in its Settings > Folders.',
    False, '');
  FolderPage.Add('Base banks (the sounds that come with Emulon X):');
  FolderPage.Add('Your presets (where Save puts new presets):');
  FolderPage.Add('Standalone app (Emulon X without a DAW):');
  FolderPage.Values[0] := GetPreviousData('BanksDir', ExpandConstant('{userdocs}\Emulon X\Base Banks'));
  FolderPage.Values[1] := GetPreviousData('PresetsDir', ExpandConstant('{userdocs}\Emulon X\Presets'));
  FolderPage.Values[2] := GetPreviousData('StandaloneDir', ExpandConstant('{commonpf64}\Emulon X'));
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  { Remembered for the next update, so you don't have to pick them again. }
  SetPreviousData(PreviousDataKey, 'BanksDir', FolderPage.Values[0]);
  SetPreviousData(PreviousDataKey, 'PresetsDir', FolderPage.Values[1]);
  SetPreviousData(PreviousDataKey, 'StandaloneDir', FolderPage.Values[2]);
end;

procedure EnableRow(Index: Integer; Enabled: Boolean);
begin
  FolderPage.PromptLabels[Index].Enabled := Enabled;
  FolderPage.Edits[Index].Enabled := Enabled;
  FolderPage.Buttons[Index].Enabled := Enabled;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { Grey out the folders of parts you chose not to install. }
  if CurPageID = FolderPage.ID then
  begin
    EnableRow(0, WizardIsComponentSelected('banks'));
    EnableRow(2, WizardIsComponentSelected('standalone'));
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  I: Integer;
begin
  Result := True;
  if CurPageID = FolderPage.ID then
    for I := 0 to 2 do
      if FolderPage.Edits[I].Enabled and (Trim(FolderPage.Values[I]) = '') then
      begin
        MsgBox('Please choose a folder for: ' + FolderPage.PromptLabels[I].Caption, mbError, MB_OK);
        Result := False;
        Exit;
      end;
end;

function BanksDir(Param: String): String;
begin
  Result := Trim(FolderPage.Values[0]);
end;

function PresetsDir(Param: String): String;
begin
  Result := Trim(FolderPage.Values[1]);
end;

function StandaloneDir(Param: String): String;
begin
  Result := Trim(FolderPage.Values[2]);
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  { The summary before installing lists every folder that will be used. }
  Result := 'Plugin (VST3) folder:' + NewLine + Space + WizardDirValue + NewLine + NewLine;
  if WizardIsComponentSelected('banks') then
    Result := Result + 'Base banks:' + NewLine + Space + BanksDir('') + NewLine + NewLine;
  Result := Result + 'Your presets:' + NewLine + Space + PresetsDir('') + NewLine + NewLine;
  if WizardIsComponentSelected('standalone') then
    Result := Result + 'Standalone app:' + NewLine + Space + StandaloneDir('') + NewLine + NewLine;
  Result := Result + 'Uninstaller, README and license:' + NewLine + Space + ExpandConstant('{commonpf64}\Emulon X') + NewLine + NewLine;
  Result := Result + MemoTypeInfo + NewLine + NewLine + MemoComponentsInfo;
end;
