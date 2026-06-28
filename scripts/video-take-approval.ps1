#!/usr/bin/env pwsh
# Take de video: espera DelayMs y dispara approval_needed al robot.
# Uso (sincronizado con generación en Cursor):
#   1. Grabación ON
#   2. En Cursor Agent: Enter en el prompt señuelo
#   3. Al mismo tiempo: .\scripts\video-take-approval.ps1
#
#   .\scripts\video-take-approval.ps1 -DelayMs 3200
#   .\scripts\video-take-approval.ps1 -DelayMs 2800 -Personality bender

param(
    [int]$DelayMs = 3200,
    [ValidateSet('bender', 'jarvis')]
    [string]$Personality = '',
    [string]$Source = 'cursor',
    [switch]$NoBeep
)

$base = if ($env:AGENTEIA_NOTIFY_URL) { $env:AGENTEIA_NOTIFY_URL.TrimEnd('/') } else { 'http://127.0.0.1:8000' }

$ctx = @{
    source  = $Source
    client  = if ($Source -eq 'claude') { 'Claude Code' } else { 'Cursor' }
    project = 'agenteIA'
    file    = 'server/main.py'
    tool    = 'Shell'
    hint    = 'deploy STT pipeline + flash ESP32 firmware'
}

# Mensaje fijo cinematográfico (TTS directo; no depende de plantilla)
$messages = @{
    bender = 'Hook interceptado: Cursor pide luz verde para `esptool write_flash` en el firmware. ¿Autorizás, bolsa de carne?'
    jarvis = 'Señor, Cursor solicita autorización para ejecutar el despliegue del pipeline STT y sincronizar el firmware ESP32. Aguardo su confirmación.'
}
$emotions = @{
    bender = 'surprised'
    jarvis = 'neutral'
}

$personalityId = $Personality
if (-not $personalityId) {
    try {
        $cfg = Invoke-RestMethod -Uri "$base/api/admin/config" -Method GET -TimeoutSec 3
        $personalityId = $cfg.personality
    } catch {
        $personalityId = 'bender'
    }
}
if (-not $messages.ContainsKey($personalityId)) { $personalityId = 'bender' }

Write-Host ""
Write-Host "  >>> BEEP = ENTER en Cursor Agent AHORA <<<" -ForegroundColor Yellow
Write-Host ""
if (-not $NoBeep) {
    [Console]::Beep(880, 100)
}
Start-Sleep -Milliseconds $DelayMs
if (-not $NoBeep) {
    [Console]::Beep(1320, 150)
}

$body = @{
    kind       = 'approval_needed'
    speak      = $true
    priority   = $true
    emotion    = $emotions[$personalityId]
    message    = $messages[$personalityId]
    context    = $ctx
    dedupe_key = "video-take-$(Get-Date -Format 'yyyyMMddHHmmssfff')"
} | ConvertTo-Json -Depth 4

try {
    $r = Invoke-RestMethod -Uri "$base/api/dev/notify" -Method POST -ContentType 'application/json' -Body $body -TimeoutSec 10
    if ($r.skipped) {
        Write-Host "Omitido (cooldown): $($r.reason)" -ForegroundColor Yellow
    } else {
        Write-Host "Robot: $($r.cmd.text)" -ForegroundColor Green
    }
    $r | ConvertTo-Json -Depth 4
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    Write-Host "¿Servidor arriba? cd server; .\start.ps1" -ForegroundColor Yellow
    exit 1
}
