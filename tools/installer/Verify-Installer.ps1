param(
    [string]$Installer = '',
    [string]$ExpectedManifest = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
if (-not $Installer) { $Installer = Join-Path $repo 'builds\releases\0.4.1\NeonCoast-0.4.1-Windows-Setup.exe' }
if (-not $ExpectedManifest) { $ExpectedManifest = Join-Path $repo 'builds\releases\0.4.1\build-manifest.json' }
$Installer = [IO.Path]::GetFullPath($Installer)
$expected = Get-Content -LiteralPath $ExpectedManifest -Raw | ConvertFrom-Json
$runId = [guid]::NewGuid().ToString('N')
$runRoot = Join-Path $repo "artifacts\installer-verification\$runId"
$installDirectory = Join-Path $runRoot 'Installed Neon Coast'
[IO.Directory]::CreateDirectory($runRoot) | Out-Null
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{84F314BF-7489-4C27-9FB4-56724CC66CC3}_is1'
$desktopLink = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Neon Coast.lnk'
$menuLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'Neon Coast\Neon Coast.lnk'
# Tests use the real AppId and shortcuts; never overwrite an existing installation.
if ((Test-Path -LiteralPath $uninstallKey) -or (Test-Path -LiteralPath $desktopLink) -or (Test-Path -LiteralPath $menuLink)) {
    throw 'An existing Neon Coast install or shortcut is present. Refusing to alter it for this test.'
}
$checks = [Collections.Generic.List[object]]::new()
$report = [ordered]@{
    status='in_progress'; run_id=$runId; started_utc=[DateTime]::UtcNow.ToString('o')
    installer=$Installer; installer_sha256=(Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
    installation_directory=$installDirectory; expected_executable_sha256=$expected.executable.sha256
    install_exit_code=$null;uninstall_exit_code=$null;verified_file_count=0;checks=$checks
}
$reportPath = Join-Path $runRoot 'report.json'
function Save-Report {
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $repo 'artifacts\windows-installer-verification.json') -Encoding UTF8
}
function Assert-Check([string]$Name,[bool]$Passed,[string]$Detail='') {
    $checks.Add([ordered]@{name=$Name;passed=$Passed;detail=$Detail})
    if (-not $Passed) { throw "Installer verification failed: $Name. $Detail" }
}
function Uninstall-TestCopy {
    $uninstaller = [IO.Path]::GetFullPath((Join-Path $installDirectory 'unins000.exe'))
    if (-not $uninstaller.StartsWith($runRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Test uninstaller path escaped its unique workspace.' }
    if (Test-Path -LiteralPath $uninstaller) {
        $args = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG="'+(Join-Path $runRoot 'uninstall.log')+'"'
        $process = Start-Process -FilePath $uninstaller -ArgumentList $args -WorkingDirectory $runRoot -WindowStyle Hidden -Wait -PassThru
        $report.uninstall_exit_code = $process.ExitCode
        for ($i=0; $i -lt 50 -and (Test-Path -LiteralPath $uninstaller); $i++) { Start-Sleep -Milliseconds 200 }
    }
}
Save-Report
try {
    $args = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LANG=chinesesimp /TASKS="desktopicon" /DIR="'+$installDirectory+'" /LOG="'+(Join-Path $runRoot 'install.log')+'"'
    $process = Start-Process -FilePath $Installer -ArgumentList $args -WorkingDirectory $runRoot -WindowStyle Hidden -Wait -PassThru
    $report.install_exit_code = $process.ExitCode
    Assert-Check 'silent_install_exits_zero' ($process.ExitCode -eq 0)
    $actualExe = Join-Path $installDirectory 'NeonCoast\Binaries\Win64\NeonCoast.exe'
    Assert-Check 'main_executable_matches_tested_package' ((Get-FileHash -LiteralPath $actualExe -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expected.executable.sha256)
    $installedManifest = Get-Content -LiteralPath (Join-Path $installDirectory 'build-manifest.json') -Raw | ConvertFrom-Json
    Assert-Check 'manifest_matches_expected_version' ($installedManifest.version -eq $expected.version)
    foreach ($entry in Get-Content -LiteralPath (Join-Path $installDirectory 'SHA256SUMS.txt') -Encoding UTF8) {
        if ($entry -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Invalid installed SHA256SUMS entry.' }
        $hash = $Matches[1]
        $relative = $Matches[2]
        $path = [IO.Path]::GetFullPath((Join-Path $installDirectory $relative))
        if (-not $path.StartsWith($installDirectory+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe relative checksum path.' }
        if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) {
            throw "Installed content hash mismatch: $relative"
        }
        $report.verified_file_count++
    }
    Assert-Check 'all_installed_payload_checksums_match' ($report.verified_file_count -gt 0) ([string]$report.verified_file_count+' files')
    Assert-Check 'desktop_shortcut_created' (Test-Path -LiteralPath $desktopLink)
    Assert-Check 'start_menu_shortcut_created' (Test-Path -LiteralPath $menuLink)
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($desktopLink)
    Assert-Check 'shortcut_targets_installed_launcher' ([IO.Path]::GetFullPath($shortcut.TargetPath) -eq (Join-Path $installDirectory 'NeonCoast.exe'))
    $registration = Get-ItemProperty -LiteralPath $uninstallKey
    Assert-Check 'uninstall_registered_for_current_user' ($registration.InstallLocation.TrimEnd('\') -eq $installDirectory)
    Assert-Check 'registered_version_matches' ($registration.DisplayVersion -eq $expected.version)
    $sentinel = Join-Path $installDirectory 'NeonCoast\Saved\SaveGames\installer-preservation-test.sav'
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($sentinel)) | Out-Null
    [IO.File]::WriteAllText($sentinel,$runId)
    Uninstall-TestCopy
    Assert-Check 'silent_uninstall_exits_zero' ($report.uninstall_exit_code -eq 0)
    Assert-Check 'installed_executable_removed' (-not (Test-Path -LiteralPath $actualExe))
    Assert-Check 'test_saved_game_preserved' ((Test-Path -LiteralPath $sentinel) -and [IO.File]::ReadAllText($sentinel) -eq $runId)
    Assert-Check 'uninstall_registration_removed' (-not (Test-Path -LiteralPath $uninstallKey))
    Assert-Check 'desktop_shortcut_removed' (-not (Test-Path -LiteralPath $desktopLink))
    Assert-Check 'start_menu_shortcut_removed' (-not (Test-Path -LiteralPath $menuLink))
    $report.status = 'passed'
} catch {
    $report.status = 'failed'
    $report.error = $_.Exception.Message
    throw
} finally {
    if ($report.status -ne 'passed') {
        try { Uninstall-TestCopy } catch { $report.cleanup_error = $_.Exception.Message }
    }
    $report.finished_utc = [DateTime]::UtcNow.ToString('o')
    Save-Report
    [pscustomobject]@{status=$report.status;report=$reportPath;checks=$checks.Count;verified_files=$report.verified_file_count;install_exit=$report.install_exit_code;uninstall_exit=$report.uninstall_exit_code} | ConvertTo-Json
}
