; Original project installer. Build with tools/package-windows.ps1.
#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef AppVersion
  #define AppVersion "0.4.1"
#endif
#ifndef VCRuntimeMinor
  #define VCRuntimeMinor 50
#endif
#ifndef VCRuntimeBuild
  #define VCRuntimeBuild 35719
#endif

[Setup]
AppId={{84F314BF-7489-4C27-9FB4-56724CC66CC3}
AppName=Neon Coast
AppVersion={#AppVersion}
AppVerName=Neon Coast {#AppVersion}
AppPublisher=Neon Coast contributors
VersionInfoDescription=Neon Coast Windows Installer
VersionInfoProductName=Neon Coast
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\NeonCoast
DefaultGroupName=Neon Coast
DisableProgramGroupPage=yes
DisableDirPage=no
UsePreviousAppDir=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
WizardStyle=modern
SetupIconFile={#StageDir}\NeonCoast.ico
UninstallDisplayIcon={app}\NeonCoast.ico
LicenseFile={#StageDir}\Licenses\RELEASE_LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=NeonCoast-{#AppVersion}-Windows-Setup
Compression=lzma2/fast
SolidCompression=yes
LZMANumBlockThreads=2
CloseApplications=yes
CloseApplicationsFilter=NeonCoast.exe
RestartApplications=no
SetupLogging=yes
Uninstallable=yes
UninstallDisplayName=Neon Coast
; Uninstall removes only files installed by this script. Never remove Saved.

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl,NeonCoast-Chinese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimp.DesktopShortcut=创建桌面快捷方式
chinesesimp.ShortcutGroup=快捷方式：
chinesesimp.ControlsSupport=操作说明与帮助
chinesesimp.LaunchGame=启动 Neon Coast
chinesesimp.InstallingRuntime=正在安装 Microsoft Visual C++ 运行库…
chinesesimp.RuntimePermission=需要 Microsoft Visual C++ 运行库。请允许其 Windows 管理员提示，然后重试。游戏本身仅为当前用户安装。
chinesesimp.RuntimeFailed=Microsoft Visual C++ 运行库安装失败。请安装 Prerequisites\vc_redist.x64.exe 后重试。退出码：
chinesesimp.RuntimeUnavailable=Microsoft Visual C++ 运行库仍不可用。如果系统要求重启，请重启后重试。
english.DesktopShortcut=Create a desktop shortcut
english.ShortcutGroup=Shortcuts:
english.ControlsSupport=Controls and support
english.LaunchGame=Launch Neon Coast
english.InstallingRuntime=Installing the Microsoft Visual C++ runtime...
english.RuntimePermission=The Microsoft Visual C++ runtime is required. Allow its Windows administrator prompt, then retry. The game itself installs for this user only.
english.RuntimeFailed=Microsoft Visual C++ runtime installation failed. Install Prerequisites\vc_redist.x64.exe, then retry. Exit code:
english.RuntimeUnavailable=Microsoft Visual C++ runtime registration is still unavailable. Restart Windows if requested, then retry.

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; GroupDescription: "{cm:ShortcutGroup}"; Flags: checkedonce

[Files]
Source: "{#StageDir}\Prerequisites\vc_redist.x64.exe"; Flags: dontcopy
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Neon Coast\Neon Coast"; Filename: "{app}\NeonCoast.exe"; WorkingDir: "{app}"; IconFilename: "{app}\NeonCoast.ico"
Name: "{autoprograms}\Neon Coast\{cm:ControlsSupport}"; Filename: "{app}\START_HERE.txt"
Name: "{autodesktop}\Neon Coast"; Filename: "{app}\NeonCoast.exe"; WorkingDir: "{app}"; IconFilename: "{app}\NeonCoast.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\NeonCoast.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchGame}"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsVCRuntime: Boolean;
var
  Installed, Major, Minor, Build: Cardinal;
begin
  Result := True;
  if not RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then Exit;
  if Installed <> 1 then Exit;
  if not RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Major', Major) then Exit;
  if not RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Minor', Minor) then Exit;
  if not RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Bld', Build) then Exit;
  Result := not ((Major > 14) or ((Major = 14) and
    ((Minor > {#VCRuntimeMinor}) or ((Minor = {#VCRuntimeMinor}) and (Build >= {#VCRuntimeBuild})))));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  if not NeedsVCRuntime then Exit;
  ExtractTemporaryFile('vc_redist.x64.exe');
  WizardForm.StatusLabel.Caption := CustomMessage('InstallingRuntime');
  if not ShellExec('runas', ExpandConstant('{tmp}\vc_redist.x64.exe'),
    '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := CustomMessage('RuntimePermission');
    Exit;
  end;
  if (ResultCode <> 0) and (ResultCode <> 1638) and (ResultCode <> 3010) then
  begin
    Result := CustomMessage('RuntimeFailed') + ' ' + IntToStr(ResultCode);
    Exit;
  end;
  if ResultCode = 3010 then NeedsRestart := True;
  if NeedsVCRuntime then
    Result := CustomMessage('RuntimeUnavailable');
end;
