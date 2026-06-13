$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$venvPython = Join-Path $root ".venv\Scripts\python.exe"
$pyLauncher = Get-Command py -ErrorAction SilentlyContinue

if (-not (Test-Path $venvPython)) {
  if (-not $pyLauncher) {
    throw "No se encontro el launcher 'py'. Instala Python 3.13 o crea firmware\.venv manualmente."
  }

  & py -3.13 -m venv .venv
  if ($LASTEXITCODE -ne 0) {
    throw "No se pudo crear firmware\.venv con Python 3.13."
  }
}

& $venvPython -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

& $venvPython -m pip install -r requirements.txt
exit $LASTEXITCODE
