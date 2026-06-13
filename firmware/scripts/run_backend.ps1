$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$python = Join-Path $root ".venv\Scripts\python.exe"
if (Test-Path $python) {
  & $python -m uvicorn backend.app.main:app --host 0.0.0.0 --port 8000 --reload
  exit $LASTEXITCODE
}

$pyLauncher = Get-Command py -ErrorAction SilentlyContinue
if ($pyLauncher) {
  & py -3.13 -m uvicorn backend.app.main:app --host 0.0.0.0 --port 8000 --reload
  exit $LASTEXITCODE
}

throw "No se encontro firmware\.venv ni Python 3.13. Ejecuta .\scripts\setup_backend.ps1 primero."
