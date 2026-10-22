param([string]$Release='', [string]$Editor='', [string]$Project='', [ValidateRange(1280,7680)][int]$Width=1600, [ValidateRange(720,4320)][int]$Height=900)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$runId=[guid]::NewGuid().ToString('N')
$started=Get-Date
$reportPath=Join-Path $repo 'artifacts/ue-frontend-verification.json'
$runRoot=Join-Path $repo ('artifacts/frontend-verification/'+$runId)
$userDir=Join-Path $runRoot 'user'
$runtimeReport=Join-Path $runRoot 'runtime-report.json'
$log=Join-Path $runRoot 'game.log'
$process=$null
$exitCode=$null
New-Item -ItemType Directory -Path $userDir -Force | Out-Null
[ordered]@{status='in_progress';run_id=$runId;started_at=$started.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
try {
    $arguments=@()
    if($Editor){
        $game=(Resolve-Path -LiteralPath $Editor).Path
        if(-not $Project){$Project=Join-Path $repo 'Unreal/NeonCoast.uproject'}
        $Project=(Resolve-Path -LiteralPath $Project).Path
        $arguments+=('"'+$Project+'"')
        $arguments+=@('-game','-DisablePython')
        $mode='editor executable in real game mode'
    }else{
        if(-not $Release){$Release=Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows'}
        $Release=(Resolve-Path -LiteralPath $Release).Path
        $game=Join-Path $Release 'NeonCoast/Binaries/Win64/NeonCoast.exe'
        if(-not (Test-Path -LiteralPath $game -PathType Leaf)){throw "Packaged executable missing: $game"}
        $mode='packaged Win64 game'
    }
    # Keep the existing menu/resolution suite's windowed baseline explicit.
    # Automatic startup defaults are independently tested without -windowed by
    # verify-ue-startup-display.ps1; never modify the real user's configuration.
    $configPlatform=if($Editor){'WindowsEditor'}else{'Windows'}
    $configDir=Join-Path $userDir ('Saved/Config/'+$configPlatform)
    New-Item -ItemType Directory -Path $configDir -Force | Out-Null
    @"
[/Script/NeonCoast.NCGameUserSettings]
Version=5
bStartInBorderless=False
ResolutionSizeX=$Width
ResolutionSizeY=$Height
LastUserConfirmedResolutionSizeX=$Width
LastUserConfirmedResolutionSizeY=$Height
FullscreenMode=2
LastConfirmedFullscreenMode=2
PreferredFullscreenMode=1
FrameRateLimit=60.000000
bVideoChangePending=False
"@ | Set-Content -LiteralPath (Join-Path $configDir 'GameUserSettings.ini') -Encoding utf8
    $exeHash=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
    $arguments+=@('-NeonCoastMenuTest','-windowed',('-ResX='+$Width),('-ResY='+$Height),'-nosplash','-unattended',('"-UserDir='+$userDir+'"'),('"-NeonCoastMenuReport='+$runtimeReport+'"'),('"-abslog='+$log+'"'))
    $process=Start-Process -FilePath $game -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -PassThru
    $nativeProcessHandle=$process.Handle
    if(-not $process.WaitForExit(180000)){
        Stop-Process -Id $process.Id
        throw "Frontend validation exceeded 180 seconds; see $log"
    }
    $process.Refresh()
    $exitCode=$process.ExitCode
    if($exitCode -ne 0){throw "Frontend process exited $exitCode; see $log"}
    if(-not (Test-Path -LiteralPath $runtimeReport)){throw 'Runtime did not write its uniquely addressed report'}
    $runtime=Get-Content -LiteralPath $runtimeReport -Raw | ConvertFrom-Json
    if($runtime.status -ne 'passed' -or $runtime.failures -ne 0 -or $runtime.checks -lt 45){throw "Frontend checks failed or did not complete; see $runtimeReport"}
    if(-not (Select-String -LiteralPath $log -Pattern 'NEON_COAST_MENU_PASS checks=[0-9]+ failures=0' -Quiet)){throw 'Runtime PASS sentinel missing'}
    if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:|NC_MENU_CHECK FAIL|NC_MENU_REPORT_WRITE_FAILED' -Quiet){throw 'Game logged a frontend or engine failure'}
    if((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant() -ne $exeHash){throw 'Executable changed during verification'}
    $metrics=$runtime.metrics
    if($metrics.walking_normal_pitch_delta -le 1 -or $metrics.vehicle_normal_pitch_delta -le 1 -or $metrics.walking_inverted_pitch_delta -ge -1 -or $metrics.vehicle_inverted_pitch_delta -ge -1){throw 'Physical right-stick camera direction metrics failed'}
    $images=@()
    foreach($entry in @(@{kind='title';path=$runtime.title_screenshot},@{kind='settings';path=$runtime.settings_screenshot})){
        $imagePath=[IO.Path]::GetFullPath($entry.path)
        $prefix=[IO.Path]::GetFullPath($runRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
        if(-not $imagePath.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Screenshot path is outside this unique test run'}
        $item=Get-Item -LiteralPath $imagePath
        if($item.Length -le 10000 -or $item.LastWriteTimeUtc -lt $started.ToUniversalTime()){throw 'Screenshot is missing, empty or stale'}
        $images+=@([ordered]@{kind=$entry.kind;path=$imagePath;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
    $content=@()
    if(-not $Editor){
        $content=@(Get-ChildItem -LiteralPath (Join-Path $Release 'NeonCoast/Content/Paks') -File | Sort-Object Name | ForEach-Object {
            [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
    }
    $result=[ordered]@{
        status='passed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o)
        mode=$mode;executable=$game;executable_sha256=$exeHash;content_files=$content
        exit_code=$exitCode;engine_failures=0;checks=$runtime.checks;failures=$runtime.failures
        report=$runtimeReport;report_sha256=(Get-FileHash -LiteralPath $runtimeReport -Algorithm SHA256).Hash.ToLowerInvariant()
        log=$log;user_dir=$userDir;settings_file=$runtime.settings_file;screenshots=$images;metrics=$metrics
        exit_method='Actual tagged Slate quit-confirm button; successful process exit checked externally'
        scope='Native Slate keyboard/gamepad events and FSceneViewport analog injection; physical controller hardware and human visual review remain separate'
    }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
    $result | ConvertTo-Json -Depth 8
}catch{
    [ordered]@{status='failed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o);message=$_.Exception.Message;exit_code=$exitCode;log=$log;report=$runtimeReport}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
    throw
}finally{
    if($process){$process.Refresh();if(-not $process.HasExited){Stop-Process -Id $process.Id}}
}
