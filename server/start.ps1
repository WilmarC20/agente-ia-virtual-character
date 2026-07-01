# Start the brain server using the project venv.

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# Load local secrets (HA token, etc.) if present - gitignored, never committed.
$secretsFile = Join-Path $PSScriptRoot "secrets.local.ps1"
if (Test-Path $secretsFile) { . $secretsFile; Write-Host "Secrets cargados (secrets.local.ps1)" }

$py = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
if (-not (Test-Path $py)) {
    Write-Host "Creating venv..."
    python -m venv .venv
    & $py -m pip install -r requirements.txt
}

$env:OMP_NUM_THREADS = "1"
$env:MKL_NUM_THREADS = "1"
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
$env:SUNO_CACHE_DIR = Join-Path $PSScriptRoot ".cache-suno"
$env:XDG_CACHE_HOME = $env:SUNO_CACHE_DIR
$env:HF_HOME = Join-Path $PSScriptRoot ".hf-cache"
$env:HUGGINGFACE_HUB_CACHE = Join-Path $env:HF_HOME "hub"
if (-not $env:RVC_INDEX_RATE) { $env:RVC_INDEX_RATE = "0.75" }
if (-not $env:RVC_RESAMPLE_SR) { $env:RVC_RESAMPLE_SR = "0" }
if (-not $env:RVC_PROTECT) { $env:RVC_PROTECT = "0.5" }
if (-not $env:RVC_RMS_MIX_RATE) { $env:RVC_RMS_MIX_RATE = "0.5" }
$env:SUNO_USE_SMALL_MODELS = if ($env:SUNO_USE_SMALL_MODELS) { $env:SUNO_USE_SMALL_MODELS } else { "True" }
New-Item -ItemType Directory -Force -Path $env:XDG_CACHE_HOME, $env:HF_HOME | Out-Null
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

if ($env:TTS_ENGINE -eq "sapi") {
    # Usuario forzo sapi (offline, voz robotica) en esta ventana.
} elseif ($env:TTS_ENGINE -eq "piper") {
    Write-Host "AVISO: TTS_ENGINE=piper muy lento en este PC. Usando sapi." -ForegroundColor Yellow
    $env:TTS_ENGINE = "sapi"
} elseif ($env:TTS_ENGINE -eq "edge") {
    # edge con fallback sapi (subproceso con timeout)
} else {
    # sapi por defecto: evita TTS fallo -11 en ESP cuando edge se cuelga
    $env:TTS_ENGINE = "sapi"
    Write-Host "TTS: sapi (offline, rapido). Voz neural: `$env:TTS_ENGINE='edge'; .\start.ps1" -ForegroundColor Cyan
}
# Si el ESP muestra TTS fallo -11: reinicia servidor; o forza sapi arriba.
$env:TTS_VOICE = if ($env:TTS_VOICE) { $env:TTS_VOICE } else { "es_MX-claude-high" }
$env:EDGE_TTS_VOICE = if ($env:EDGE_TTS_VOICE) { $env:EDGE_TTS_VOICE } else { "es-MX-DaliaNeural" }

if (-not $env:ENABLE_SINGING) { $env:ENABLE_SINGING = "0" }

# Voz RVC (TTS hablado) â€” rutas en secrets.local.ps1 (no hardcodeadas aqui).
# Requiere: APPLIO_DIR, APPLIO_PYTHON, RVC_MODEL_PATH (.pth inference ~50MB), RVC_INDEX_PATH
if (-not $env:ENABLE_TTS_RVC) {
    $env:ENABLE_TTS_RVC = if ($env:RVC_MODEL_PATH) { "1" } else { "0" }
}
if (-not $env:OLLAMA_URL) { $env:OLLAMA_URL = "http://127.0.0.1:11434" }
if (-not $env:OLLAMA_MODEL) { $env:OLLAMA_MODEL = "qwen2.5:7b" }
if (-not $env:OLLAMA_TIMEOUT_S) { $env:OLLAMA_TIMEOUT_S = "90" }
if (-not $env:OLLAMA_KEEP_ALIVE) { $env:OLLAMA_KEEP_ALIVE = "30m" }
if (-not $env:OLLAMA_NUM_PREDICT) { $env:OLLAMA_NUM_PREDICT = "220" }
if (-not $env:TTS_RVC_TIMEOUT_S) { $env:TTS_RVC_TIMEOUT_S = "180" }
if (-not $env:APPLIO_PRELOAD)         { $env:APPLIO_PRELOAD = "1" }         # Carga daemon al arrancar (evita timeout en primer /tts)
if (-not $env:APPLIO_PRELOAD_TIMEOUT_S) { $env:APPLIO_PRELOAD_TIMEOUT_S = "600" } # Hasta 10 min para cargar modelo

# Motor RVC local: Applio VoiceConverter (subprocess APPLIO_PYTHON, sin HTTP externo).
if ($env:ENABLE_TTS_RVC -eq "1") {
    if (-not $env:TTS_RVC_GUIDE) { $env:TTS_RVC_GUIDE = "edge" }
    if (-not $env:TTS_RVC_EDGE_VOICE) { $env:TTS_RVC_EDGE_VOICE = "es-MX-JorgeNeural" }
    if (-not $env:TTS_RVC_ENGINE) { $env:TTS_RVC_ENGINE = "applio" }
    if (-not $env:TTS_RVC_F0_UP_KEY) { $env:TTS_RVC_F0_UP_KEY = "0" }
    if (-not $env:TTS_RVC_INDEX_RATE) { $env:TTS_RVC_INDEX_RATE = "0.75" }
    if (-not $env:TTS_RVC_PROTECT) { $env:TTS_RVC_PROTECT = "0.33" }
    if (-not $env:TTS_SPEED_PERCENT) { $env:TTS_SPEED_PERCENT = "5" }
    if (-not $env:RVC_F0_METHOD) { $env:RVC_F0_METHOD = "rmvpe" }
    if ($env:RVC_MODEL_PATH) {
        Write-Host "TTS RVC: guia $env:TTS_RVC_GUIDE ($env:TTS_RVC_EDGE_VOICE) + Applio local" -ForegroundColor Cyan
    } else {
        Write-Host "AVISO: ENABLE_TTS_RVC=1 pero falta RVC_MODEL_PATH en secrets.local.ps1" -ForegroundColor Yellow
    }
}

$rvcPy = Join-Path $PSScriptRoot ".venv-rvc\Scripts\python.exe"
if ($env:RVC_MODEL_PATH) {
    Write-Host "RVC modelo: $(Split-Path $env:RVC_MODEL_PATH -Leaf)" -ForegroundColor Cyan
    if ($env:ENABLE_TTS_RVC -eq "1") {
        Write-Host "TTS -> RVC activo (ENABLE_TTS_RVC=1)" -ForegroundColor Cyan
    }
    if (-not (Test-Path $rvcPy)) {
        Write-Host "AVISO: falta .venv-rvc - ejecuta .\install-rvc.ps1" -ForegroundColor Yellow
    }
}
if ($env:ENABLE_TTS_RVC -eq "1") {
    if (-not $env:APPLIO_DIR -or -not (Test-Path $env:APPLIO_DIR)) {
        Write-Host "AVISO: APPLIO_DIR no configurado o no existe (secrets.local.ps1)" -ForegroundColor Yellow
    } elseif (-not $env:APPLIO_PYTHON -or -not (Test-Path $env:APPLIO_PYTHON)) {
        Write-Host "AVISO: APPLIO_PYTHON no encontrado" -ForegroundColor Yellow
    }
}

# Canto Bark+RVC (solo si ENABLE_SINGING=1)
if ($env:ENABLE_SINGING -eq "1") {
    $rvcDir = Join-Path $PSScriptRoot "rvc_models"
    if (-not $env:RVC_MODEL_PATH -and (Test-Path $rvcDir)) {
        $pth = Get-ChildItem $rvcDir -Filter *.pth -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($pth) {
            $env:RVC_MODEL_PATH = $pth.FullName
            $idx = Join-Path $rvcDir ($pth.BaseName + ".index")
            if (Test-Path $idx) { $env:RVC_INDEX_PATH = $idx }
            Write-Host "RVC model: $($pth.Name)" -ForegroundColor Cyan
        } else {
            Write-Host "RVC: sin .pth en rvc_models/ - /agent/sing devuelve 503" -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "Canto Bark desactivado (ENABLE_SINGING=0)" -ForegroundColor DarkGray
}

function Get-BrainLanIp {
    # Get-NetIPAddress puede colgar minutos con Docker/WSL/Hyper-V.
    try {
        foreach ($ni in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
            if ($ni.OperationalStatus -ne [System.Net.NetworkInformation.OperationalStatus]::Up) { continue }
            if ($ni.NetworkInterfaceType -in @(
                    [System.Net.NetworkInformation.NetworkInterfaceType]::Loopback,
                    [System.Net.NetworkInformation.NetworkInterfaceType]::Tunnel,
                    [System.Net.NetworkInformation.NetworkInterfaceType]::Ppp
                )) { continue }
            $name = $ni.Name
            if ($name -match 'vEthernet|WSL|Docker|Hyper-V|VMware|VirtualBox|TAP|Npcap') { continue }
            foreach ($ua in $ni.GetIPProperties().UnicastAddresses) {
                $ip = $ua.Address.ToString()
                if ($ip -match '^(192\.168|10\.)' -and $ip -notlike '169.254.*') {
                    return $ip
                }
            }
        }
    } catch {}
    try {
        foreach ($a in [System.Net.Dns]::GetHostAddresses($env:COMPUTERNAME)) {
            if ($a.AddressFamily -eq [System.Net.Sockets.AddressFamily]::InterNetwork) {
                $ip = $a.ToString()
                if ($ip -match '^(192\.168|10\.)') { return $ip }
            }
        }
    } catch {}
    return "192.168.0.103"
}

$bindHost = if ($env:BRAIN_BIND_HOST) { $env:BRAIN_BIND_HOST } else { "0.0.0.0" }
$lanIp = Get-BrainLanIp

Write-Host "Starting brain server on http://${bindHost}:8000 (LAN ESP -> http://${lanIp}:8000)"
Write-Host "Ollama: $env:OLLAMA_URL model=$env:OLLAMA_MODEL timeout=${env:OLLAMA_TIMEOUT_S}s keep_alive=$env:OLLAMA_KEEP_ALIVE"
Write-Host "Whisper: model=$env:WHISPER_MODEL device=$env:WHISPER_DEVICE compute=$env:WHISPER_COMPUTE_TYPE beam=$env:WHISPER_BEAM_SIZE best_of=$env:WHISPER_BEST_OF"
Write-Host "TTS activo: $env:TTS_ENGINE (fallback automatico a sapi si falla)"
if ($env:ENABLE_TTS_RVC -eq "1") {
    Write-Host "Voz robot: edge/sapi guia -> RVC Applio local"
} else {
    Write-Host 'Sin RVC: $env:ENABLE_TTS_RVC="1" + RVC_MODEL_PATH'
}
Write-Host 'Otra IP LAN:          $env:BRAIN_BIND_HOST="0.0.0.0"; .\start.ps1'
Write-Host ('Prueba health:       http://' + $lanIp + ':8000/health')
Write-Host ('Capturas audio: ' + (Join-Path $PSScriptRoot 'debug_audio\last_converse.wav'))
if ($env:ENABLE_SINGING -eq "1") {
    Write-Host ('Prueba canto RVC:  http://' + $bindHost + ':8000/sing-test') -ForegroundColor Cyan
}
if (-not $env:YTMUSIC_STREAM_OPEN_TIMEOUT) { $env:YTMUSIC_STREAM_OPEN_TIMEOUT = "240" }
if (-not $env:YTMUSIC_STREAM_URL_TIMEOUT) { $env:YTMUSIC_STREAM_URL_TIMEOUT = "120" }
if (-not $env:YTMUSIC_TRY_URL_PATH) { $env:YTMUSIC_TRY_URL_PATH = "1" }
if (-not $env:YTMUSIC_ALLOW_YTDLP_PIPE) { $env:YTMUSIC_ALLOW_YTDLP_PIPE = "1" }
if (-not $env:YTMUSIC_YTDLP_EJS) { $env:YTMUSIC_YTDLP_EJS = "" }
if (-not $env:YTMUSIC_REQUIRE_COOKIES) { $env:YTMUSIC_REQUIRE_COOKIES = "0" }
if (-not $env:YTMUSIC_LIGHT_OPEN) { $env:YTMUSIC_LIGHT_OPEN = "1" }
# pytubefix suele fallar en videos oficiales; yt-dlp download + EJS warm es más fiable.
if (-not $env:YTMUSIC_SKIP_PYTUBEFIX) { $env:YTMUSIC_SKIP_PYTUBEFIX = "1" }
if ($env:YTMUSIC_SKIP_PYTUBEFIX -eq "1") {
    # Forzar (secrets.local.ps1 puede tener WARM_EJS=0 o ESP_OPEN=60 — rompe la 1ª canción).
    $env:YTMUSIC_STREAM_START_TIMEOUT = "180"
    $env:YTMUSIC_PREFETCH_WAIT_SEC = "180"
    $env:YTMUSIC_ESP_OPEN_TIMEOUT = "180"
    $env:YTMUSIC_SKIP_EJS_WARM = "0"
    $env:YTMUSIC_WARM_EJS = "1"
    $env:YTMUSIC_PCM_PACE = "0"
} else {
    if (-not $env:YTMUSIC_ESP_OPEN_TIMEOUT) { $env:YTMUSIC_ESP_OPEN_TIMEOUT = "180" }
    if (-not $env:YTMUSIC_SKIP_EJS_WARM) { $env:YTMUSIC_SKIP_EJS_WARM = "1" }
    if (-not $env:YTMUSIC_STREAM_START_TIMEOUT) { $env:YTMUSIC_STREAM_START_TIMEOUT = "120" }
    if (-not $env:YTMUSIC_PREFETCH_WAIT_SEC) { $env:YTMUSIC_PREFETCH_WAIT_SEC = "120" }
    if ($env:YOUTUBE_API_KEY -and -not $env:YTMUSIC_WARM_EJS) { $env:YTMUSIC_WARM_EJS = "0" }
}
$ejsCheck = & $py -c "import importlib.util; print('yes' if importlib.util.find_spec('yt_dlp_ejs') else 'no')" 2>$null
$pytubefixCheck = & $py -c "import importlib.util; print('yes' if importlib.util.find_spec('pytubefix') else 'no')" 2>$null
$ytApi = if ($env:YOUTUBE_API_KEY) { 'activa' } else { 'sin-key' }
$skipPytube = $env:YTMUSIC_SKIP_PYTUBEFIX -eq "1"
Write-Host ("Musica: open=" + $env:YTMUSIC_STREAM_OPEN_TIMEOUT + "s esp_open=" + $env:YTMUSIC_ESP_OPEN_TIMEOUT + "s prefetch=" + $env:YTMUSIC_PREFETCH_WAIT_SEC + "s YouTubeAPI=" + $ytApi + " pytubefix=" + $(if ($skipPytube) { 'off' } else { $pytubefixCheck }) + " ejs=" + ($(if ($env:YTMUSIC_YTDLP_EJS) { $env:YTMUSIC_YTDLP_EJS } elseif ($ejsCheck -eq 'yes') { 'local' } else { 'github-auto' }))) -ForegroundColor DarkGray
if (-not $env:YOUTUBE_API_KEY) {
    Write-Host "AVISO: sin YOUTUBE_API_KEY — ponela en secrets.local.ps1 (YouTube Data API v3)" -ForegroundColor Yellow
} else {
    Write-Host "YouTube Data API v3: busqueda oficial activa" -ForegroundColor Cyan
}
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "AVISO: Node.js no en PATH — yt-dlp EJS limitado" -ForegroundColor Yellow
} else {
    Write-Host ("Musica YT: Node " + (Get-Command node).Source) -ForegroundColor DarkGray
}
if ($ejsCheck -ne "yes") {
    Write-Host "AVISO: primera reproduccion puede tardar ~1 min (yt-dlp descarga EJS)" -ForegroundColor Yellow
}

& $py -m uvicorn main:app --host $bindHost --port 8000 --timeout-graceful-shutdown 3 --ws-ping-interval 30 --ws-ping-timeout 120
