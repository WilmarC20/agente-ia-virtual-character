#!/usr/bin/env pwsh
# Take de video: código en Cursor + Bender criticando en cadena.
# Latencia real: poll ESP ~2s + TTS/RVC ~3s + audio ~4-6s por frase corta.
#
# Uso:
#   .\scripts\video-take-coding-roast.ps1
#   .\scripts\video-take-coding-roast.ps1 -FirstDelayMs 3200 -LineGapMs 10000
#   .\scripts\video-take-coding-roast.ps1 -NoBeep

param(
    [int]$FirstDelayMs = 3200,
    [int]$LineGapMs = 10000,
    [switch]$NoBeep,
    [switch]$SkipApproval
)

$base = if ($env:AGENTEIA_NOTIFY_URL) { $env:AGENTEIA_NOTIFY_URL.TrimEnd('/') } else { 'http://127.0.0.1:8000' }

$ctx = @{
    source  = 'cursor'
    client  = 'Cursor'
    project = 'agenteIA'
    file    = 'server/main.py'
    tool    = 'Shell'
}

# Frases cortas = menos audio = encadenar más rápido si bajás LineGapMs
$roasts = @(
    @{ emotion = 'confused';  message = 'Otro refactor sin commit. Mis circuitos tiemblan, bolsa de carne.' }
    @{ emotion = 'angry';     message = 'Shell en firmware en vivo. ¿Querés un ESP32 ladrillo?' }
    @{ emotion = 'thinking';  message = 'Generando voz, generando bugs. Pipeline clásico de humano.' }
)

$approval = @{
    emotion = 'surprised'
    message = 'Hook interceptado: Cursor pide luz verde para esptool write_flash. ¿Autorizás, bolsa de carne?'
}

function Send-RobotLine {
    param(
        [string]$Message,
        [string]$Emotion,
        [string]$Kind = 'agent',
        [hashtable]$Context = $ctx
    )
    $body = @{
        kind       = $Kind
        speak      = $true
        priority   = $true
        emotion    = $Emotion
        message    = $Message
        context    = $Context
        dedupe_key = "roast-$(Get-Date -Format 'yyyyMMddHHmmssfff')-$(Get-Random -Maximum 99999)"
    } | ConvertTo-Json -Depth 4

    $r = Invoke-RestMethod -Uri "$base/api/dev/notify" -Method POST -ContentType 'application/json' -Body $body -TimeoutSec 15
    if ($r.skipped) {
        Write-Host "  [omitido cooldown] $($r.reason)" -ForegroundColor Yellow
    } else {
        Write-Host "  >> $($r.cmd.text)" -ForegroundColor Green
    }
    return $r
}

Write-Host ""
Write-Host "  Latencia estimada por linea: poll 2s + TTS ~3s + audio ~5s" -ForegroundColor DarkGray
Write-Host "  Gap entre envios: ${LineGapMs}ms (ajustá si se solapan o quedan huecos)" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  >>> BEEP = ENTER en Cursor Agent (codigo) AHORA <<<" -ForegroundColor Yellow
Write-Host ""

if (-not $NoBeep) { [Console]::Beep(880, 100) }
Start-Sleep -Milliseconds $FirstDelayMs

$lineNum = 0
foreach ($line in $roasts) {
    $lineNum++
    if (-not $NoBeep -and $lineNum -eq 1) { [Console]::Beep(1320, 80) }
    Write-Host "[$lineNum/$($roasts.Count + $(if ($SkipApproval) { 0 } else { 1 }))] Bender ($($line.emotion)):" -ForegroundColor Cyan
    try {
        Send-RobotLine -Message $line.message -Emotion $line.emotion | Out-Null
    } catch {
        Write-Host "  Error: $_" -ForegroundColor Red
        Write-Host "  ¿Servidor arriba? cd server; .\start.ps1" -ForegroundColor Yellow
        exit 1
    }
    if ($lineNum -lt $roasts.Count -or -not $SkipApproval) {
        Start-Sleep -Milliseconds $LineGapMs
    }
}

if (-not $SkipApproval) {
    $lineNum++
    Write-Host "[$lineNum] Bender (approval):" -ForegroundColor Cyan
    try {
        Send-RobotLine -Message $approval.message -Emotion $approval.emotion -Kind 'approval_needed' | Out-Null
    } catch {
        Write-Host "  Error: $_" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "Secuencia encolada. El ESP reproduce en orden (una voz a la vez)." -ForegroundColor Green
