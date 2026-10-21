param([string]$Godot = 'D:\Work_software\Godot\4.7.2\Godot_v4.7.2-stable_win64_console.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'builds/NeonCoast-Windows'
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
Set-Content -LiteralPath (Join-Path (Split-Path -Parent $buildDirectory) '.gdignore') -Value '' -Encoding ascii
& $Godot --headless --path $projectRoot --editor --import --quit
if ($LASTEXITCODE -ne 0) { throw 'Asset import failed.' }
$executable = Join-Path $buildDirectory 'NeonCoast.exe'
& $Godot --headless --path $projectRoot --export-release 'Windows Desktop' $executable
if ($LASTEXITCODE -ne 0) { throw 'Windows export failed. Install official Godot 4.7.2 Windows export templates.' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $buildDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/CONTROLS.md') -Destination $buildDirectory -Force
$docsDirectory = Join-Path $buildDirectory 'docs'
New-Item -ItemType Directory -Path $docsDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/CONTROLS.md') -Destination $docsDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/VERIFICATION.md') -Destination $docsDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/TOOLCHAIN.md') -Destination $docsDirectory -Force
$imageDirectory = Join-Path $docsDirectory 'images'
New-Item -ItemType Directory -Path $imageDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/images/title.png'),(Join-Path $projectRoot 'docs/images/gameplay.png') -Destination $imageDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination $buildDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') -Destination $buildDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets/fonts/BarlowCondensed-OFL.txt') -Destination $buildDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets/fonts/NotoSansSC-OFL.txt') -Destination $buildDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/licenses/GODOT_LICENSE.txt') -Destination $buildDirectory -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/licenses/GODOT_COPYRIGHT.txt') -Destination $buildDirectory -Force
$hash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  NeonCoast.exe" | Set-Content -LiteralPath (Join-Path $buildDirectory 'SHA256SUMS.txt') -Encoding ascii
$zipPath = Join-Path $projectRoot 'builds/NeonCoast-Windows-v0.1.0.zip'
$packageItems = Get-ChildItem -LiteralPath $buildDirectory | Where-Object { $_.Name -ne 'artifacts' } | Select-Object -ExpandProperty FullName
Compress-Archive -LiteralPath $packageItems -DestinationPath $zipPath -Force
Write-Host "Built $executable"
Write-Host "SHA256 $hash"
Write-Host "Package $zipPath"
