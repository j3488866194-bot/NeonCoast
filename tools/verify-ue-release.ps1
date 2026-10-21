param([string]$Release='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$started=Get-Date
$runId=[guid]::NewGuid().ToString('N')
$reportPath=Join-Path $repo 'artifacts/ue-release-verification.json'
$phase='startup'
$lastGameExitCode=$null
trap {
    $failureMessage=$_.Exception.Message
    try {
        [ordered]@{
            status='failed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o)
            phase=$phase;message=$failureMessage;executable=$game;exit_code=$lastGameExitCode
        }|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
    } catch { Write-Warning "Could not write failed verification status: $($_.Exception.Message)" }
    # A trap's break propagates the original terminating error to the caller.
    break
}
New-Item -ItemType Directory -Path (Split-Path -Parent $reportPath) -Force|Out-Null
# Replace a previous PASS before any executable check or new launch can fail.
[ordered]@{status='in_progress';run_id=$runId;started_at=$started.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
if(-not $Release){$Release=Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows'}
$game=Join-Path $Release 'NeonCoast/Binaries/Win64/NeonCoast.exe'
if(-not (Test-Path -LiteralPath $game)){throw "Packaged game is missing: $game"}
$log=Join-Path $repo 'artifacts/ue-release-smoke.log'
$console=Join-Path $repo 'artifacts/ue-release-smoke-console.log'
$phase='smoke'
$smokeStartedUtc=[DateTime]::UtcNow
$process=Start-Process -FilePath $game -ArgumentList @('-NeonCoastSmokeTest','-unattended','-nosplash','-NullRHI','"-ExecCmds=t.MaxFPS 60"',('"-abslog='+$log+'"')) -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -RedirectStandardOutput $console -RedirectStandardError ($console+'.stderr') -PassThru
if(-not $process.WaitForExit(180000)){
    Stop-Process -Id $process.Id
    throw "Packaged gameplay check timed out. See $log"
}
$process.Refresh()
$code=$process.ExitCode
$lastGameExitCode=$code
if(-not (Test-Path -LiteralPath $log)){throw "Packaged game did not create its log (exit $code)."}
if((Get-Item -LiteralPath $log).LastWriteTimeUtc -lt $smokeStartedUtc){throw "Packaged gameplay log was not updated by this run: $log"}
$sentinel=Select-String -LiteralPath $log -Pattern 'NEON_COAST_SMOKE_PASS checks=([0-9]+) failures=0'|Select-Object -Last 1
if($code -ne 0 -or -not $sentinel){throw "Packaged gameplay check failed ($code). See $log"}
if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:' -Quiet){throw "Packaged gameplay logged an engine failure. See $log"}
$characters=@(Select-String -LiteralPath $log -SimpleMatch 'NEON_COAST_CHARACTER_VISUAL').Count
if($characters -lt 65 -or (Select-String -LiteralPath $log -SimpleMatch 'NEON_COAST_CHARACTER_FALLBACK' -Quiet)){throw "Packaged skeletal characters did not load for every actor. See $log"}
$policeLog=Join-Path $repo 'artifacts/ue-release-police.log'
$policeConsole=Join-Path $repo 'artifacts/ue-release-police-console.log'
$phase='police'
$lastGameExitCode=$null
$policeStartedUtc=[DateTime]::UtcNow
$process=Start-Process -FilePath $game -ArgumentList @('-NeonCoastPoliceTest','-NeonCoastSmokeTest','-unattended','-nosplash','-NullRHI','"-ExecCmds=t.MaxFPS 60"',('"-abslog='+$policeLog+'"')) -WorkingDirectory (Split-Path -Parent $game) -WindowStyle Hidden -RedirectStandardOutput $policeConsole -RedirectStandardError ($policeConsole+'.stderr') -PassThru
if(-not $process.WaitForExit(180000)){
    Stop-Process -Id $process.Id
    throw "Packaged police check timed out. See $policeLog"
}
$process.Refresh()
$lastGameExitCode=$process.ExitCode
if(-not (Test-Path -LiteralPath $policeLog)){throw "Packaged police check did not create its log (exit $($process.ExitCode))."}
if((Get-Item -LiteralPath $policeLog).LastWriteTimeUtc -lt $policeStartedUtc){throw "Packaged police log was not updated by this run: $policeLog"}
$policeSentinel=Select-String -LiteralPath $policeLog -Pattern 'NEON_COAST_POLICE_PASS checks=([0-9]+) failures=0'|Select-Object -Last 1
if($process.ExitCode -ne 0 -or -not $policeSentinel){throw "Packaged police check failed. See $policeLog"}
if(Select-String -LiteralPath $policeLog -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:' -Quiet){throw "Packaged police logged an engine failure. See $policeLog"}
$result=[ordered]@{
    status='passed'
    run_id=$runId
    started_at=$started.ToString('o')
    checked_at=(Get-Date -Format o)
    executable=(Resolve-Path -LiteralPath $game).Path
    executable_bytes=(Get-Item -LiteralPath $game).Length
    sha256=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
    content_files=@(Get-ChildItem -LiteralPath (Join-Path $Release 'NeonCoast/Content/Paks') -File | Sort-Object Name | ForEach-Object {
        [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    checks=[int]$sentinel.Matches[0].Groups[1].Value
    police_checks=[int]$policeSentinel.Matches[0].Groups[1].Value
    total_checks=([int]$sentinel.Matches[0].Groups[1].Value+[int]$policeSentinel.Matches[0].Groups[1].Value)
    skeletal_characters=$characters
    failures=0
    engine_failures=0
    log=$log
    log_updated_at=(Get-Item -LiteralPath $log).LastWriteTimeUtc.ToString('o')
    police_log=$policeLog
    police_log_updated_at=(Get-Item -LiteralPath $policeLog).LastWriteTimeUtc.ToString('o')
    mode='packaged Win64 Development; real world ticks with NullRHI; player saves disabled'
    metrics=@(Select-String -LiteralPath $log -Pattern 'NC_METRIC'|ForEach-Object {$_.Line})
    police_metrics=@(Select-String -LiteralPath $policeLog -Pattern 'NC_POLICE_METRIC'|ForEach-Object {$_.Line})
}
$result|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
$result|ConvertTo-Json -Depth 4
