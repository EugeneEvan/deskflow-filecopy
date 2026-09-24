; SPDX-FileCopyrightText: Copyright (C) 2026 Deskflow FileCopy contributors
; SPDX-License-Identifier: MIT
; Built by package-filecopy.ps1. This is deliberately separate from upstream's
; service-installing MSI and never stops another Deskflow process.

#ifndef PayloadDir
  #error PayloadDir must name a clean, validated package directory.
#endif
#ifndef PackageVersion
  #error PackageVersion is required.
#endif
#ifndef ProjectUrl
  #error ProjectUrl is required.
#endif
#ifndef OutputDir
  #error OutputDir is required.
#endif
#ifndef PackageBaseName
  #error PackageBaseName is required.
#endif

#ifdef Testing
  #define ProductName "Deskflow FileCopy Packaging Test"
  #define ProductId "{94B05CA0-EF48-4D37-A49E-B5B96870EAD0}"
#else
  #define ProductName "Deskflow FileCopy"
  #define ProductId "{F1C33D63-3CBD-405E-AE82-BA74ECA936C0}"
#endif

[Setup]
AppId={{#ProductId}
AppName={#ProductName}
AppVersion={#PackageVersion}
AppPublisher=Deskflow FileCopy contributors
AppPublisherURL={#ProjectUrl}
AppSupportURL={#ProjectUrl}/issues
AppUpdatesURL={#ProjectUrl}/releases
DefaultDirName={localappdata}\Programs\{#ProductName}
DefaultGroupName={#ProductName}
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#PackageBaseName}-setup
SetupIconFile={#PayloadDir}\deskflow-filecopy.ico
UninstallDisplayIcon={app}\deskflow.exe
LicenseFile={#PayloadDir}\LICENSE
InfoBeforeFile={#PayloadDir}\INSTALL-NOTES.txt
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
AlwaysRestart=no
SetupLogging=yes
Uninstallable=yes
UsePreviousTasks=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "autostart"; Description: "Start Deskflow FileCopy when I sign in (desktop mode)"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "\settings\*"; Flags: ignoreversion recursesubdirs createallsubdirs
; Preserve this installation's settings on upgrades and uninstall. The portable
; settings file also prevents importing another Deskflow installation's profile.
Source: "{#PayloadDir}\settings\Deskflow.conf"; DestDir: "{app}\settings"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\{#ProductName}"; Filename: "{app}\deskflow.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\{#ProductName}"; Filename: "{app}\deskflow.exe"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{userstartup}\{#ProductName}"; Filename: "{app}\deskflow.exe"; WorkingDir: "{app}"; Tasks: autostart

[InstallDelete]
; Remove only this product's shortcut when login startup is disabled on upgrade.
Type: files; Name: "{userstartup}\{#ProductName}.lnk"; Tasks: not autostart

; There is intentionally no [Run], service, firewall, or process-killing action.
; The optional startup shortcut runs in the signed-in user's desktop session.
[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not WizardSilent then begin
    if FileExists(ExpandConstant('{app}\deskflow.exe')) and
       not FileExists(ExpandConstant('{app}\deskflow-filecopy.package')) then begin
      MsgBox('This directory contains another Deskflow installation. Choose a different, writable directory for Deskflow FileCopy.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  { Repeat the check for silent installation, which can skip wpSelectDir. }
  if FileExists(ExpandConstant('{app}\deskflow.exe')) and
     not FileExists(ExpandConstant('{app}\deskflow-filecopy.package')) then
    Result := 'Refusing to overwrite a different Deskflow installation. Choose a separate directory.';
end;
