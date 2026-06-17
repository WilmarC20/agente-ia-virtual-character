# Upgrade .venv-rvc PyTorch to CUDA 12.8 (required for RTX 5080 / sm_120).
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$pipTmp = Join-Path $PSScriptRoot ".pip-tmp"
New-Item -ItemType Directory -Force -Path $pipTmp | Out-Null
$env:TMP = $pipTmp
$env:TEMP = $pipTmp

$pip = Join-Path $PSScriptRoot ".venv-rvc\Scripts\pip.exe"
$python = Join-Path $PSScriptRoot ".venv-rvc\Scripts\python.exe"
if (-not (Test-Path $pip)) { throw "Falta .venv-rvc. Ejecuta install-rvc.ps1 primero." }

Write-Host "Uninstalling old torch (cu124)..."
& $pip uninstall -y torch torchvision torchaudio

Write-Host "Installing PyTorch cu128 (~2.5 GB)..."
& $pip install --no-cache-dir torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128
if ($LASTEXITCODE -ne 0) {
    Write-Host "Stable cu128 failed, trying nightly..." -ForegroundColor Yellow
    & $pip install --no-cache-dir --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/cu128
}
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Verify CUDA..."
& $python -c 'import torch; print(torch.__version__, torch.version.cuda); print(torch.cuda.get_device_name(0)); print(torch.randn(1, device="cuda:0"))'
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Done. Reinicia start.ps1"
