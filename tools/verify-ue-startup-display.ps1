param([string]$Release='', [string]$Editor='', [string]$Project='', [ValidateSet('fresh','legacy','opt_out','recovery','override')][string[]]$Cases=@('fresh','legacy','opt_out','recovery','override'))
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$runId=[guid]::NewGuid().ToString('N')
$started=Get-Date
$reportPath=Join-Path $repo 'artifacts/ue-startup-display-verification.json'
$runRoot=Join-Path $repo ('artifacts/startup-display-verification/'+$runId)
$process=$null
$caseReports=@()
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
[ordered]@{status='in_progress';run_id=$runId;started_at=$started.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
try {
    $baseArguments=@()
    if($Editor){
        $game=(Resolve-Path -LiteralPath $Editor).Path
        if(-not $Project){$Project=Join-Path $repo 'Unreal/NeonCoast.uproject'}
        $Project=(Resolve-Path -LiteralPath $Project).Path
        $baseArguments+=@(('"'+$Project+'"'),'-game','-DisablePython')
        $platform='WindowsEditor'
        $mode='editor executable in real game mode'
    }else{
        if(-not $Release){$Release=Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows'}
        $Release=(Resolve-Path -LiteralPath $Release).Path
        $game=Join-Path $Release 'NeonCoast/Binaries/Win64/NeonCoast.exe'
        if(-not (Test-Path -LiteralPath $game -PathType Leaf)){throw "Packaged executable missing: $game"}
        $platform='Windows'
        $mode='packaged Win64 game'
    }
    $exeHash=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
    foreach($caseName in $Cases){
        $caseRoot=Join-Path $runRoot $caseName
        $userDir=Join-Path $caseRoot 'user'
        $configDir=Join-Path $userDir ('Saved/Config/'+$platform)
        New-Item -ItemType Directory -Path $configDir -Force | Out-Null
        $settingsFile=Join-Path $configDir 'GameUserSettings.ini'
        $fixtureHash=$null
        if($caseName -ne 'fresh'){
            $pending=if($caseName -eq 'recovery'){'True'}else{'False'}
            $width=if($caseName -eq 'recovery'){3840}else{1280}
            $height=if($caseName -eq 'recovery'){2160}else{720}
            $windowMode=if($caseName -eq 'recovery'){0}else{2}
            $option=if($caseName -eq 'opt_out'){'bStartInBorderless=False'}elseif($caseName -eq 'recovery'){'bStartInBorderless=True'}else{''}
            @"
[/Script/NeonCoast.NCGameUserSettings]
Version=5
ResolutionSizeX=$width
ResolutionSizeY=$height
FullscreenMode=$windowMode
LastUserConfirmedResolutionSizeX=1280
LastUserConfirmedResolutionSizeY=720
LastConfirmedFullscreenMode=2
PreferredFullscreenMode=1
FrameRateLimit=60.000000
bUseVSync=False
bVideoChangePending=$pending
$option
MouseSensitivity=1.200000
GamepadSensitivity=1.350000
bInvertMouseY=False
bInvertGamepadY=True
GamepadDeadZone=0.170000
MasterVolume=0.730000
MusicVolume=0.410000
EffectsVolume=0.620000
"@ | Set-Content -LiteralPath $settingsFile -Encoding utf8
            $fixtureHash=(Get-FileHash -LiteralPath $settingsFile -Algorithm SHA256).Hash.ToLowerInvariant()
            Copy-Item -LiteralPath $settingsFile -Destination (Join-Path $caseRoot 'fixture-before.ini')
        }
        $runtimeReport=Join-Path $caseRoot 'runtime-report.json'
        $log=Join-Path $caseRoot 'game.log'
        $arguments=$baseArguments+@('-NeonCoastStartupDisplayTest',('-NeonCoastStartupDisplayCase='+$caseName),'-nosplash','-unattended',('"-UserDir='+$userDir+'"'),('"-NeonCoastStartupDisplayReport='+$runtimeReport+'"'),('"-abslog='+$log+'"'))
        if($caseName -eq 'override'){$arguments+=@('-windowed','-ResX=1280','-ResY=720')}
        $process=Start-Process -FilePath $game -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -PassThru
        if(-not $process.WaitForExit(90000)){
            Stop-Process -Id $process.Id
            throw "Startup display case $caseName exceeded 90 seconds; see $log"
        }
        $process.Refresh()
        if($process.ExitCode -ne 0){throw "Startup display case $caseName exited $($process.ExitCode); see $log"}
        if(-not (Test-Path -LiteralPath $runtimeReport)){throw "Startup display case $caseName did not write a report"}
        $runtime=Get-Content -LiteralPath $runtimeReport -Raw | ConvertFrom-Json
        if($runtime.status -ne 'passed' -or $runtime.failures -ne 0 -or $runtime.checks -lt 14 -or $runtime.case -ne $caseName){throw "Startup display checks failed for $caseName; see $runtimeReport"}
        if(-not (Select-String -LiteralPath $log -Pattern 'NEON_COAST_STARTUP_DISPLAY_PASS checks=[0-9]+ failures=0' -Quiet)){throw "Runtime PASS sentinel missing for $caseName"}
        if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:|NC_STARTUP_DISPLAY_CHECK FAIL|NC_STARTUP_DISPLAY_REPORT_WRITE_FAILED' -Quiet){throw "Engine or startup display error in $log"}
        $expectedConfig=[IO.Path]::GetFullPath($settingsFile)
        if([IO.Path]::GetFullPath($runtime.settings_file) -ne $expectedConfig){throw "Runtime did not use this case's isolated settings file"}
        $imagePath=[IO.Path]::GetFullPath($runtime.screenshot)
        $prefix=[IO.Path]::GetFullPath($caseRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
        if(-not $imagePath.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Screenshot escaped isolated case directory"}
        $image=Get-Item -LiteralPath $imagePath
        if($image.Length -le 10000 -or $image.LastWriteTimeUtc -lt $started.ToUniversalTime()){throw 'Startup screenshot is stale or invalid'}
        $caseReports+=@([ordered]@{
            case=$caseName;checks=$runtime.checks;failures=$runtime.failures;exit_code=$process.ExitCode
            command_line_display_override=($caseName -eq 'override');metrics=$runtime.metrics
            report=$runtimeReport;report_sha256=(Get-FileHash -LiteralPath $runtimeReport -Algorithm SHA256).Hash.ToLowerInvariant()
            user_dir=$userDir;settings_file=$expectedConfig;fixture_sha256=$fixtureHash;log=$log
            screenshot=[ordered]@{path=$imagePath;bytes=$image.Length;sha256=(Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
        Write-Output ("Startup display case {0}: {1} checks passed; viewport {2}x{3}, mode {4}" -f $caseName,$runtime.checks,$runtime.metrics.viewport_width,$runtime.metrics.viewport_height,$runtime.metrics.viewport_mode)
        $process=$null
    }
    if((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant() -ne $exeHash){throw 'Executable changed during verification'}
    $content=@()
    if(-not $Editor){
        $content=@(Get-ChildItem -LiteralPath (Join-Path $Release 'NeonCoast/Content/Paks') -File | Sort-Object Name | ForEach-Object {
            [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
    }
    $result=[ordered]@{
        status='passed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o)
        mode=$mode;executable=$game;executable_sha256=$exeHash;content_files=$content
        checks=($caseReports|ForEach-Object {$_.checks}|Measure-Object -Sum).Sum;failures=0;cases=$caseReports
        scope='Real rendered viewport and native-window mode/size; fresh launch, v0.4 legacy settings, opt-out, safe recovery, and explicit command-line overrides; unique UserDir per case'
    }
    $result|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $reportPath -Encoding utf8
    $result|ConvertTo-Json -Depth 9
}catch{
    [ordered]@{status='failed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o);message=$_.Exception.Message;completed_cases=$caseReports}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $reportPath -Encoding utf8
    throw
}finally{
    if($process){$process.Refresh();if(-not $process.HasExited){Stop-Process -Id $process.Id}}
}
