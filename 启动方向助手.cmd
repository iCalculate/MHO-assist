@echo off
chcp 65001 >nul
cd /d "%~dp0"
set "QT_QPA_PLATFORM=windows"
if exist "%~dp0release_v5\ArrowAssist\ArrowAssist.exe" (
  start "" "%~dp0release_v5\ArrowAssist\ArrowAssist.exe"
  exit /b
)
if exist "%~dp0release_v4\ArrowAssist\ArrowAssist.exe" (
  start "" "%~dp0release_v4\ArrowAssist\ArrowAssist.exe"
  exit /b
)
if exist "%~dp0release_v3\ArrowAssist\ArrowAssist.exe" (
  start "" "%~dp0release_v3\ArrowAssist\ArrowAssist.exe"
  exit /b
)
if exist "%~dp0release_v2\ArrowAssist\ArrowAssist.exe" (
  start "" "%~dp0release_v2\ArrowAssist\ArrowAssist.exe"
  exit /b
)
if exist "%~dp0release\ArrowAssist\ArrowAssist.exe" (
  start "" "%~dp0release\ArrowAssist\ArrowAssist.exe"
  exit /b
)
if exist "D:\Anaconda\python.exe" (
  "D:\Anaconda\python.exe" assist.py
) else (
  python assist.py
)
if errorlevel 1 (
  echo 启动失败，请查看上方错误。需要时运行 python -m pip install -r requirements.txt
  pause
)
