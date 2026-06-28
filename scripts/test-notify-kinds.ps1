#!/usr/bin/env pwsh
# Prueba TTS de todos los kinds de notify (sin pasar por hooks de Claude).
# Uso: .\scripts\test-notify-kinds.ps1
# Requiere: server/start.ps1 corriendo

$base = if ($env:AGENTEIA_NOTIFY_URL) { $env:AGENTEIA_NOTIFY_URL.TrimEnd('/') } else { 'http://127.0.0.1:8000' }
$ctx = @{ project = 'agenteIA'; file = 'face.h'; tool = 'Shell'; task = 'prueba-hooks'; server = 'notion'; source = 'claude'; client = 'Claude Code' }

$kinds = @(
    'ask_question',
    'approval_needed',
    'agent_blocked',
    'stop_failure',
    'subagent_done',
    'elicitation',
    'task_completed'
)

foreach ($k in $kinds) {
    $body = @{
        kind       = $k
        context    = $ctx
        dedupe_key = "manual-test-$k-$(Get-Date -Format 'yyyyMMddHHmmss')"
    } | ConvertTo-Json -Depth 4
    Write-Host "`n>>> $k" -ForegroundColor Cyan
    try {
        $r = Invoke-RestMethod -Uri "$base/api/dev/notify" -Method POST -ContentType 'application/json' -Body $body
        if ($r.skipped) { Write-Host "  omitido: $($r.reason)" -ForegroundColor Yellow }
        else { Write-Host "  ok: $($r.cmd.text)" -ForegroundColor Green }
    } catch {
        Write-Host "  error: $_" -ForegroundColor Red
    }
    Start-Sleep -Seconds 3
}

Write-Host "`nListo. Espera ~3s entre avisos; cooldown del servidor = 45s por dedupe_key similar." -ForegroundColor Gray
