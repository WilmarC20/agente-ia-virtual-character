# Start the brain server using the project venv.

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$py = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
if (-not (Test-Path $py)) {
    Write-Host "Creating venv..."
    python -m venv .venv
    & $py -m pip install -r requirements.txt
}

$env:OMP_NUM_THREADS = "1"
$env:MKL_NUM_THREADS = "1"
$env:WHISPER_MODEL = "base"
if (-not $env:WHISPER_DEVICE) {
    $cudaCheck = & $py -c "import ctranslate2 as c; print('cuda' if c.get_cuda_device_count() > 0 else 'cpu')" 2>$null
    $env:WHISPER_DEVICE = if ($cudaCheck -eq 'cuda') { 'cuda' } else { 'cpu' }
}
if (-not $env:WHISPER_COMPUTE_TYPE) {
    $env:WHISPER_COMPUTE_TYPE = if ($env:WHISPER_DEVICE -eq 'cuda') { 'float16' } else { 'float32' }
}
$env:WHISPER_LANGUAGE = "es"
$env:WHISPER_BEAM_SIZE = "2"
$env:WHISPER_BEST_OF = "1"

# TTS: edge por defecto (voz neural ~2s) con fallback rapido a sapi si edge falla.
# piper queda colgado 20+ min cargando ONNX en este PC -> bloqueado.
if ($env:TTS_ENGINE -eq "sapi") {
    # Usuario forzo sapi (offline, voz robotica) en esta ventana.
} elseif ($env:TTS_ENGINE -eq "piper") {
    Write-Host "AVISO: TTS_ENGINE=piper muy lento en este PC. Usando edge." -ForegroundColor Yellow
    $env:TTS_ENGINE = "edge"
} else {
    $env:TTS_ENGINE = "edge"
}
$env:TTS_VOICE = if ($env:TTS_VOICE) { $env:TTS_VOICE } else { "es_MX-claude-high" }
$env:EDGE_TTS_VOICE = if ($env:EDGE_TTS_VOICE) { $env:EDGE_TTS_VOICE } else { "es-MX-DaliaNeural" }

# Solo red local (la placa usa 192.168.1.100). NO usar 0.0.0.0.
$bindHost = if ($env:BRAIN_BIND_HOST) { $env:BRAIN_BIND_HOST } else { "192.168.1.100" }

Write-Host "Starting brain server on http://${bindHost}:8000"
Write-Host "Whisper: model=$env:WHISPER_MODEL device=$env:WHISPER_DEVICE compute=$env:WHISPER_COMPUTE_TYPE beam=$env:WHISPER_BEAM_SIZE best_of=$env:WHISPER_BEST_OF"
Write-Host "TTS activo: $env:TTS_ENGINE (fallback automatico a sapi si falla)"
Write-Host 'Voz offline robotica: $env:TTS_ENGINE="sapi"; .\start.ps1'
Write-Host 'Otra IP LAN:          $env:BRAIN_BIND_HOST="tu.ip"; .\start.ps1'
Write-Host "Capturas audio: $PSScriptRoot\debug_audio\last_converse.wav"

& $py -m uvicorn main:app --host $bindHost --port 8000
