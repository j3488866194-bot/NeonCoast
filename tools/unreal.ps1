param(
    [ValidateSet('Check','Build','Prepare','Editor','Play','Test','Package')][string]$Action='Check',
    [string]$Engine='',
    [ValidateRange(1024,65535)][int]$McpPort=8008
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$project=Join-Path $repo 'Unreal/NeonCoast.uproject'
$artifacts=Join-Path $repo 'artifacts'
New-Item -ItemType Directory -Path $artifacts -Force|Out-Null
$candidates=@($Engine,'D:\Work_software\UnrealEngine\UE_5.8','D:\Work_software\UE_5.8','C:\Program Files\Epic Games\UE_5.8')
$registration=Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue
if($registration.InstalledDirectory){$candidates += $registration.InstalledDirectory}
$launcherManifest='C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
if(Test-Path -LiteralPath $launcherManifest){
    $manifest=Get-Content -LiteralPath $launcherManifest -Raw|ConvertFrom-Json
    $candidates += @($manifest.InstallationList|Where-Object {$_.AppName -eq 'UE_5.8'}|ForEach-Object {$_.InstallLocation})
}
$engineRoot=$candidates|Where-Object {$_ -and (Test-Path -LiteralPath (Join-Path $_ 'Engine/Binaries/Win64/UnrealEditor.exe'))}|Select-Object -First 1
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$compiler=$null
if(Test-Path -LiteralPath $vswhere){$compiler=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath|Select-Object -First 1)}
$status=[ordered]@{project=$project;engine=$engineRoot;compiler=$compiler;engine_available=[bool]$engineRoot;compiler_available=[bool]$compiler}
$status|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'ue-toolchain.json') -Encoding utf8
if($Action -eq 'Check'){$status|ConvertTo-Json;exit 0}
if(-not $engineRoot){throw 'UE 5.8 is not installed yet. Complete its download in Epic Games Launcher, then rerun this command.'}
if(-not $compiler){throw 'MSVC x64 build tools are missing.'}
$build=Join-Path $engineRoot 'Engine/Build/BatchFiles/Build.bat'
$editor=Join-Path $engineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
$commandlet=Join-Path $engineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$buildLog=Join-Path $artifacts 'ue-build.log'
& $build NeonCoastEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE 2>&1|Tee-Object -FilePath $buildLog
if($LASTEXITCODE -ne 0){throw "UE compilation failed. See $buildLog"}
if($Action -eq 'Build'){exit 0}
$bootstrap=Join-Path $repo 'Unreal/Content/Python/bootstrap_project.py'
$prepareLog=Join-Path $artifacts 'ue-prepare.log'
& $commandlet $project '-run=pythonscript' "-script=$bootstrap" -unattended -nop4 -nosplash -NullRHI "-abslog=$prepareLog"
if($LASTEXITCODE -ne 0){throw "UE asset generation failed. See $prepareLog"}
if(-not (Select-String -LiteralPath $prepareLog -SimpleMatch 'NEON_COAST_ASSETS_READY' -Quiet)){throw "Asset builder did not complete. See $prepareLog"}
switch($Action){
    'Prepare' {Write-Host 'Compilation and asset generation completed.'}
    'Editor' {Start-Process -FilePath $editor -ArgumentList @($project,'-ModelContextProtocolStartServer',"-ModelContextProtocolPort=$McpPort") -WorkingDirectory (Split-Path -Parent $project) -WindowStyle Normal}
    'Play' {Start-Process -FilePath $editor -ArgumentList @($project,'-game','-windowed','-ResX=1600','-ResY=900') -WorkingDirectory (Split-Path -Parent $project) -WindowStyle Normal}
    'Test' {
        $testLog=Join-Path $artifacts 'ue-smoke.log'
        & $commandlet $project -game -NeonCoastSmokeTest -unattended -nop4 -nosplash -NullRHI '-ExecCmds=t.MaxFPS 60' "-abslog=$testLog"
        if($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $testLog -SimpleMatch 'NEON_COAST_SMOKE_PASS' -Quiet)){throw "Gameplay smoke test failed. See $testLog"}
        Select-String -LiteralPath $testLog -Pattern 'NC_CHECK|NEON_COAST_SMOKE_'
    }
    'Package' {
        $uat=Join-Path $engineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
        $release=Join-Path $repo 'builds/NeonCoast-UE-Windows'
        & $uat BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$release" -utf8output 2>&1|Tee-Object -FilePath (Join-Path $artifacts 'ue-package.log')
        if($LASTEXITCODE -ne 0){throw 'UE packaging failed.'}
        Write-Host "Package: $release"
    }
}
