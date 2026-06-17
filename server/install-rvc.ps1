# RVC deps for /agent/sing - separate venv (Python 3.11).
# rvc-python does NOT work on the main server venv (Python 3.14).
# Needs ~4 GB free on H: (PyTorch CUDA ~2.5 GB). pip usa TEMP en server/.pip-tmp

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$venv = Join-Path $PSScriptRoot ".venv-rvc"
$pipTmp = Join-Path $PSScriptRoot ".pip-tmp"
$pipCache = Join-Path $PSScriptRoot ".pip-cache"
New-Item -ItemType Directory -Force -Path $pipTmp, $pipCache | Out-Null
$env:TMP = $pipTmp
$env:TEMP = $pipTmp
$env:PIP_CACHE_DIR = $pipCache

if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    throw "Python launcher 'py' not found. Install Python 3.11 from python.org"
}

$freeGb = (Get-PSDrive (Split-Path $PSScriptRoot -Qualifier).TrimEnd(':')).Free / 1GB
$cFreeGb = (Get-PSDrive C).Free / 1GB
if ($freeGb -lt 4) {
    Write-Host "AVISO: quedan $([math]::Round($freeGb,1)) GB libres en H:. PyTorch CUDA necesita ~3 GB." -ForegroundColor Yellow
}
if ($cFreeGb -lt 3) {
    Write-Host "AVISO: C: tiene solo $([math]::Round($cFreeGb,1)) GB - pip usara TEMP en $pipTmp" -ForegroundColor Yellow
}

if (-not (Test-Path $venv)) {
    Write-Host "Creating RVC venv (Python 3.11) at .venv-rvc ..."
    & py -3.11 -m venv $venv
}

$pip = Join-Path $venv "Scripts\pip.exe"
$python = Join-Path $venv "Scripts\python.exe"
$fairseqWheel = "https://github.com/Sharrnah/fairseq/releases/download/v0.12.4/fairseq-0.12.4-cp311-cp311-win_amd64.whl"

Write-Host "Pin pip<24.1 (compat omegaconf)..."
& $python -m pip install "pip<24.1" wheel setuptools

Write-Host "Installing PyTorch CUDA 12.8 (RTX 5080 / sm_120, TEMP=$pipTmp)..."
& $pip install --no-cache-dir torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128
if ($LASTEXITCODE -ne 0) {
    Write-Host "cu128 failed, trying nightly cu128..." -ForegroundColor Yellow
    & $pip install --no-cache-dir --pre torch torchvision torchaudio --index-url https://download.pytorch.org/whl/nightly/cu128
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "Fallo PyTorch CUDA. Libera espacio en H: o instala CPU:" -ForegroundColor Red
    Write-Host '  .\.venv-rvc\Scripts\pip install torch torchvision torchaudio' -ForegroundColor Yellow
    exit 1
}

Write-Host "Installing fairseq prebuilt wheel (Windows)..."
& $pip install --no-cache-dir $fairseqWheel
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Installing rvc-python (sin reinstalar fairseq)..."
& $pip install --no-cache-dir --no-deps rvc-python
& $pip install --no-cache-dir faiss-cpu==1.7.3 av ffmpeg-python loguru soundfile "numpy<2" `
    librosa praat-parselmouth pyworld torchcrepe requests pydantic python-multipart
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host ""
Write-Host "Verifying rvc-python import..."
& $python -c "from rvc_python.infer import RVCInference; print('rvc-python OK')"
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host ""
Write-Host "Deep warm-up (HuBERT + rmvpe, ~30s primera vez)..."
$warmReq = '{"model_path":"rvc_models/robot.pth","index_path":"rvc_models/robot.index","device":"cuda:0","warmup":true}'
$warmReq | & $python rvc_worker.py 2>&1 | Select-Object -Last 5
if ($LASTEXITCODE -ne 0) {
    Write-Host "RVC warm-up fallo - revisa PyTorch cu128 (upgrade-rvc-torch.ps1)" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host 'Listo. Reinicia el brain server: .\start.ps1'
Write-Host 'Copia robot.pth y robot.index a .\rvc_models\'
