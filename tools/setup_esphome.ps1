[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$environment = Join-Path $root '.tools\esphome'
$python = Join-Path $environment 'Scripts\python.exe'

if (-not (Test-Path -LiteralPath $python)) {
    & py -3.12 -m venv $environment
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.12 is required to create the ESPHome environment.' }
}

& $python -m pip install --disable-pip-version-check -r (Join-Path $root 'tools\requirements-esphome.txt')
if ($LASTEXITCODE -ne 0) { throw 'ESPHome dependency installation failed.' }
Write-Host "ESPHome environment ready: $python"

