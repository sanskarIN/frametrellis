; SPDX-License-Identifier: GPL-3.0-or-later
; SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
;
; Inno Setup script for the FrameTrellis Windows installer. It packages the folder produced by
; packaging/windows/package.py:
;
;   ISCC.exe /DAppVersion=1.0.0 /DStageDir=dist\FrameTrellis-1.0.0-windows-x64 ^
;            /DOutputDir=dist /DRepoDir=. packaging\windows\frametrellis.iss
;
; By default it installs for the current user only (no administrator rights needed); the
; installer offers an all-users installation when it is started with administrator rights.

#ifndef AppVersion
  #error Define AppVersion, StageDir, OutputDir and RepoDir on the ISCC command line.
#endif

#define AppName "FrameTrellis"
#define AppExe "frametrellis.exe"
#define ProjectProgId "FrameTrellis.Project"

[Setup]
; The AppId identifies the installation for upgrades and uninstallation; never change it.
AppId={{653A63AF-5BF8-4BD3-95A9-1B95BB5C7E26}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Sanskar and FrameTrellis contributors
AppPublisherURL=https://sanskarin.github.io/frametrellis/
AppSupportURL=https://github.com/sanskarIN/frametrellis/issues
AppUpdatesURL=https://github.com/sanskarIN/frametrellis/releases
AppCopyright=Copyright (C) 2026 Sanskar and FrameTrellis contributors
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 version 1809 is the oldest release Qt 6 supports.
MinVersion=10.0.17763
LicenseFile={#StageDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-windows-x64-setup
SetupIconFile={#RepoDir}\resources\windows\frametrellis.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} installer
VersionInfoCompany=Sanskar and FrameTrellis contributors

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "association"; Description: "Open FrameTrellis projects (.ftproj) with FrameTrellis"; GroupDescription: "File types:"

[InstallDelete]
; Remove files of a previous version first, so no outdated library or plug-in is left behind.
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\share"
Type: filesandordirs; Name: "{app}\licenses"
Type: files; Name: "{app}\*.dll"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "Edit videos with FrameTrellis"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.ftproj"; ValueType: string; ValueName: ""; ValueData: "{#ProjectProgId}"; Flags: uninsdeletevalue; Tasks: association
Root: HKA; Subkey: "Software\Classes\.ftproj\OpenWithProgids"; ValueType: string; ValueName: "{#ProjectProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: association
Root: HKA; Subkey: "Software\Classes\{#ProjectProgId}"; ValueType: string; ValueName: ""; ValueData: "FrameTrellis project"; Flags: uninsdeletekey; Tasks: association
Root: HKA; Subkey: "Software\Classes\{#ProjectProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: association
Root: HKA; Subkey: "Software\Classes\{#ProjectProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: association
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".ftproj"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
