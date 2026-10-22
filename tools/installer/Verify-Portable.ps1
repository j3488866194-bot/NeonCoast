param([string]$Archive = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
if (-not $Archive) { $Archive = Join-Path $repo 'builds\releases\0.4.0\NeonCoast-0.4.0-Windows-Portable.zip' }
$Archive = [IO.Path]::GetFullPath($Archive)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($Archive)
$report = [ordered]@{status='in_progress';archive=$Archive;archive_sha256=(Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant();verified_file_count=0;started_utc=[DateTime]::UtcNow.ToString('o')}
$reportPath = Join-Path $repo 'artifacts\windows-portable-verification.json'
try {
    # .NET Framework on Windows can write backslashes in ZIP entry names.
    $checksumEntries = @($zip.Entries | Where-Object {$_.FullName.Replace('\','/') -match '^[^/]+/SHA256SUMS\.txt$'})
    if ($checksumEntries.Count -ne 1) { throw 'Portable archive must have one root SHA256SUMS.txt.' }
    $checksumEntry = $checksumEntries[0]
    $baseDirectory = $checksumEntry.FullName.Replace('\','/').Substring(0,$checksumEntry.FullName.Length-'SHA256SUMS.txt'.Length)
    $stream = $checksumEntry.Open()
    $reader = [IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
    try { $checksumText = $reader.ReadToEnd() } finally { $reader.Dispose(); $stream.Dispose() }
    $entries = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $zip.Entries) {
        $entryName = $entry.FullName.Replace('\','/')
        if ($entryName.EndsWith('/')) { continue }
        if (-not $entryName.StartsWith($baseDirectory,[StringComparison]::Ordinal)) { throw 'Unexpected archive root.' }
        $relative = $entryName.Substring($baseDirectory.Length)
        if ($relative -match '(^|/)\.\.(/|$)|(^|/)(Saved|Crashes|Logs|QA)(/|$)|\.pdb$|(^|/)(Crypto|CryptoKeys.*)\.json$|(^|/)DefaultCrypto\.ini$|Encryption.*\.ini$') {
            throw "Forbidden portable payload path: $relative"
        }
        $entries.Add($relative,$entry)
    }
    foreach ($line in ($checksumText -split '\r?\n')) {
        if (-not $line) { continue }
        if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Malformed portable SHA256SUMS entry.' }
        $expectedHash = $Matches[1]
        $relative = $Matches[2]
        if (-not $entries.ContainsKey($relative)) { throw "Missing portable file: $relative" }
        $stream = $entries[$relative].Open()
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $actualHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
        finally { $stream.Dispose(); $hasher.Dispose() }
        if ($actualHash -ne $expectedHash) { throw "Portable content checksum mismatch: $relative" }
        if ($relative -eq 'NeonCoast/Binaries/Win64/NeonCoast.exe') { $report.executable_sha256=$actualHash }
        $report.verified_file_count++
    }
    if ($report.verified_file_count -ne $entries.Count-1) { throw 'Archive has unchecked files outside its checksum list.' }
    $manifestStream = $entries['build-manifest.json'].Open()
    $manifestReader = [IO.StreamReader]::new($manifestStream,[Text.Encoding]::UTF8)
    try { $manifest = $manifestReader.ReadToEnd() | ConvertFrom-Json }
    finally { $manifestReader.Dispose(); $manifestStream.Dispose() }
    if ($report.executable_sha256 -ne $manifest.executable.sha256) { throw 'Portable EXE does not match its build manifest.' }
    $report.status='passed'
} catch { $report.status='failed'; $report.error=$_.Exception.Message; throw }
finally {
    $zip.Dispose()
    $report.finished_utc=[DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    $report | ConvertTo-Json -Depth 4
}
