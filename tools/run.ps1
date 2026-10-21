param([switch]$Editor,[switch]$Headless)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$engineCandidates = @(
    $env:GODOT_EXE,
    'D:\Work_software\Godot\4.7.2\Godot_v4.7.2-stable_win64_console.exe',
    (Join-Path $projectRoot '.tools\Godot_v4.7.2-stable_win64_console.exe')
)
$engine = $engineCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $engine) {
    $command = Get-Command godot -ErrorAction SilentlyContinue
    if ($command) { $engine = $command.Source }
}
if (-not $engine) { throw 'Godot 4.7.2 not found. Run tools/setup-engine.ps1 or set GODOT_EXE.' }
$arguments = @('--path',$projectRoot)
if ($Editor) { $arguments += '--editor' }
if ($Headless) { $arguments += '--headless' }
& $engine @arguments
exit $LASTEXITCODE
