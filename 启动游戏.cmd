@echo off
chcp 65001 >nul
cd /d "%~dp0"
if exist "builds\NeonCoast-Windows\NeonCoast.exe" (
    start "Neon Coast" "builds\NeonCoast-Windows\NeonCoast.exe"
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run.ps1"
)
