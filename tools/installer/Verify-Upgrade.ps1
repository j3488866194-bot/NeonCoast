param(
    [string]$Installer = '',
    [string]$ExpectedManifest = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
if (-not $Installer) { $Installer = Join-Path $repo 'builds\releases\0.4.1\NeonCoast-0.4.1-Windows-Setup.exe' }
if (-not $ExpectedManifest) { $ExpectedManifest = Join-Path $repo 'builds\releases\0.4.1\build-manifest.json' }
$Installer = [IO.Path]::GetFullPath($Installer)
$ExpectedManifest = [IO.Path]::GetFullPath($ExpectedManifest)
$installDirectory = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\NeonCoast')).TrimEnd('\')
$userSaved = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'NeonCoast\Saved')).TrimEnd('\')
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{84F314BF-7489-4C27-9FB4-56724CC66CC3}_is1'
$runId = [guid]::NewGuid().ToString('N')
$runRoot = Join-Path $repo "artifacts\upgrade-verification\$runId"
$backupRoot = Join-Path $runRoot 'backup'
[IO.Directory]::CreateDirectory($backupRoot) | Out-Null
$checks = [Collections.Generic.List[object]]::new()
$protectedFiles = [Collections.Generic.List[object]]::new()
$protectedDirectories = [Collections.Generic.List[object]]::new()
$violations = [Collections.Generic.List[object]]::new()
$report = [ordered]@{
    status='in_progress'; run_id=$runId; started_utc=[DateTime]::UtcNow.ToString('o')
    installer=$Installer; expected_manifest=$ExpectedManifest; installation_directory=$installDirectory
    backup_directory=$backupRoot; install_exit_code=$null; native_process_handle_retained=$false
    verified_payload_file_count=0; protected_file_count=0; protected_directory_count=0
    checks=$checks; preservation_violations=$violations
    migration_performed=$false; uninstall_performed=$false
}
$reportPath = Join-Path $runRoot 'report.json'
$snapshotReady = $false
$installerStarted = $false
$installerProcess = $null

function Save-Report {
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $repo 'artifacts\windows-upgrade-verification.json') -Encoding UTF8
}
function Assert-Check([string]$Name,[bool]$Passed,[string]$Detail='') {
    $checks.Add([ordered]@{name=$Name;passed=$Passed;detail=$Detail})
    if (-not $Passed) { throw "Upgrade verification failed: $Name. $Detail" }
}
function Get-ContainedPath([string]$Root,[string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)|:') { throw 'Unsafe relative payload path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if (-not $path.StartsWith($Root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped its expected root.' }
    return $path
}
function Get-SafeTree([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root)) { return }
    $rootItem = Get-Item -LiteralPath $Root -Force
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Expected a real directory: $Root" }
    $stack = [Collections.Generic.Stack[string]]::new()
    $stack.Push($Root)
    while ($stack.Count -gt 0) {
        foreach ($item in Get-ChildItem -LiteralPath $stack.Pop() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing to follow a reparse point while protecting user data: $($item.FullName)" }
            if ($item.PSIsContainer) { $stack.Push($item.FullName) }
            $item
        }
    }
}
function Copy-FileBytes([string]$Source,[string]$Destination) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    $inputStream = [IO.File]::OpenRead($Source)
    try {
        $outputStream = [IO.File]::Create($Destination)
        try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
    } finally { $inputStream.Dispose() }
}
function Protect-File([IO.FileInfo]$File,[string]$Category,[string]$Relative) {
    $backup = Get-ContainedPath $backupRoot ($Category+'\'+$Relative)
    $beforeHash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-FileBytes $File.FullName $backup
    $backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash.ToLowerInvariant()
    $afterHash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($beforeHash -ne $backupHash -or $beforeHash -ne $afterHash) { throw "User file changed while it was being backed up: $Relative" }
    $protectedFiles.Add([ordered]@{
        source=$File.FullName; category=$Category; relative_path=$Relative.Replace('\','/')
        backup=$backup; sha256=$beforeHash; bytes=$File.Length
        last_write_utc=$File.LastWriteTimeUtc.ToString('o'); attributes=[int]$File.Attributes
    })
}
function Restore-AndCheckProtectedData {
    foreach ($directory in $protectedDirectories) {
        if (-not (Test-Path -LiteralPath $directory.source -PathType Container)) {
            $violations.Add([ordered]@{path=$directory.source;kind='directory_missing';restored=$false})
            [IO.Directory]::CreateDirectory($directory.source) | Out-Null
            $violations[$violations.Count-1].restored=$true
        }
    }
    foreach ($file in $protectedFiles) {
        $fileUnchanged = (Test-Path -LiteralPath $file.source -PathType Leaf) -and
            ((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash.ToLowerInvariant() -eq $file.sha256)
        if ($fileUnchanged) { continue }
        $violation = [ordered]@{path=$file.source;kind='file_changed_or_missing';restored=$false}
        $violations.Add($violation)
        if ((Get-FileHash -LiteralPath $file.backup -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'Backup checksum changed; refusing an unsafe restore.' }
        if (Test-Path -LiteralPath $file.source) {
            $existing = Get-Item -LiteralPath $file.source -Force
            if ($existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Cannot restore a protected file over a directory or reparse point.' }
            if ($existing.IsReadOnly) { $existing.IsReadOnly=$false }
        }
        Copy-FileBytes $file.backup $file.source
        [IO.File]::SetLastWriteTimeUtc($file.source,[DateTime]::Parse($file.last_write_utc).ToUniversalTime())
        $normalAttributes = [IO.FileAttributes]($file.attributes -band [int]([IO.FileAttributes]::ReadOnly -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System -bor [IO.FileAttributes]::Archive))
        [IO.File]::SetAttributes($file.source,$normalAttributes)
        $violation.restored=((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash.ToLowerInvariant() -eq $file.sha256)
        if (-not $violation.restored) { throw 'Protected file restoration did not match its backup.' }
    }
}

Save-Report
try {
    Assert-Check 'installer_and_manifest_exist' ((Test-Path -LiteralPath $Installer -PathType Leaf) -and (Test-Path -LiteralPath $ExpectedManifest -PathType Leaf))
    $expected = Get-Content -LiteralPath $ExpectedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
    $report.expected_version=$expected.version
    $report.expected_executable_sha256=$expected.executable.sha256
    $report.installer_sha256=(Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Check 'existing_current_user_registration_present' (Test-Path -LiteralPath $uninstallKey)
    $registration = Get-ItemProperty -LiteralPath $uninstallKey
    $registeredDirectory = [IO.Path]::GetFullPath($registration.InstallLocation).TrimEnd('\')
    Assert-Check 'registered_directory_is_expected_default' ($registeredDirectory.Equals($installDirectory,[StringComparison]::OrdinalIgnoreCase))
    Assert-Check 'registered_application_is_neon_coast' ($registration.DisplayName -eq 'Neon Coast')
    Assert-Check 'upgrade_does_not_downgrade' ([Version]$expected.version -ge [Version]$registration.DisplayVersion)
    $report.previous_version=$registration.DisplayVersion
    $running = @(Get-Process -Name NeonCoast -ErrorAction SilentlyContinue)
    Assert-Check 'game_is_closed_before_snapshot' ($running.Count -eq 0) 'Close Neon Coast before upgrading so its saves remain stable.'

    $oldChecksums = Join-Path $installDirectory 'SHA256SUMS.txt'
    Assert-Check 'old_payload_manifest_exists' (Test-Path -LiteralPath $oldChecksums -PathType Leaf)
    $oldPayload = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in Get-Content -LiteralPath $oldChecksums -Encoding UTF8) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Old payload checksum list is malformed; cannot classify user files safely.' }
        $oldHash = $Matches[1]
        $relative = $Matches[2].Replace('/','\')
        $null = Get-ContainedPath $installDirectory $relative
        if ($oldPayload.ContainsKey($relative)) { throw 'Duplicate path in old payload checksum list.' }
        $oldPayload.Add($relative,$oldHash)
    }
    $oldPayload.Add('SHA256SUMS.txt',(Get-FileHash -LiteralPath $oldChecksums -Algorithm SHA256).Hash.ToLowerInvariant())
    foreach ($item in Get-SafeTree $installDirectory) {
        $relative = $item.FullName.Substring($installDirectory.Length+1)
        $isSaved = $relative -match '(^|\\)Saved(\\|$)'
        if ($item.PSIsContainer) {
            if ($isSaved) {
                $backup = Get-ContainedPath $backupRoot ('installed\'+$relative)
                [IO.Directory]::CreateDirectory($backup) | Out-Null
                $protectedDirectories.Add([ordered]@{source=$item.FullName;backup=$backup})
            }
            continue
        }
        $isInstallerState = $relative -match '^unins\d+\.(exe|dat|msg)$'
        $isManaged = $oldPayload.ContainsKey($relative)
        $isUserModified = $isManaged -and ((Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $oldPayload[$relative])
        # An unchanged managed document (including 游戏操作说明.txt) is allowed
        # to receive the new release's instructions; preserve edits and extras.
        if ($isSaved -or $isUserModified -or (-not $isManaged -and -not $isInstallerState)) {
            Protect-File $item 'installed' $relative
        }
    }
    if (Test-Path -LiteralPath $userSaved) {
        $null = @(Get-SafeTree $userSaved) # Validate the tree before creating its snapshot.
        [IO.Directory]::CreateDirectory((Join-Path $backupRoot 'user-saved')) | Out-Null
        $protectedDirectories.Add([ordered]@{source=$userSaved;backup=(Join-Path $backupRoot 'user-saved')})
        foreach ($item in Get-SafeTree $userSaved) {
            $relative = $item.FullName.Substring($userSaved.Length+1)
            if ($item.PSIsContainer) {
                $backup = Get-ContainedPath $backupRoot ('user-saved\'+$relative)
                [IO.Directory]::CreateDirectory($backup) | Out-Null
                $protectedDirectories.Add([ordered]@{source=$item.FullName;backup=$backup})
            } else { Protect-File $item 'user-saved' $relative }
        }
    }
    $snapshot = [ordered]@{created_utc=[DateTime]::UtcNow.ToString('o');files=$protectedFiles;directories=$protectedDirectories}
    $snapshot | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'backup-manifest.json') -Encoding UTF8
    $report.protected_file_count=$protectedFiles.Count
    $report.protected_directory_count=$protectedDirectories.Count
    $snapshotReady=$true
    foreach ($file in $protectedFiles) {
        if ((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw 'User files changed after snapshot; installation was not started.' }
    }
    Assert-Check 'all_user_files_backed_up_and_stable' $true ([string]$protectedFiles.Count+' files')
    Save-Report

    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LANG=chinesesimp /TASKS="desktopicon" /DIR="'+$registeredDirectory+'" /LOG="'+(Join-Path $runRoot 'upgrade.log')+'"'
    $installerProcess = Start-Process -FilePath $Installer -ArgumentList $arguments -WorkingDirectory $runRoot -WindowStyle Hidden -PassThru
    $installerStarted=$true
    # Keep the native handle open before waiting; otherwise an already-exited
    # Start-Process object can lose its exit code on Windows PowerShell 5.1.
    $nativeHandle = $installerProcess.Handle
    $report.native_process_handle_retained=($nativeHandle -ne [IntPtr]::Zero)
    $installerProcess.WaitForExit()
    $report.install_exit_code=$installerProcess.ExitCode
    Restore-AndCheckProtectedData
    Assert-Check 'silent_upgrade_exits_zero' ($report.install_exit_code -eq 0)
    Assert-Check 'saved_and_extra_user_files_unchanged' ($violations.Count -eq 0) 'Any installer overwrite is restored from backup and still fails this check.'

    $actualExe = Join-Path $installDirectory 'NeonCoast\Binaries\Win64\NeonCoast.exe'
    Assert-Check 'main_executable_matches_tested_package' ((Get-FileHash -LiteralPath $actualExe -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expected.executable.sha256)
    Assert-Check 'installed_manifest_matches_expected_manifest' ((Get-FileHash -LiteralPath (Join-Path $installDirectory 'build-manifest.json') -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $ExpectedManifest -Algorithm SHA256).Hash)
    $checkedPayload = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in Get-Content -LiteralPath (Join-Path $installDirectory 'SHA256SUMS.txt') -Encoding UTF8) {
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Installed payload checksum list is malformed.' }
        $hash = $Matches[1]
        $relative = $Matches[2]
        $path = Get-ContainedPath $installDirectory $relative
        if (-not $checkedPayload.Add($relative.Replace('/','\'))) { throw 'Duplicate path in installed payload checksum list.' }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw "Installed payload checksum mismatch: $relative" }
        $report.verified_payload_file_count++
    }
    Assert-Check 'all_new_payload_checksums_match' ($report.verified_payload_file_count -gt 0) ([string]$report.verified_payload_file_count+' files')
    $registration = Get-ItemProperty -LiteralPath $uninstallKey
    Assert-Check 'registered_directory_preserved' ([IO.Path]::GetFullPath($registration.InstallLocation).TrimEnd('\').Equals($installDirectory,[StringComparison]::OrdinalIgnoreCase))
    Assert-Check 'registered_version_matches' ($registration.DisplayVersion -eq $expected.version)
    $shell = New-Object -ComObject WScript.Shell
    foreach ($link in @(
        [ordered]@{name='desktop';path=(Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Neon Coast.lnk')},
        [ordered]@{name='start_menu';path=(Join-Path ([Environment]::GetFolderPath('Programs')) 'Neon Coast\Neon Coast.lnk')}
    )) {
        Assert-Check ($link.name+'_shortcut_exists') (Test-Path -LiteralPath $link.path -PathType Leaf)
        $shortcut = $shell.CreateShortcut($link.path)
        Assert-Check ($link.name+'_shortcut_target_and_working_directory_match') (
            ([IO.Path]::GetFullPath($shortcut.TargetPath) -eq (Join-Path $installDirectory 'NeonCoast.exe')) -and
            ([IO.Path]::GetFullPath($shortcut.WorkingDirectory).TrimEnd('\') -eq $installDirectory))
    }
    Restore-AndCheckProtectedData
    Assert-Check 'final_user_data_matches_snapshot' ($violations.Count -eq 0)
    $report.status='passed'
} catch {
    $report.status='failed'
    $report.error=$_.Exception.Message
    throw
} finally {
    # Never uninstall or roll back the game during an in-place upgrade test.
    # A failed update still leaves a verified backup and restores damaged user data.
    if ($snapshotReady -and $installerStarted) {
        try {
            if ($null -ne $installerProcess -and -not $installerProcess.HasExited) { $installerProcess.WaitForExit() }
            Restore-AndCheckProtectedData
            if ($violations.Count -gt 0) { $report.status='failed' }
        } catch { $report.status='failed'; $report.preservation_error=$_.Exception.Message }
    }
    if ($null -ne $installerProcess) { $installerProcess.Dispose() }
    $report.finished_utc=[DateTime]::UtcNow.ToString('o')
    Save-Report
    [pscustomobject]@{status=$report.status;report=$reportPath;backup=$backupRoot;checks=$checks.Count;verified_payload_files=$report.verified_payload_file_count;protected_files=$protectedFiles.Count;preservation_violations=$violations.Count;install_exit=$report.install_exit_code;uninstalled=$false} | ConvertTo-Json
}
if ($report.status -ne 'passed') { throw 'Upgrade verification failed; inspect windows-upgrade-verification.json and the retained backup.' }
