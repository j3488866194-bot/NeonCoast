param([string]$Godot = 'D:\Work_software\Godot\4.7.2\Godot_v4.7.2-stable_win64_console.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$artifactDirectory = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
foreach ($test in @('actors_smoke','save_smoke','missions_smoke','game_smoke','map_smoke','ui_smoke')) {
    $scriptPath = Join-Path $projectRoot "tests/$test.gd"
    if (-not (Test-Path -LiteralPath $scriptPath)) { throw "Missing required test $scriptPath" }
    $output = & $Godot --headless --path $projectRoot --fixed-fps 120 --script "res://tests/$test.gd" -- --test-mode 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $artifactDirectory "$test.log") -Encoding utf8
    $output | Write-Output
    if ($exitCode -ne 0 -or ($output -match 'SCRIPT ERROR:|Parse Error:|FAIL:')) { throw "Verification failed: $test" }
}
Write-Host 'All required tests passed.'
