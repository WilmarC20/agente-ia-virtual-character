#!/usr/bin/env pwsh
# Prueba notify para Claude y Cursor (misma API, distinto source).
# Uso: .\scripts\test-notify-both.ps1
# Requiere: server/start.ps1

$base = if ($env:AGENTEIA_NOTIFY_URL) { $env:AGENTEIA_NOTIFY_URL.TrimEnd('/') } else { 'http://127.0.0.1:8000' }
$ctxBase = @{ project = 'agenteIA'; file = 'face.h'; tool = 'Shell' }

$tests = @(
    @{ label = 'Claude — pregunta'; kind = 'ask_question'; ctx = $ctxBase + @{ source = 'claude'; client = 'Claude Code' } },
    @{ label = 'Cursor — pregunta'; kind = 'ask_question'; ctx = $ctxBase + @{ source = 'cursor'; client = 'Cursor' } },
    @{ label = 'Claude — permiso'; kind = 'approval_needed'; ctx = $ctxBase + @{ source = 'claude'; client = 'Claude Code' } },
    @{ label = 'Cursor — fallo tool'; kind = 'agent_blocked'; ctx = $ctxBase + @{ source = 'cursor'; client = 'Cursor' } },
    @{ label = 'Claude — stop'; kind = 'stop_failure'; ctx = $ctxBase + @{ source = 'claude'; client = 'Claude Code' } }
)

$i = 0
foreach ($t in $tests) {
    $i++
    $body = @{
        kind       = $t.kind
        context    = $t.ctx
        dedupe_key = "both-test-$i-$(Get-Date -Format 'yyyyMMddHHmmss')"
    } | ConvertTo-Json -Depth 4
    Write-Host "`n>>> $($t.label)" -ForegroundColor Cyan
    try {
        $r = Invoke-RestMethod -Uri "$base/api/dev/notify" -Method POST -ContentType 'application/json' -Body $body
        if ($r.skipped) { Write-Host "  omitido: $($r.reason)" -ForegroundColor Yellow }
        else { Write-Host "  $($r.cmd.text)" -ForegroundColor Green }
    } catch {
        Write-Host "  error: $_" -ForegroundColor Red
    }
    Start-Sleep -Seconds 4
}
