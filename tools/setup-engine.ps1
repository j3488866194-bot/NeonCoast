param([string]$InstallDirectory = 'D:\Work_software\Godot\4.7.2',[switch]$WithExportTemplates)
$ErrorActionPreference = 'Stop'
$version = '4.7.2-stable'
$archiveName = "Godot_v${version}_win64.exe.zip"
$source = "https://github.com/godotengine/godot-builds/releases/download/$version"
New-Item -ItemType Directory -Path $InstallDirectory -Force | Out-Null
$archivePath = Join-Path $InstallDirectory $archiveName
$checksumPath = Join-Path $InstallDirectory 'SHA512-SUMS.txt'
Invoke-WebRequest "$source/$archiveName" -OutFile $archivePath
Invoke-WebRequest "$source/SHA512-SUMS.txt" -OutFile $checksumPath
$entry = Get-Content -LiteralPath $checksumPath | Where-Object { $_.EndsWith(" $archiveName") }
if (-not $entry) { throw 'Archive is missing from official checksum manifest.' }
$expected = ($entry -split '\s+')[0]
$actual = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA512).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw 'SHA-512 mismatch; archive was not extracted.' }
Expand-Archive -LiteralPath $archivePath -DestinationPath $InstallDirectory -Force
Set-Content -LiteralPath (Join-Path $InstallDirectory '_sc_') -Value '' -Encoding ascii
if ($WithExportTemplates) {
    $templateName = "Godot_v${version}_export_templates.tpz"
    $templateArchive = Join-Path $InstallDirectory $templateName
    $templateEntry = Get-Content -LiteralPath $checksumPath | Where-Object { $_.EndsWith(" $templateName") }
    if (-not $templateEntry) { throw 'Export templates are missing from the official checksum manifest.' }
    $templateExpected = ($templateEntry -split '\s+')[0]
    if (-not (Test-Path -LiteralPath $templateArchive) -or (Get-FileHash -LiteralPath $templateArchive -Algorithm SHA512).Hash.ToLowerInvariant() -ne $templateExpected) {
        Invoke-WebRequest "$source/$templateName" -OutFile $templateArchive
    }
    if ((Get-FileHash -LiteralPath $templateArchive -Algorithm SHA512).Hash.ToLowerInvariant() -ne $templateExpected) { throw 'Export template SHA-512 mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $templateDirectory = Join-Path $InstallDirectory 'editor_data/export_templates/4.7.2.stable'
    New-Item -ItemType Directory -Path $templateDirectory -Force | Out-Null
    $zip = [System.IO.Compression.ZipFile]::OpenRead($templateArchive)
    try {
        foreach ($filename in @('windows_release_x86_64.exe','windows_debug_x86_64.exe','version.txt')) {
            $entry = $zip.GetEntry("templates/$filename")
            if (-not $entry) { throw "Missing required template: $filename" }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry,(Join-Path $templateDirectory $filename),$true)
        }
    } finally { $zip.Dispose() }
    Write-Host "Verified Windows export templates installed at $templateDirectory"
}
& (Join-Path $InstallDirectory 'Godot_v4.7.2-stable_win64_console.exe') --version
Write-Host "Verified engine installed at $InstallDirectory"
