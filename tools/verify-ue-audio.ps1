param([string]$Release='', [string]$Editor='', [string]$Project='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$runId=[guid]::NewGuid().ToString('N')
$started=Get-Date
$reportPath=Join-Path $repo 'artifacts/ue-audio-verification.json'
$runRoot=Join-Path $repo ('artifacts/audio-verification/'+$runId)
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
    $exeHash=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
    # This override applies only to the isolated test process. It avoids a false
    # silence result when the CLI test window is not the foreground application.
    $arguments+=@('-NeonCoastAudioTest','-windowed','-ResX=1280','-ResY=720','-nosplash','-unattended','-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0',('"-UserDir='+$userDir+'"'),('"-NeonCoastAudioReport='+$runtimeReport+'"'),('"-abslog='+$log+'"'))
    $process=Start-Process -FilePath $game -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -PassThru
    $null=$process.Handle # Retain the native handle before Windows PowerShell waits.
    if(-not $process.WaitForExit(120000)){
        Stop-Process -Id $process.Id
        throw "Audio validation exceeded 120 seconds; see $log"
    }
    $process.Refresh()
    $exitCode=$process.ExitCode
    if($exitCode -ne 0){throw "Audio process exited $exitCode; see $log"}
    if(-not (Test-Path -LiteralPath $runtimeReport)){throw 'Runtime did not write its uniquely addressed report'}
    $runtime=Get-Content -LiteralPath $runtimeReport -Raw | ConvertFrom-Json
    if($runtime.status -ne 'passed' -or $runtime.failures -ne 0 -or $runtime.checks -lt 40 -or @($runtime.measurements).Count -ne 11){throw "Audio checks failed or did not complete; see $runtimeReport"}
    if(-not (Select-String -LiteralPath $log -Pattern 'NEON_COAST_AUDIO_PASS checks=[0-9]+ failures=0' -Quiet)){throw 'Runtime PASS sentinel missing'}
    if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:|NC_AUDIO_CHECK FAIL|NC_AUDIO_MISSING' -Quiet){throw 'Game logged an audio or engine failure'}
    if((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant() -ne $exeHash){throw 'Executable changed during verification'}
    $waves=@()
    foreach($measurement in $runtime.measurements){
        $wavePath=[IO.Path]::GetFullPath($measurement.wave)
        $prefix=[IO.Path]::GetFullPath($runRoot).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
        if(-not $wavePath.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'PCM evidence path is outside this unique test run'}
        $item=Get-Item -LiteralPath $wavePath
        if($item.Length -le 2048 -or $item.LastWriteTimeUtc -lt $started.ToUniversalTime()){throw 'PCM evidence is missing, empty or stale'}
        $header=[IO.File]::ReadAllBytes($wavePath)
        if([Text.Encoding]::ASCII.GetString($header,0,4) -ne 'RIFF' -or [Text.Encoding]::ASCII.GetString($header,8,4) -ne 'WAVE'){throw 'Audio evidence is not a WAV file'}
        if($measurement.seconds -lt 1.5 -or $measurement.peak -ge .99){throw 'Audio duration or clipping measurement failed'}
        if($measurement.expected_silent){if($measurement.rms -ge .0000001){throw 'Requested mute did not produce silence'}}
        elseif($measurement.rms -le .0001){throw 'Expected sound produced no measurable signal'}
        $waves+=@([ordered]@{stage=$measurement.stage;path=$wavePath;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $wavePath -Algorithm SHA256).Hash.ToLowerInvariant();sample_rate=$measurement.sample_rate;channels=$measurement.channels;seconds=$measurement.seconds;rms=$measurement.rms;peak=$measurement.peak;expected_silent=$measurement.expected_silent})
    }
    $content=@()
    if(-not $Editor){
        $content=@(Get-ChildItem -LiteralPath (Join-Path $Release 'NeonCoast/Content/Paks') -File | Sort-Object Name | ForEach-Object {
            [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
    }
    $hardware=@(Select-String -LiteralPath $log -Pattern 'Using Audio Hardware Device|Initializing audio mixer using platform API|InitializeHardware succeeded|No Audio Device|Audio Device failure' | ForEach-Object {$_.Line})
    $result=[ordered]@{
        status='passed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o)
        mode=$mode;executable=$game;executable_sha256=$exeHash;content_files=$content
        exit_code=$exitCode;checks=$runtime.checks;failures=$runtime.failures
        report=$runtimeReport;report_sha256=(Get-FileHash -LiteralPath $runtimeReport -Algorithm SHA256).Hash.ToLowerInvariant()
        log=$log;user_dir=$userDir;waves=$waves;hardware_log=$hardware
        scope=$runtime.scope;background_audio='Enabled only in this isolated verification process; Windows volume and endpoint settings are not changed.'
    }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
    [ordered]@{status=$result.status;checks=$result.checks;failures=$result.failures;exit_code=$result.exit_code;scenarios=$waves.Count;report=$reportPath;runtime_report=$runtimeReport;log=$log}|ConvertTo-Json
}catch{
    [ordered]@{status='failed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o);message=$_.Exception.Message;exit_code=$exitCode;log=$log;report=$runtimeReport}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
    throw
}finally{
    if($process){$process.Refresh();if(-not $process.HasExited){Stop-Process -Id $process.Id}}
}
