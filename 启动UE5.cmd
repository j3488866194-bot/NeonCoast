@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\unreal.ps1" -Action Play
if errorlevel 1 pause
