param([string]$Release='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$started=Get-Date
$runId=[guid]::NewGuid().ToString('N')
$reportPath=Join-Path $repo 'artifacts/ue-release-render-verification.json'
$lastGameExitCode=$null
trap {
    $failureMessage=$_.Exception.Message
    try {
        [ordered]@{
            status='failed';run_id=$runId;started_at=$started.ToString('o');checked_at=(Get-Date -Format o)
            message=$failureMessage;executable=$game;exit_code=$lastGameExitCode;log=$log
        }|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
    } catch { Write-Warning "Could not write failed render status: $($_.Exception.Message)" }
    # A trap's break propagates the original terminating error to the caller.
    break
}
New-Item -ItemType Directory -Path (Split-Path -Parent $reportPath) -Force|Out-Null
# An interrupted/failed new attempt must never leave an old success report active.
[ordered]@{status='in_progress';run_id=$runId;started_at=$started.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath $reportPath -Encoding utf8
if(-not $Release){$Release=Join-Path $repo 'builds/NeonCoast-UE-Windows/Windows'}
$game=Join-Path $Release 'NeonCoast/Binaries/Win64/NeonCoast.exe'
if(-not (Test-Path -LiteralPath $game)){throw "Packaged game is missing: $game"}
$Release=(Resolve-Path -LiteralPath $Release).Path
$game=(Resolve-Path -LiteralPath $game).Path
$workingDirectory=Split-Path -Parent $game
$savedRoot=[IO.Path]::GetFullPath((Join-Path $Release 'NeonCoast/Saved'))
$savedPrefix=$savedRoot.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
$shotName='NCVerify_'+$runId
$log=Join-Path $repo 'artifacts/ue-release-render.log'
# Native engine CLI captures a real frame after 600 rendered frames, then exits.
# No desktop screenshots, global keyboard events or editor installation are used.
# UE 5.8 supports shot filename=<name> -nosuffix and CSV metadata. A unique name
# and run marker bind both artifacts to this process instead of a nearby editor.
# Synchronous CSV avoids the profiler background-thread teardown failure seen
# in this package's A/B check. This flag affects verification, not normal play.
$renderStartedUtc=[DateTime]::UtcNow
$process=Start-Process -FilePath $game -ArgumentList @('-SkipFrontEnd','-windowed','-ResX=1600','-ResY=900','-nosplash','-unattended','-csvCaptureFrames=720','-csvCompression=0','-csvNoProcessingThread',('-csvMetadata=NeonCoastVerificationRun='+$runId),('"-csvExecCmds=600:shot filename='+$shotName+' -nosuffix"'),'-ExitAfterCsvProfiling','"-ExecCmds=t.MaxFPS 60"',('"-abslog='+$log+'"')) -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(180000)){
    Stop-Process -Id $process.Id
    throw "Packaged render check timed out. See $log"
}
$process.Refresh()
$lastGameExitCode=$process.ExitCode
if($process.ExitCode -ne 0){throw "Packaged render check failed ($($process.ExitCode)). See $log"}
if(-not (Test-Path -LiteralPath $log)){throw 'Packaged render log missing.'}
if((Get-Item -LiteralPath $log).LastWriteTimeUtc -lt $renderStartedUtc){throw "Packaged render log was not updated by this run: $log"}
if(Select-String -LiteralPath $log -Pattern 'Ensure condition failed|Fatal error:|Assertion failed:' -Quiet){throw "Packaged rendering logged an engine failure. See $log"}
$scannedCharacters=@(Select-String -LiteralPath $log -Pattern 'NEON_COAST_CHARACTER_VISUAL.*mesh=SK_(Eric|Carla)').Count
if($scannedCharacters -lt 65 -or (Select-String -LiteralPath $log -Pattern 'NEON_COAST_CHARACTER_FALLBACK|Failed to compile Material|Default Material will be used' -Quiet)){throw "Packaged realism render fell back or a material failed. See $log"}
if(-not (Select-String -LiteralPath $log -SimpleMatch '9/9 public models loaded' -Quiet) -or -not (Select-String -LiteralPath $log -SimpleMatch '16/16 palettes use MI_CoastPBR' -Quiet)){throw "Packaged realism models/materials are missing. See $log"}
if(-not (Select-String -LiteralPath $log -SimpleMatch $runId -Quiet)){throw 'Render log does not contain this verification run marker.'}
$screens=@(Get-ChildItem -LiteralPath (Join-Path $savedRoot 'Screenshots') -Filter ($shotName+'.png') -File -Recurse | Where-Object {$_.LastWriteTimeUtc -ge $renderStartedUtc})
if($screens.Count -ne 1 -or $screens[0].Length -lt 10000){throw "This run's uniquely named packaged-game screenshot is missing or ambiguous. See $log"}
$screen=$screens[0]
if(-not $screen.FullName.StartsWith($savedPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Screenshot escaped the selected packaged Saved directory.'}
$profileRecord=Select-String -LiteralPath $log -Pattern 'Capture Ended\. Writing CSV to file\s*:\s*(.+\.csv)\s*$'|Select-Object -Last 1
if(-not $profileRecord){throw "This render log did not report a completed CSV capture. See $log"}
$recordedProfile=$profileRecord.Matches[0].Groups[1].Value.Trim().Trim('"')
$profilePath=if([IO.Path]::IsPathRooted($recordedProfile)){
    [IO.Path]::GetFullPath($recordedProfile)
}else{
    [IO.Path]::GetFullPath((Join-Path $workingDirectory $recordedProfile))
}
if(-not $profilePath.StartsWith($savedPrefix,[StringComparison]::OrdinalIgnoreCase)){throw "CSV log points outside the selected packaged Saved directory: $profilePath"}
if(-not (Test-Path -LiteralPath $profilePath -PathType Leaf)){throw "Logged CSV capture is missing: $profilePath"}
$profile=Get-Item -LiteralPath $profilePath
if($profile.LastWriteTimeUtc -lt $renderStartedUtc -or $profile.Length -le 0){throw "Logged CSV capture was not updated by this render run: $profilePath"}
if(-not (Select-String -LiteralPath $profile.FullName -SimpleMatch $runId -Quiet)){throw 'CSV metadata does not contain this verification run marker.'}
$imagePath=Join-Path $repo 'docs/images/ue/packaged-game.png'
New-Item -ItemType Directory -Path (Split-Path -Parent $imagePath) -Force|Out-Null
Copy-Item -LiteralPath $screen.FullName -Destination $imagePath -Force
$result=[ordered]@{
    status='passed'
    run_id=$runId
    started_at=$started.ToString('o')
    checked_at=(Get-Date -Format o)
    executable=$game
    executable_sha256=(Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash.ToLowerInvariant()
    content_files=@(Get-ChildItem -LiteralPath (Join-Path $Release 'NeonCoast/Content/Paks') -File | Sort-Object Name | ForEach-Object {
        [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    log=$log
    log_updated_at=(Get-Item -LiteralPath $log).LastWriteTimeUtc.ToString('o')
    screenshot=$imagePath
    screenshot_source=$screen.FullName
    screenshot_sha256=(Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
    profile=$profile.FullName
    profile_sha256=(Get-FileHash -LiteralPath $profile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    audio_ready=[bool](Select-String -LiteralPath $log -SimpleMatch 'NC_AUDIO_READY radioTracks=2 loops=4' -Quiet)
    exit_code=$process.ExitCode
    engine_failures=0
    scanned_character_instances=$scannedCharacters
    public_models_loaded=9
    pbr_palettes_loaded=16
    method='Packaged DX12 game; synchronous CSV (-csvNoProcessingThread); native frame command at frame 600; 720-frame capture; no editor or NullRHI'
    visual_inspection='Required separately; producing a PNG alone does not verify its contents'
}
$result|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $reportPath -Encoding utf8
$result|ConvertTo-Json -Depth 4
