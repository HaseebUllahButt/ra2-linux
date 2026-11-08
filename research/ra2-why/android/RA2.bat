@echo off
rem Red Alert 2 launcher for Android Wine runners (Winlator / Mobox)
rem Default: Yuri's Revenge. Use "RA2.bat --ra2" for the base game.
cd /d "%~dp0game"
if "%1"=="--ra2" (
  start game.exe
) else (
  start gamemd.exe
)
