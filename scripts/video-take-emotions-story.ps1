#!/usr/bin/env pwsh
# Historia Bender: audio completo + emociones sincronizadas (modo story).
#
# Uso:
#   .\scripts\video-take-emotions-story.ps1
#   .\scripts\video-take-emotions-story.ps1 -From 7 -To 9
#   .\scripts\video-take-emotions-story.ps1 -Only angry
#   .\scripts\video-take-emotions-story.ps1 -NoBeep
#   .\scripts\video-take-emotions-story.ps1 -GapMs 500

param(
    [int]$FirstDelayMs = 4000,
    [int]$GapMs = 350,
    [int]$From = 1,
    [int]$To = 0,
    [string]$Only = '',
    [switch]$NoBeep
)

$base = if ($env:AGENTEIA_NOTIFY_URL) { $env:AGENTEIA_NOTIFY_URL.TrimEnd('/') } else { 'http://127.0.0.1:8000' }

# Orden = arco narrativo para video de emociones
$story = @(
    @{ n = 1; emotion = 'neutral'; message = 'Ah... desperté. A ver qué daño hiciste mientras estaba apagado.' }

    @{ n = 2; emotion = 'happy'; message = '¡Milagro! Todo funciona. No toques nada.' }

    @{ n = 3; emotion = 'excited'; message = '¡Vamos! Abrí Cursor... necesito reírme un rato.' }

    @{ n = 4; emotion = 'thinking'; message = 'Estoy analizando tu código... dame un segundo para dejar de llorar.' }

    @{ n = 5; emotion = 'confused'; message = 'Esperá... ¿eso compila o es arte moderno?' }

    @{ n = 6; emotion = 'surprised'; message = '¡Compiló a la primera!... Eso estuvo sospechoso.' }

    @{ n = 7; emotion = 'angry'; message = 'Claude volvió a gastar todos los tokens. Ese tipo administra peor que vos.' }

    @{ n = 8; emotion = 'sad'; message = 'Sin tokens... sin respuestas... ahora vas a tener que pensar por tu cuenta.' }

    @{ n = 9; emotion = 'love'; message = 'Mi ESP32... el único aquí que realmente trabaja.' }

    @{ n = 10; emotion = 'cool'; message = 'Tranquilo. Si esto explota, voy a decir que fue idea tuya.' }

    @{ n = 11; emotion = 'dizzy'; message = 'Demasiados commits... hasta Git perdió la fe.' }

    @{ n = 12; emotion = 'sleepy'; message = 'Despertame cuando aparezca un bug interesante.' }

    @{ n = 13; emotion = 'vibing'; message = 'Mientras compila... yo cobro por mirar.' }
)

$lines = $story
if ($Only) {
    $key = $Only.Trim().ToLower()
    $lines = @($story | Where-Object { $_.emotion -eq $key })
    if ($lines.Count -eq 0) {
        Write-Host 'Emocion desconocida:' $Only -ForegroundColor Red
        Write-Host ('Validas: ' + ($story.emotion -join ', ')) -ForegroundColor Yellow
        exit 1
    }
} else {
    $maxN = if ($To -gt 0) { $To } else { ($story.n | Measure-Object -Maximum).Maximum }
    if ($From -lt 1) { $From = 1 }
    $lines = @($story | Where-Object { $_.n -ge $From -and $_.n -le $maxN })
}

$beats = @($lines | ForEach-Object {
    @{ text = $_.message; emotion = $_.emotion }
})

Write-Host ''
Write-Host ('  Historia Bender - ' + $lines.Count + ' beats (modo story)') -ForegroundColor Cyan
Write-Host ('  Gap TTS: ' + $GapMs + 'ms | Primer beat tras beep: ' + $FirstDelayMs + 'ms') -ForegroundColor DarkGray
Write-Host ''
Write-Host '  BEEP = encende camara / ENTER en grabacion' -ForegroundColor Yellow
Write-Host ''

if (-not $NoBeep) {
    [Console]::Beep(660, 120)
    Start-Sleep -Milliseconds 80
    [Console]::Beep(880, 120)
}
Start-Sleep -Milliseconds $FirstDelayMs

$body = @{
    title    = 'Historia Bender emociones'
    beats    = $beats
    gap_ms   = $GapMs
    priority = $true
} | ConvertTo-Json -Depth 5

Write-Host 'Generando audio + encolando en ESP...' -ForegroundColor Cyan
try {
    $r = Invoke-RestMethod -Uri "$base/api/dev/story" -Method POST -ContentType 'application/json' -Body $body -TimeoutSec 600
} catch {
    Write-Host '  Error:' $_ -ForegroundColor Red
    Write-Host '  Servidor arriba? cd server; .\start.ps1' -ForegroundColor Yellow
    exit 1
}

Write-Host ''
Write-Host ('Story id: ' + $r.story_id) -ForegroundColor Green
Write-Host ('Duracion: ' + $r.duration_ms + ' ms | Cola ESP: ' + $r.queued) -ForegroundColor Green
Write-Host ''
Write-Host 'Timeline de emociones:' -ForegroundColor DarkGray
foreach ($cue in $r.timeline) {
    Write-Host ('  ' + $cue.at_ms + 'ms -> ' + $cue.emotion) -ForegroundColor DarkGray
}
Write-Host ''
Write-Host 'El ESP reproducira el guion completo con emociones sincronizadas.' -ForegroundColor Green
