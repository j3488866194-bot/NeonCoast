@echo off
cd /d "%~dp0"
if exist "%~dp0builds\NeonCoast-UE-Windows\Windows\NeonCoast.exe" (
    start "Neon Coast" "%~dp0builds\NeonCoast-UE-Windows\Windows\NeonCoast.exe"
    exit /b 0
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\unreal.ps1" -Action Play
if errorlevel 1 pause
