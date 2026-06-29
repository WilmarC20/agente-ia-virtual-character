# PROJECT SNAPSHOT — agenteIA

> Fotografía técnica del proyecto **antes** de iniciar la nueva arquitectura (*Aura Engine*).
> Este documento describe el estado estable de cierre de la **primera gran etapa**.

---

## 1. Nombre del proyecto

**agenteIA** — Asistente conversacional con cara (repositorio: `agente-ia-virtual-character`).
"ChatGPT con cuerpo": voz, cara, personalidad, domótica e integración con IDEs (Cursor / Claude).

## 2. Fecha

**2026-06-29**

## 3. Rama actual

`main`

## 4. Commit actual (base del checkpoint)

`86f6ec1` — *"Reorganizar /admin en perfiles unificados y añadir KITT."*

## 5. Hash del commit (base)

`86f6ec1867f8c30d2f47fa0b39cd1b9eb3679add`

- Total de commits en la historia: **55**
- Tag de cierre de etapa: **`v1.0.0-foundation`** (creado en este checkpoint)
- El commit del checkpoint queda anclado a ese tag.

## 6. Estado del repositorio

- Remoto: `origin` → `https://github.com/WilmarC20/agente-ia-virtual-character.git`
- Rama `main` sincronizada con `origin/main` antes del checkpoint.
- Archivos de producto **modificados** sin commit (entran al checkpoint):
  `firmware/agente-ia/agente-ia.ino`, `config.h`, `face.h`, `touch.h`, `web_admin.h`,
  `server/admin.html`, `server/face_preview.js`, `server/main.py`, `server/music_service.py`,
  `server/server_config.py`.
- Archivos **nuevos de código** (entran al checkpoint):
  `firmware/agente-ia/face_kitt.h`, `face_presentation.h`, `font_kitt_extended.h`,
  `firmware/agente-ia/fonts/` (fuente Michroma OFL), `tools/ttf_to_gfx.py`.
- Artefactos / binarios / tooling local **excluidos** vía `.gitignore` (ver §11 y `.gitignore`):
  builds (~350 MB), modelos RVC (~216 MB), voces (~60 MB), audio de depuración,
  copias manuales (`checkpoints/`, `firmware/checkpoints/`, `firmware/commit/`, `firmware/agente-iaV1/`),
  y configuración de IDE/agente (`.cursor/`, `.engram/`, `.atl/`, `.mcp.json`).

## 7. Arquitectura actual

Sistema **cliente-servidor** de dos nodos en la red local:

```
ESP32-S3 (cara + oídos)                 PC (cerebro)
├── 12 emociones + lip-sync             ├── FastAPI (server/main.py)
├── Pantalla táctil 240×320 + parlante  ├── Whisper → Ollama → emoción + texto
├── Graba voz → POST /converse ────────→├── TTS + RVC → voz del personaje
├── Música / modo historia ←────────────┤ YouTube Music · Home Assistant
└── Panel web en http://IP/             └── Panel /admin + hooks Cursor/Claude
```

- **Firmware (ESP32-S3)**: captura de audio (I2S/ES8311), render de cara y UI (LovyanGFX),
  táctil (FT6336), reproducción de audio, pantallas (música, ajustes, captions), wake word.
- **Servidor (PC, Python/FastAPI)**: STT (faster-whisper), LLM (Ollama), TTS (edge-tts/Piper/SAPI),
  conversión de voz (RVC/Applio), música (yt-dlp/ytmusic), modo historia, domótica (Home Assistant),
  memoria emocional/contexto, y endpoints de notificación para IDEs.
- **Presentaciones de cara**: motor de presentación abstracto (`face_presentation.h`) con
  personajes (Bender en landscape, KITT en portrait) — base sobre la que crecerá *Aura Engine*.

## 8. Tecnologías utilizadas

| Capa | Tecnología |
|------|------------|
| MCU | ESP32-S3 (Arduino core 3.x), C++ |
| Display / gráficos | LovyanGFX (ILI9341 240×320, sprites/doble buffer) |
| Táctil | FT6336 (I2C) |
| Audio MCU | ES8311 códec, I2S, MEMS mic + parlante |
| Wake word | ESP-SR (WakeNet "Hi ESP") |
| Servidor | Python 3.x, FastAPI, Uvicorn |
| STT | faster-whisper (CUDA/CPU) |
| LLM | Ollama (qwen2.5:7b por defecto) |
| TTS | edge-tts (neural), Piper, SAPI |
| Conversión de voz | RVC / Applio (venv aparte, Python 3.11) |
| Música | yt-dlp, pytubefix, ytmusicapi |
| Domótica | Home Assistant (REST + token) |
| Serialización | ArduinoJson (MCU), JSON (servidor) |

## 9. Librerías

**Firmware (Arduino):** LovyanGFX, ArduinoJson, ESP_SR, ESP_I2S, WiFi, WebServer, Preferences,
Wire, FS, SPIFFS, Hash (core ESP32 3.3.6).

**Servidor (`server/requirements.txt`):** fastapi, python-multipart, uvicorn[standard], httpx,
faster-whisper, piper-tts, edge-tts, miniaudio, numpy, scipy, soundfile, yt-dlp, yt-dlp-ejs,
pytubefix, ytmusicapi, nvidia-cublas-cu12, nvidia-cudnn-cu12, nvidia-cuda-runtime-cu12.

**Herramientas (`tools/`):** `ttf_to_gfx.py` (requiere `freetype-py`) — conversor de fuentes a header GFX.

## 10. Dependencias externas (runtime)

- **Ollama** corriendo localmente con un modelo (p.ej. `qwen2.5:7b`).
- **Home Assistant** (opcional) con `HA_URL` + `HA_TOKEN`.
- **GPU NVIDIA + CUDA 12** (opcional, acelera Whisper/RVC).
- **Secretos** no versionados: `firmware/agente-ia/secrets.h`, `server/secrets.local.ps1`, `server/.env`.

## 11. Organización del proyecto

```
agenteIA/
├── firmware/agente-ia/        # Sketch ESP32-S3 (.ino + headers por módulo)
│   ├── agente-ia.ino          # Entry point / loop principal
│   ├── config.h               # Pines, rotación display, flags
│   ├── face.h                 # Clase Face (emociones, lip-sync, sprites)
│   ├── face_presentation.h    # Abstracción de presentación (Bender/KITT)
│   ├── face_kitt.h            # UI tablero KITT (portrait 240×320)
│   ├── font_kitt_extended.h   # Fuente GFX generada (Michroma bold)
│   ├── touch.h, display_setup.h, es8311.h, audio_*.h
│   ├── music_screen.h, settings.h, speech_caption*.h, sound_fx.h
│   ├── web_admin.h, playback_spectrum.h
│   ├── fonts/                 # Fuente fuente TTF (OFL)
│   └── secrets.example.h      # Plantilla de credenciales (real ignorada)
├── server/                    # Cerebro (FastAPI)
│   ├── main.py                # API: /converse, /tts, /music, /api/dev/notify…
│   ├── server_config.py       # Perfiles/personalidades, parámetros RVC
│   ├── tts_engine.py, edge_worker.py, sapi_worker.py, piper_worker.py, bark_worker.py
│   ├── rvc_worker.py, applio_rvc_*.py, singing_pipeline.py
│   ├── music_service.py, story_mode.py
│   ├── context_manager.py, emotional_memory.py, agent_state.py
│   ├── ha_client.py, esp_registry.py, text_encoding.py
│   ├── admin.html             # Panel web /admin
│   └── requirements.txt
├── docs/                      # README assets, datasheets, arquitectura
│   └── behavior-engine-architecture.md
├── tools/                     # Utilidades de desarrollo (conversor de fuentes)
├── README.md                  # Documentación bilingüe del producto
└── CLAUDE.md                  # Guía para agentes de IA
```

## 12. Funcionalidades actuales

- **Asistente virtual conversacional** (preguntas abiertas, chistes, consejos, hora).
- **Pantalla** táctil 240×320 con render por sprites (doble buffer).
- **Emociones** (12: neutral, happy, sad, angry, vibing, love, thinking, confused…).
- **Expresiones** dinámicas: parpadeo, mirada viva, cejas, respiración idle, lip-sync.
- **Personalidades** (6 editables: Bender, Burro, J.A.R.V.I.S., amigable, técnico, curioso).
- **Presentaciones visuales por personaje** (Bender landscape, KITT portrait).
- **TTS** (edge-tts neural, Piper, SAPI) + **RVC** (timbre del personaje) + canto opcional.
- **STT** (faster-whisper, GPU/CPU).
- **LLM** local vía Ollama (emoción + texto de respuesta).
- **Música** por voz y panel web (YouTube / YouTube Music) con cara *vibing*.
- **Modo historia** (audio + timeline de emociones sincronizadas).
- **Domótica** (Home Assistant): luces, interruptores, escenas, clima, presencia.
- **Notificaciones de IDE**: hooks `POST /api/dev/notify` para Cursor/Claude
  (permiso, fallo de tool, fin de subagente, rate limit).
- **Menús / pantallas**: ajustes en pantalla, pantalla de música, captions tipo karaoke.
- **Táctil**: despertar, caricia, golpe, abrir ajustes.
- **Configuración**: panel `/admin` (preset, prompt, modelo, memoria, voz RVC, prueba de chat).
- **Sensores / audio**: mic MEMS, detección de nivel, wake word "Hi ESP" (ESP-SR).
- **Webhooks / API** REST en el servidor; panel web en el dispositivo.
- **Memoria**: contexto conversacional + memoria emocional + estado del agente.

> No implementado aún (roadmap): wake word por voz 100% estable, visión (ESP-CAM), MQTT/OTA.

## 13. Explicación general del funcionamiento

1. El **ESP32-S3** muestra la cara del personaje y escucha. Al despertarlo (táctil o wake word),
   **graba** la voz del usuario y la envía por HTTP (`POST /converse`) al servidor.
2. El **servidor** transcribe con Whisper, consulta a **Ollama** (que devuelve texto + emoción),
   sintetiza la respuesta con **TTS** y, opcionalmente, la convierte al timbre del personaje con **RVC**.
3. El audio resultante se **transmite de vuelta** al ESP32, que lo reproduce mientras anima la cara
   (lip-sync, emoción) en la pantalla.
4. En paralelo, el dispositivo puede **reproducir música**, narrar **historias** con emociones
   sincronizadas, **controlar la casa** (Home Assistant) o **avisar** cuando un IDE (Cursor/Claude)
   necesita atención.
5. Toda la **configuración** (personalidad, prompt, voz, memoria) se gestiona desde el panel web `/admin`.

## 14. Estado de compilación (verificación de la auditoría)

- **Servidor (Python):** dependencias declaradas en `server/requirements.txt`; arranque con `server/start.ps1` (crea `.venv`, instala, levanta uvicorn).
- **Firmware (ESP32-S3):** el estado base (`86f6ec1`) compila y flashea correctamente
  (verificado previamente: **1.791.375 bytes, 56% APP**). Los cambios de esta etapa (UI KITT + fuente)
  compilan a nivel de fuentes; durante la auditoría se observaron incidencias **del entorno de build**
  (ICE de GCC por presión de memoria en compilación paralela, y un error de enlazado por
  `.debug_line` en objetos cacheados), mitigadas con compilación **limpia y en serie** (`--clean --jobs 1`).
  No son defectos del código del producto.
