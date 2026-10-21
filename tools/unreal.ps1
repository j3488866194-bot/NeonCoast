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
& $build NeonCoastEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=6 2>&1|Tee-Object -FilePath $buildLog
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
    'Play' {Start-Process -FilePath $editor -ArgumentList @($project,'-game','-DisablePython','-windowed','-ResX=1600','-ResY=900') -WorkingDirectory (Split-Path -Parent $project) -WindowStyle Normal}
    'Test' {
        $testLog=Join-Path $artifacts 'ue-smoke.log'
        & $commandlet $project -game -DisablePython -NeonCoastSmokeTest -unattended -nop4 -nosplash -NullRHI '-ExecCmds=t.MaxFPS 60' "-abslog=$testLog"
        if($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $testLog -SimpleMatch 'NEON_COAST_SMOKE_PASS' -Quiet)){throw "Gameplay smoke test failed. See $testLog"}
        Select-String -LiteralPath $testLog -Pattern 'NC_CHECK FAIL|NC_METRIC|NEON_COAST_SMOKE_'
        $policeLog=Join-Path $artifacts 'ue-police.log'
        & $commandlet $project -game -DisablePython -NeonCoastPoliceTest -NeonCoastSmokeTest -unattended -nop4 -nosplash -NullRHI '-ExecCmds=t.MaxFPS 60' "-abslog=$policeLog"
        if($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $policeLog -SimpleMatch 'NEON_COAST_POLICE_PASS' -Quiet)){throw "Police integration test failed. See $policeLog"}
        Select-String -LiteralPath $policeLog -Pattern 'NC_POLICE_CHECK FAIL|NC_POLICE_METRIC|NEON_COAST_POLICE_'
        $automationLog=Join-Path $artifacts 'ue-automation.log'
        $reportDir=Join-Path $artifacts ('ue-automation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
        & $commandlet $project -DisablePython -unattended -nop4 -nosplash -NullRHI '-ExecCmds=Automation RunTests NeonCoast.Progress' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportDir" "-abslog=$automationLog"
        if($LASTEXITCODE -ne 0){throw "Progress automation failed. See $automationLog"}
        $report=Get-Content -LiteralPath (Join-Path $reportDir 'index.json') -Raw|ConvertFrom-Json
        if($report.succeeded -ne 2 -or $report.failed -ne 0 -or $report.notRun -ne 0){throw "Progress tests did not all pass. See $reportDir"}
        Write-Host "Progress automation: 2 passed; report $reportDir"
    }
    'Package' {
        $uat=Join-Path $engineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
        $release=Join-Path $repo 'builds/NeonCoast-UE-Windows'
        # .NET 10 matches the bracketed IPv6 host used by Zen literally.
        # Keep this bypass in the current build process; no machine proxy changes.
        if(($env:NO_PROXY -split ',') -notcontains '[::1]'){$env:NO_PROXY=$env:NO_PROXY+',[::1]'}
        # AppData logs can inherit EFS encryption, which prevents staging to D:.
        # UAT clears this directory, so give each run a new task-owned folder.
        $env:uebp_LogFolder=Join-Path $artifacts ('uat-package-'+[guid]::NewGuid().ToString('N'))
        $env:uebp_FinalLogFolder=$env:uebp_LogFolder
        New-Item -ItemType Directory -Path $env:uebp_LogFolder | Out-Null
        & $uat BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive "-archivedirectory=$release" '-ubtargs=-MaxParallelActions=6' -utf8output 2>&1|Tee-Object -FilePath (Join-Path $artifacts 'ue-package.log')
        if($LASTEXITCODE -ne 0){throw 'UE packaging failed.'}
        Write-Host "Package: $release"
    }
}
