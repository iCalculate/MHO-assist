@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
set "QT_QPA_PLATFORM=windows"
for %%V in (release_v5 release_v4 release_v3 release_v2 release) do (
  if exist "%~dp0%%V\ArrowAssist\ArrowAssist.exe" (
    start "" "%~dp0%%V\ArrowAssist\ArrowAssist.exe" %*
    exit /b
  )
)
if exist "%~dp0.venv\Scripts\python.exe" (
  "%~dp0.venv\Scripts\python.exe" -m mho_assist %*
) else (
  python -m mho_assist %*
)
if errorlevel 1 (
  echo 启动失败，请查看上方错误。需要时运行 python -m pip install -r requirements.txt
  pause
)
