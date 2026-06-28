# agenteIA — instrucciones para Claude Code / Cursor

Proyecto compartido. Memoria persistente via **Engram** (proyecto `agenteia`).

## Engram MCP

- Config: `.mcp.json` en esta carpeta (`engram mcp --project agenteia`).
- Verificar con `/mcp` que **engram** está conectado.

## Inicio de CADA sesión (OBLIGATORIO)

1. `mem_context` o `mem_search` con query `agenteia canon audio`
2. **Leer canon completo**: `.engram/agenteia-merge.txt` (contiene TODO el chat relevante, bug abierto, reglas usuario)
3. Si Engram y el código difieren → **gana el código en disco**; luego actualizar Engram

> ⚠️ El chat de Cursor no se transfiere automáticamente a Claude. El canon está en Engram + `agenteia-merge.txt`.

## Guardar en Engram (automático al cerrar tarea con cambios)

Formato:
```
**What**: qué cambió
**Why**: motivo
**Where**: archivos
**Learned**: gotchas
```
Actualizar también `.engram/agenteia-merge.txt` si cambia canon o estado del bug.

---

## Contexto rápido

- **Placa**: Hosyond ES3C28P, ESP32-S3, ES8311, WakeNet "Hi ESP"
- **Sketch**: `firmware/agente-ia/`
- **Servidor**: `server/` — `start.ps1`, `/converse`, `/tts`
- **Bug abierto**: 2ª grabación tras TTS → `i2s_channel_read: channel not enabled`. Ver merge.txt.
- **Regla de oro**: 1ª grabación = `prepareCapture()` solo drain; TTS = TX mono + write directo; MCLK×384

## Build Arduino

- Board: ESP32S3 Dev Module, OPI PSRAM, 16MB
- Partition: **ESP SR 16M (3MB APP/7MB SPIFFS/2.9MB MODEL)**
- Abrir sketch desde `firmware/agente-ia/` (no renombrar carpeta)

## Servidor

```powershell
cd server
.\start.ps1
```

Usar venv (`.venv`), no Python global. Whisper default cuda; TTS SAPI (pyttsx3).
