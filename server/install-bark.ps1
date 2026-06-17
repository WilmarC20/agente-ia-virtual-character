# Bark singing guide for RVC — installs into .venv-rvc (Python 3.11 + torch).
# RVC convierte TIMBRE; la guia debe CANTAR (Bark), no hablar (edge-TTS).

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$venv = Join-Path $PSScriptRoot ".venv-rvc"
$python = Join-Path $venv "Scripts\python.exe"
if (-not (Test-Path $python)) {
    Write-Host "Primero ejecuta .\install-rvc.ps1" -ForegroundColor Red
    exit 1
}

$pip = Join-Path $venv "Scripts\pip.exe"
$cacheRoot = Join-Path $PSScriptRoot ".cache-suno"
$hfCache = Join-Path $PSScriptRoot ".hf-cache"
New-Item -ItemType Directory -Force -Path $cacheRoot, $hfCache | Out-Null
$env:XDG_CACHE_HOME = $cacheRoot
$env:HF_HOME = $hfCache
$env:HUGGINGFACE_HUB_CACHE = Join-Path $hfCache "hub"
$env:SUNO_USE_SMALL_MODELS = "True"
# GPU por defecto; daemon mantiene modelos cargados (no recarga por cancion).
# Si falla shm.dll al arrancar: $env:BARK_CPU_ONLY = "1"

Write-Host "Bark cache (XDG_CACHE_HOME): $cacheRoot"
Write-Host "Modelos pequenos SUNO_USE_SMALL_MODELS=True (~1 GB en H:)"
Write-Host "Daemon persistente (BARK_USE_DAEMON=1): modelos en memoria, GPU por defecto"
& $pip install --no-cache-dir "numpy<2" scipy huggingface_hub
& $pip install --no-cache-dir "git+https://github.com/suno-ai/bark.git"
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "Verifying bark import..."
& $python -c "from bark import preload_models; print('bark OK')"
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host ""
Write-Host "Warm-up Bark (descarga modelos, puede tardar varios minutos)..."
$warmReq = '{"warmup":true}'
$warmReq | & $python bark_worker.py 2>&1 | Select-Object -Last 8
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host ""
Write-Host "Listo. En secrets.local.ps1 (opcional):"
Write-Host '  $env:SING_GUIDE_ENGINE = "bark"'
Write-Host '  $env:BARK_HISTORY_PROMPT = "v2/es_speaker_6"'
Write-Host '  $env:BARK_CPU_ONLY = "0"   # GPU (default); "1" si WinError shm.dll'
Write-Host "Reinicia .\start.ps1 y prueba http://192.168.0.103:8000/sing-test"
