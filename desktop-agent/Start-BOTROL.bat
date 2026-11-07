@echo off
setlocal EnableExtensions
title BOTROL Desktop Agent

echo.
echo  ==========================================
echo             BOTROL DESKTOP AGENT
echo  ==========================================
echo.
echo This launcher downloads the open-source BOTROL
echo agent files from the official GitHub repository.
echo Source: https://github.com/TerminalJoint/botrol/tree/main/desktop-agent
echo.

where py >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python 3 was not found.
  echo Install Python 3.10 or newer: https://www.python.org/downloads/windows/
  echo Then run this launcher again.
  pause
  exit /b 1
)

set "BOTROL_DIR=%LOCALAPPDATA%\BOTROL"
if not exist "%BOTROL_DIR%" mkdir "%BOTROL_DIR%"
if errorlevel 1 goto failed

echo [1/4] Downloading BOTROL agent source...
powershell.exe -NoProfile -Command "$ErrorActionPreference='Stop'; $base='https://raw.githubusercontent.com/TerminalJoint/botrol/main/desktop-agent/'; $dir=Join-Path $env:LOCALAPPDATA 'BOTROL'; Invoke-WebRequest -UseBasicParsing ($base+'botrol_hotkey.py') -OutFile (Join-Path $dir 'botrol_hotkey.py'); Invoke-WebRequest -UseBasicParsing ($base+'requirements.txt') -OutFile (Join-Path $dir 'requirements.txt')"
if errorlevel 1 goto failed
if not exist "%BOTROL_DIR%\botrol_hotkey.py" goto failed
if not exist "%BOTROL_DIR%\requirements.txt" goto failed

echo.
echo [2/4] Preparing isolated Python environment...
if not exist "%BOTROL_DIR%\.venv\Scripts\python.exe" (
  py -3 -m venv "%BOTROL_DIR%\.venv"
  if errorlevel 1 goto failed
)

echo.
echo [3/4] Installing required packages...
"%BOTROL_DIR%\.venv\Scripts\python.exe" -m pip install -r "%BOTROL_DIR%\requirements.txt"
if errorlevel 1 goto failed

echo.
echo [4/4] Starting BOTROL...
echo Keep this window open while using BOTROL.
echo Press Ctrl+C here to stop it.
echo.
"%BOTROL_DIR%\.venv\Scripts\python.exe" "%BOTROL_DIR%\botrol_hotkey.py"
set "BOTROL_EXIT=%ERRORLEVEL%"
echo.
echo BOTROL has stopped (exit code %BOTROL_EXIT%).
pause
exit /b %BOTROL_EXIT%

:failed
echo.
echo [ERROR] BOTROL setup failed. Check your internet connection,
echo Python 3.10+ installation, and the official source repository.
pause
exit /b 1
