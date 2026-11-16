param([string]$ReleaseFolder = 'release')
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$releasePath = Join-Path $PSScriptRoot $ReleaseFolder
$appPath = Join-Path $releasePath 'ArrowAssist'
$settingsPath = Join-Path $appPath 'settings.json'
$savedSettings = Join-Path $PSScriptRoot 'build\saved-settings.json'
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
if (Test-Path -LiteralPath $settingsPath) { Copy-Item -LiteralPath $settingsPath -Destination $savedSettings -Force }
python -m PyInstaller --noconfirm --onedir --windowed --name ArrowAssist --add-data 'templates;templates' --distpath $releasePath --workpath build assist.py
if ($LASTEXITCODE -ne 0) { throw 'PyInstaller failed' }
# Anaconda qt.conf can make the PyInstaller hook collect Qt 5 plugins for Qt 6.
# Always replace collected plugins using the actual PySide6 package directory.
$pluginSource = python -c "import PySide6,pathlib;print(pathlib.Path(PySide6.__file__).parent/'plugins')"
$pluginTarget = Join-Path $appPath '_internal\PySide6\plugins'
Get-ChildItem -LiteralPath $pluginSource | Copy-Item -Destination $pluginTarget -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $appPath 'README.md') -Force
if (Test-Path -LiteralPath $savedSettings) { Copy-Item -LiteralPath $savedSettings -Destination $settingsPath -Force }
$probe = Start-Process -FilePath (Join-Path $appPath 'ArrowAssist.exe') -ArgumentList '--smoke-test' -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -Wait
if ($probe.ExitCode -ne 0) { throw 'Packaged application smoke test failed' }
Write-Output "Built $appPath\ArrowAssist.exe; packaged UI smoke test passed."
