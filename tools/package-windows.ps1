param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '0.4.1',
    [ValidateSet('All','StageOnly','PortableOnly','InstallerOnly')][string]$Mode = 'All',
    [string]$SourcePackage = '',
    [string]$Engine = '',
    [string]$Python = '',
    [string]$Compiler = '',
    [string]$OutputDirectory = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if (-not $SourcePackage) { $SourcePackage = Join-Path $repo 'builds\NeonCoast-UE-Windows\Windows' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo "builds\releases\$Version" }
$SourcePackage = [IO.Path]::GetFullPath($SourcePackage).TrimEnd('\')
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($OutputDirectory.StartsWith($SourcePackage+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be outside the Unreal package.'
}
foreach ($required in @('NeonCoast.exe','NeonCoast\Binaries\Win64\NeonCoast.exe','NeonCoast\Content\Paks\NeonCoast-Windows.ucas')) {
    if (-not (Test-Path -LiteralPath (Join-Path $SourcePackage $required))) { throw "Missing cooked package file: $required" }
}
if (-not (Test-Path -LiteralPath (Join-Path $repo 'docs\RELEASE_LICENSE.txt'))) {
    throw 'docs/RELEASE_LICENSE.txt must be present before making a public distribution.'
}
if (-not $Python) {
    $localPython = Join-Path $repo '.venv-mcp\Scripts\python.exe'
    if (Test-Path -LiteralPath $localPython) { $Python = $localPython }
    else { $Python = (Get-Command python.exe -ErrorAction SilentlyContinue | Select-Object -First 1).Source }
}
if (-not $Python) { throw 'Python 3 is required for the encrypted-content release gate; pass -Python with its executable.' }
$encryptionGate = Join-Path $PSScriptRoot 'verify-ue-encryption.py'
if (-not (Test-Path -LiteralPath $encryptionGate)) { throw 'The required encryption verifier is missing.' }
function Assert-EncryptedPackage([string]$Release,[string]$ReportPath) {
    & $Python $encryptionGate --release $Release --report $ReportPath
    if ($LASTEXITCODE -ne 0) { throw "Encrypted-content release gate failed. See $ReportPath" }
    $verification = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    if ($verification.status -ne 'passed') { throw "Encrypted-content verifier did not pass. See $ReportPath" }
}
$gateId = [guid]::NewGuid().ToString('N')
[IO.Directory]::CreateDirectory((Join-Path $repo 'artifacts')) | Out-Null
Assert-EncryptedPackage $SourcePackage (Join-Path $repo "artifacts\installer-source-encryption-$gateId.json")
if (-not $Engine) {
    $candidates = @('D:\Work_software\UnrealEngine\UE_5.8','D:\Work_software\UE_5.8','C:\Program Files\Epic Games\UE_5.8')
    $registration = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue
    if ($registration.InstalledDirectory) { $candidates += $registration.InstalledDirectory }
    $Engine = $candidates | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Engine\Extras\Redist\en-us\vc_redist.x64.exe') } | Select-Object -First 1
}
if (-not $Engine) { throw 'Pass -Engine with the installed UE root to include the official prerequisites and notices.' }
$vcSource = Join-Path $Engine 'Engine\Extras\Redist\en-us\vc_redist.x64.exe'
$thirdPartyLicenses = Join-Path $Engine 'Engine\Source\ThirdParty\Licenses'
if (-not (Test-Path -LiteralPath $vcSource) -or -not (Test-Path -LiteralPath $thirdPartyLicenses)) {
    throw 'The selected engine lacks its x64 VC redistributable or ThirdParty/Licenses directory.'
}
if ($Mode -in @('All','InstallerOnly')) {
    $compilerCandidates = @($Compiler,(Join-Path $repo '.tools\inno-6.7.3\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'))
    $Compiler = $compilerCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $Compiler) { throw 'Inno Setup compiler not found. See docs/WINDOWS_INSTALLER.md; PortableOnly works without it.' }
}

$runId = [guid]::NewGuid().ToString('N')
$stageParent = Join-Path $repo "artifacts\installer-staging\$runId"
$stage = Join-Path $stageParent "NeonCoast-$Version-Windows"
[IO.Directory]::CreateDirectory($stage) | Out-Null
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$excluded = [Collections.Generic.List[string]]::new()
$copied = 0
foreach ($file in Get-ChildItem -LiteralPath $SourcePackage -Recurse -File) {
    $relative = $file.FullName.Substring($SourcePackage.Length+1)
    if ($relative -match '(^|\\)(Saved|Crashes|Logs|Screenshots|DerivedDataCache|Intermediate|__pycache__|QA|Diagnostics)(\\|$)' -or
        $file.Extension -match '^\.(pdb|exp|lib|ilk|obj|log|tmp|bak|trace|sym)$' -or
        $file.Name -match '^Manifest_.*\.txt$' -or
        $file.Name -match '^(Crypto|CryptoKeys.*)\.json$|^DefaultCrypto\.ini$|Encryption.*\.ini$') {
        $excluded.Add($relative)
        continue
    }
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Package contains an unsupported reparse point: $relative" }
    $destination = Join-Path $stage $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    $copied++
}

function Copy-LicenseFile([string]$Source,[string]$RelativeDestination) {
    $destination = Join-Path $stage $RelativeDestination
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination -Force
}
Copy-LicenseFile (Join-Path $repo 'LICENSE') 'Licenses\PROJECT_LICENSE.txt'
Copy-LicenseFile (Join-Path $repo 'THIRD_PARTY_NOTICES.md') 'Licenses\THIRD_PARTY_NOTICES.md'
Copy-LicenseFile (Join-Path $repo 'docs\RELEASE_LICENSE.txt') 'Licenses\RELEASE_LICENSE.txt'
Copy-LicenseFile (Join-Path $repo 'docs\RELEASE_LICENSE.txt') 'LICENSE.txt'
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo 'docs\licenses') -Recurse -File) {
    $relative = $file.FullName.Substring((Join-Path $repo 'docs\licenses').Length+1)
    Copy-LicenseFile $file.FullName (Join-Path 'Licenses\Provenance' $relative)
}
foreach ($file in Get-ChildItem -LiteralPath $thirdPartyLicenses -Recurse -File) {
    $relative = $file.FullName.Substring($thirdPartyLicenses.Length+1)
    Copy-LicenseFile $file.FullName (Join-Path 'Licenses\UnrealThirdParty' $relative)
}
Copy-LicenseFile $vcSource 'Prerequisites\vc_redist.x64.exe'
& (Join-Path $PSScriptRoot 'installer\New-Icon.ps1') -Destination (Join-Path $stage 'NeonCoast.ico')
$startHere = @"
NEON COAST — Windows edition

双击 NeonCoast.exe 开始游戏。首次进入会显示主菜单，首次游玩默认无边框全屏。
设置 → 画面：窗口、全屏、无边框；改变显示模式后请在 15 秒内确认。
F11：游玩中切换窗口 / 无边框。菜单中请使用画面设置。
Esc / 手柄 Menu：暂停、设置、操作说明、返回主菜单、保存并退出。
首次游玩会显示新手引导；游戏中按 F2 跳过。
主菜单或暂停菜单 → 操作说明 → 重新体验新手引导，可再次练习。
设置 → 声音：先应用音量更改，再点击“试听当前音量”。主音量或音效为 0 时不播放。
鼠标与手柄分别拥有灵敏度和垂直反转设置。默认抬高视角不反转。

安装版：支持当前用户安装、自选目录、快捷方式与系统卸载。
便携版：完整解压后运行；请勿只复制 EXE 或在 ZIP 内运行。
卸载只移除发行文件，保留游戏生成的 Saved 文件夹和用户存档。
更新前也可自行备份 NeonCoast/Saved 和 %LOCALAPPDATA%/NeonCoast/Saved。

需要 Windows 10 2004 或更新的 64 位系统和支持 DirectX 12 的显卡，建议独立显卡。
无需安装 Unreal Editor。首次启动/首次着色器缓存会比后续启动更慢。
缺少 Visual C++ 运行库时，运行 Prerequisites/vc_redist.x64.exe。
该微软系统组件可能要求管理员确认；游戏本身使用当前用户权限。
如果画面设置异常，可先备份后重命名 Saved/Config/Windows/GameUserSettings.ini。

第三方素材和 Unreal Engine 保留各自许可；详见 LICENSE.txt 与 Licenses。
这是独立开发的开放世界原型，与 Rockstar Games 或 GTA VI 无关联。

Run NeonCoast.exe after extracting the entire folder. No Unreal Editor is needed.
First-time play defaults to borderless fullscreen. Settings > Video provides
windowed, fullscreen and borderless modes with a 15-second confirmation.
Escape / controller Menu opens the pause menu. F2 skips the tutorial;
Controls > Replay Tutorial starts it again. Apply volume changes before using
Settings > Audio > Preview. Muting master volume or effects also mutes the preview.
Microsoft's x64 runtime installer is included in Prerequisites if it is missing.
Uninstall keeps user-created saved games/settings. See Licenses for component terms.
"@
$controls = @"
Neon Coast · UE 版操作

首次游玩默认无边框全屏；设置 → 画面可切换窗口、全屏、无边框。
更改显示模式后请在 15 秒内确认，否则自动还原。
首次游玩有新手引导，F2 跳过；主菜单或暂停菜单 → 操作说明 → 重新体验新手引导。
设置 → 声音：先应用更改，再点击“试听当前音量”。主音量或音效为 0 时保持静音。

行为                         键盘鼠标                    手柄
步行 / 驾驶方向              WASD                        左摇杆；RT 油门、LT 刹停后倒车
视角                         鼠标                        右摇杆
上车 / 下车 / 高速跳车        F                           Y
步行跳跃 / 奔跑              空格 / 左 Shift             A / 左摇杆按下
手刹 / 车辆加速              空格 / 左 Shift             B / A
瞄准 / 射击 / 换弹            鼠标右键 / 左键 / R         LT / RT / X
任务交互 / 与行人打招呼       E                           步行方向右；车内维修 X
鸣笛 / 切换电台              H / Q                       左摇杆按下 / 方向左
车灯 / 救援复位              L / T                       方向上 / 方向下
地图 / 暂停                  M / Esc                     View / Menu
地图标记 / 清除标记           地图内鼠标左键 / Delete
操作说明                     F1；主菜单/暂停菜单也可打开
跳过当前新手引导             F2；操作说明页也可跳过
隐藏界面 / 截图              F6 / F12
窗口 / 无边框切换             游玩中 F11
画质 / 分辨率 / 音量         Esc → 设置                  Menu → 设置

出生后附近有橙色跑车和海岸任务点。按 E 接取快递工作，上车运到内陆交付点；脱离警察视线、等待搜索结束后回海岸领取报酬。南侧道路可开始限时街道竞速。地图还显示维修、医疗和探索点。

高速跳车会继承车辆动量并可能受伤；四周都被挡住时无法离车，可先移动或按 T 救援。医疗会扣除费用；角色倒下后自动恢复。游戏自动保存现金、任务进度、竞速纪录及探索奖励。

F6 隐藏界面用于构图，不提供自由飞行相机。F12 截图位于运行项目的 Saved/Screenshots 目录。鼠标与手柄的灵敏度、垂直反转可分别设置；实体手柄连接及手感仍需对应硬件确认。
"@
[IO.File]::WriteAllText((Join-Path $stage 'START_HERE.txt'),$startHere,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllText((Join-Path $stage '开始游玩.txt'),$startHere,[Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllText((Join-Path $stage '游戏操作说明.txt'),$controls,[Text.UTF8Encoding]::new($true))
$gitCommit = (& git -C $repo rev-parse HEAD 2>$null | Select-Object -First 1)
$sourceDirty = [bool](& git -C $repo status --porcelain --untracked-files=normal 2>$null | Select-Object -First 1)
$sourceFiles = @(foreach ($directory in @('Unreal\Source','Unreal\Config')) {
    Get-ChildItem -LiteralPath (Join-Path $repo $directory) -Recurse -File | Where-Object {
        $_.Name -notmatch '^(Crypto|CryptoKeys.*)\.json$|^DefaultCrypto\.ini$|Encryption.*\.ini$'
    }
})
$sourceRows = @($sourceFiles | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.FullName.Substring($repo.Length+1).Replace('\','/')
})
$sourceHasher = [Security.Cryptography.SHA256]::Create()
try {
    $sourceAggregate = [BitConverter]::ToString($sourceHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($sourceRows -join "`n")+"`n"))).Replace('-','').ToLowerInvariant()
} finally { $sourceHasher.Dispose() }
$vcVersion = (Get-Item -LiteralPath $vcSource).VersionInfo
$payload = @(Get-ChildItem -LiteralPath (Join-Path $stage 'NeonCoast\Content\Paks') -File | ForEach-Object {
    [ordered]@{file=('NeonCoast/Content/Paks/'+$_.Name); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$mainExe = Join-Path $stage 'NeonCoast\Binaries\Win64\NeonCoast.exe'
$manifest = [ordered]@{
    product='Neon Coast'; version=$Version; platform='Windows x64'; generated_utc=[DateTime]::UtcNow.ToString('o')
    source_commit=$gitCommit; source_worktree_dirty=$sourceDirty
    source_commit_note='Base HEAD before uncommitted v0.4 release changes; executable/content hashes identify the tested build'
    source_tree=[ordered]@{scope=@('Unreal/Source','Unreal/Config');excluded=@('DefaultCrypto.ini','Crypto*.json','*Encryption*.ini');algorithm='SHA-256 of sorted lowercase file SHA-256 + two spaces + repo-relative slash path + LF, UTF-8';file_count=$sourceRows.Count;sha256=$sourceAggregate}
    original_package_files_copied=$copied
    executable=[ordered]@{file='NeonCoast/Binaries/Win64/NeonCoast.exe'; bytes=(Get-Item -LiteralPath $mainExe).Length; sha256=(Get-FileHash -LiteralPath $mainExe -Algorithm SHA256).Hash.ToLowerInvariant()}
    cooked_payload=$payload
    prerequisite=[ordered]@{file='Prerequisites/vc_redist.x64.exe'; version=$vcVersion.FileVersion; sha256=(Get-FileHash -LiteralPath $vcSource -Algorithm SHA256).Hash.ToLowerInvariant()}
    exclusions=@('Saved and QA directories','debug symbols','Unreal staging manifests','logs and crash data')
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'build-manifest.json') -Encoding UTF8
$fileManifest = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.FullName.Substring($stage.Length+1).Replace('\','/')
})
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'),$fileManifest,[Text.UTF8Encoding]::new($false))
$forbidden = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
    $_.FullName.Substring($stage.Length+1) -match '(^|\\)(Saved|Crashes|Logs|QA)(\\|$)' -or $_.Extension -eq '.pdb'
})
if ($forbidden.Count) { throw 'Clean-stage verification found excluded files.' }
Assert-EncryptedPackage $stage (Join-Path $stageParent 'encryption-verification.json')

$outputs = @()
function Prepare-OutputFile([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        if (-not $Force) { throw "Output already exists: $Path. Use -Force to replace this exact release file." }
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Path)) -ne $OutputDirectory) { throw 'Refusing to replace a file outside OutputDirectory.' }
        Remove-Item -LiteralPath $Path -Force
    }
}
if ($Mode -in @('All','PortableOnly')) {
    $zip = Join-Path $OutputDirectory "NeonCoast-$Version-Windows-Portable.zip"
    Prepare-OutputFile $zip
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$true)
    $outputs += $zip
}
if ($Mode -in @('All','InstallerOnly')) {
    $setup = Join-Path $OutputDirectory "NeonCoast-$Version-Windows-Setup.exe"
    Prepare-OutputFile $setup
    & $Compiler "/DStageDir=$stage" "/DOutputDir=$OutputDirectory" "/DAppVersion=$Version" "/DVCRuntimeMinor=$($vcVersion.FileMinorPart)" "/DVCRuntimeBuild=$($vcVersion.FileBuildPart)" (Join-Path $PSScriptRoot 'installer\NeonCoast.iss')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $setup)) { throw 'Inno Setup compilation failed.' }
    $outputs += $setup
}
$outputRecords = @($outputs | ForEach-Object {
    [ordered]@{file=[IO.Path]::GetFileName($_);bytes=(Get-Item -LiteralPath $_).Length;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
})
if ($outputs.Count) {
    # Include any sibling final package already produced in a separate mode.
    $releaseFiles = @(Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object {$_.Name -match ('^NeonCoast-'+[regex]::Escape($Version)+'-Windows-(Portable\.zip|Setup\.exe)$')})
    $checksums = @($releaseFiles | Sort-Object Name | ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.Name })
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'),$checksums,[Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $stage 'build-manifest.json') -Destination (Join-Path $OutputDirectory 'build-manifest.json') -Force
}
$report = [ordered]@{run_id=$runId;mode=$Mode;version=$Version;stage=$stage;source_package=$SourcePackage;excluded_count=$excluded.Count;excluded_files=@($excluded);staged_file_count=$fileManifest.Count+1;outputs=$outputRecords;source_executable_sha256=$manifest.executable.sha256}
$reportPath = Join-Path $repo 'artifacts\windows-distribution-build.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
