param([string]$Release='', [string]$Editor='', [string]$Project='', [ValidateRange(1280,7680)][int]$Width=1600, [ValidateRange(720,4320)][int]$Height=900)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$runId=[guid]::NewGuid().ToString('N')
$started=Get-Date
$reportPath=Join-Path $repo 'artifacts/ue-onboarding-verification.json'
$runRoot=Join-Path $repo ('artifacts/onboarding-verification/'+$runId)
$userDir=Join-Path $runRoot 'user'
$process=$null
$exitCode=$null
$log=''
$runtimeReport=''
New-Item -ItemType Directory -Path $userDir -Force | Out-Null
[ordered]@{status='in_progress';run_id=$runId;started_at=$started.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
try {
    $baseArguments=@()
    if($Editor){
        $game=(Resolve-Path -LiteralPath $Editor).Path
        if(-not $Project){$Project=Join-Path $repo 'Unreal/NeonCoast.uproject'}
        $Project=(Resolve-Path -LiteralPath $Project).Path
        $baseArguments+=('"'+$Project+'"')
        $baseArguments+=@('-game','-DisablePython')
        $mode='editor executable in real game mode'
    }else{
        if(-not $Release){$Release=Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows'}
        $Release=(Resolve-Path -LiteralPath $Release).Path
        $game=Join-Path $Release 'NeonCoast/Binaries/Win64/NeonCoast.exe'
        if(-not (Test-Path -LiteralPath $game -PathType Leaf)){throw "Packaged executable missing: $game"}
        $mode='packaged Win64 game'
    }
    $exeHash=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
    $runs=@()
    foreach($runKind in @('fresh','returning')){
        $runtimeReport=Join-Path $runRoot ($runKind+'-report.json')
        $log=Join-Path $runRoot ($runKind+'.log')
        $arguments=$baseArguments+@('-NeonCoastOnboardingTest','-windowed',('-ResX='+$Width),('-ResY='+$Height),'-nosplash','-unattended',('"-UserDir='+$userDir+'"'),('"-NeonCoastOnboardingReport='+$runtimeReport+'"'),('"-abslog='+$log+'"'))
        if($runKind -eq 'returning'){$arguments+='-NeonCoastOnboardingResumeTest'}
        $process=Start-Process -FilePath $game -ArgumentList $arguments -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -PassThru
        # Materialize and retain the native handle before exit; otherwise some
        # PowerShell/.NET combinations can refresh away the exit-code handle.
        $processHandle=$process.Handle
        if(-not $process.WaitForExit(210000)){
            Stop-Process -Id $process.Id
            throw "Onboarding $runKind validation exceeded 210 seconds; see $log"
        }
        $process.Refresh()
        $exitCode=$process.ExitCode
        if($exitCode -ne 0){throw "Onboarding $runKind process exited $exitCode; see $log"}
        if(-not (Test-Path -LiteralPath $runtimeReport)){throw 'Runtime did not write its uniquely addressed report'}
        $runtime=Get-Content -LiteralPath $runtimeReport -Raw | ConvertFrom-Json
        $minimumChecks=if($runKind -eq 'fresh'){60}else{12}
        if($runtime.status -ne 'passed' -or $runtime.failures -ne 0 -or $runtime.checks -lt $minimumChecks){throw "Onboarding $runKind checks failed or did not complete; see $runtimeReport"}
        if(-not (Select-String -LiteralPath $log -Pattern 'NEON_COAST_ONBOARDING_PASS checks=[0-9]+ failures=0' -Quiet)){throw 'Runtime PASS sentinel missing'}
        if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:|NC_ONBOARDING_CHECK FAIL|NC_ONBOARDING_REPORT_WRITE_FAILED' -Quiet){throw 'Game logged an onboarding or engine failure'}
        if((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant() -ne $exeHash){throw 'Executable changed during verification'}
        if($runKind -eq 'fresh' -and ($runtime.walk_displacement_cm -lt 390 -or $runtime.drive_travel_cm -lt 1950 -or $runtime.peak_speed_kmh -le 5)){throw 'Independent movement measurements did not pass'}
        $runs+=@([ordered]@{kind=$runKind;exit_code=$exitCode;checks=$runtime.checks;failures=$runtime.failures;report=$runtimeReport;report_sha256=(Get-FileHash -LiteralPath $runtimeReport -Algorithm SHA256).Hash.ToLowerInvariant();log=$log;metrics=[ordered]@{walk_displacement_cm=$runtime.walk_displacement_cm;drive_travel_cm=$runtime.drive_travel_cm;peak_speed_kmh=$runtime.peak_speed_kmh}})
    }
    $images=@()
    foreach($kind in @('welcome','walking','complete')){
        $imagePath=Join-Path $runRoot ($kind+'.png')
        $item=Get-Item -LiteralPath $imagePath
        if($item.Length -le 10000 -or $item.LastWriteTimeUtc -lt $started.ToUniversalTime()){throw 'Screenshot is missing, empty or stale'}
        $images+=@([ordered]@{kind=$kind;path=$imagePath;bytes=$item.Length;sha256=(Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()})
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
        checks=($runs|ForEach-Object {$_.checks}|Measure-Object -Sum).Sum;failures=0;engine_failures=0
        runs=$runs;user_dir=$userDir;settings_file=$runtime.settings_file;screenshots=$images
        exit_method='Actual tagged Slate quit-confirm button in both processes; successful exit checked externally'
        scope='Fresh and separate returning processes; actual Slate controls and physical gameplay movement from viewport input; controller hardware and visual review remain separate'
    }
    $result|ConvertTo-Json -Depth 9|Set-Content -LiteralPath $reportPath -Encoding utf8
    $result|ConvertTo-Json -Depth 9
}catch{
    [ordered]@{status='failed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o);message=$_.Exception.Message;exit_code=$exitCode;log=$log;report=$runtimeReport}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
    throw
}finally{
    if($process){$process.Refresh();if(-not $process.HasExited){Stop-Process -Id $process.Id}}
}
