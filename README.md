# agenteIA — Virtual Character with Emotions

An ESP32-S3 board with a 2.8" display acts as the face and ears of a virtual
character. A PC on the local network runs the brain (Whisper + Ollama).

```
ESP32-S3 (face + ears)                PC (brain)
├── Paints emotions on screen          ├── FastAPI server (server/main.py)
├── Wake word "Hi ESP" (ESP-SR, local) ├── Whisper STT (audio -> text)
├── Records speech, POSTs WAV ────────→├── Ollama (qwen2.5:7b) -> reply + emotion
└── Shows reply + emotion  ←───────────┘
```

## Hardware

[Hosyond ES3C28P](https://us.amazon.com/dp/B0FKG7WRWV): ESP32-S3, 2.8" 240x320
IPS (ILI9341), FT6336 capacitive touch, ES8311 audio codec with MEMS mic,
speaker output, 16MB flash, OPI PSRAM. Schematic and datasheets in
`docs/datasheets/`. Full pin map in `firmware/agente-ia/config.h`.

## Server setup (PC)

```powershell
cd server
.\start.ps1
```

`start.ps1` creates `.venv`, installs deps (including `piper-tts`), and launches uvicorn.
Do **not** run `uvicorn` with global Python — TTS will fail with `No module named 'piper'`.

Requires Ollama running with `qwen2.5:7b`. TTS uses **edge-tts** (voice
`es-MX-DaliaNeural`, natural; needs internet). Fallback: Piper offline.

Env vars: `OLLAMA_URL`, `OLLAMA_MODEL`, `WHISPER_MODEL`, `EDGE_TTS_VOICE`,
`TTS_PCM_GAIN` (default `0.55` — lower if speaker still loud).

Wake phrase **"Hi ESP"** is detected on-device by **WakeNet** (ESP-SR library).
The server endpoint `/wake-check` still exists for debugging but is no longer used by the firmware.

Test without the board:

```powershell
curl -X POST http://localhost:8000/chat -H "Content-Type: application/json" -d '{"text": "hola, como estas?"}'
```

## Firmware setup (Arduino IDE)

1. Install **esp32 by Espressif** core **3.x** (Tools → Board → Boards Manager).
2. Install libraries: **LovyanGFX**, **ArduinoJson**.
3. Open **`firmware/agente-ia/agente-ia.ino`** (not the parent `agenteIA` folder).
4. Board: **ESP32S3 Dev Module**
   - PSRAM: **OPI PSRAM**
   - Flash Size: **16MB (128Mb)**
   - Partition Scheme: **ESP SR 16M (3MB APP/7MB SPIFFS/2.9MB MODEL)** — required for WakeNet (`ENABLE_WAKEWORD=1` in `config.h`). Without this partition the board boot-loops with *Can not find model in partition table*.
5. Copy `secrets.example.h` → `secrets.h` (WiFi + server URL).

### Error `WiFi.h: No such file or directory`

The IDE is **not** targeting ESP32 (often Arduino Uno / wrong board). Fix:

- Tools → Board → **ESP32 Arduino** → **ESP32S3 Dev Module**
- Close and reopen `firmware/agente-ia/agente-ia.ino`
- Arduino IDE 2.x: the included `sketch.yaml` should pick the profile **es3c28p** automatically

`WiFi.h` ships with the esp32 core (`packages/esp32/.../libraries/WiFi/`).

## Status / roadmap

- [x] Brain server: WAV -> Whisper -> Ollama -> `{emotion, reply}`
- [x] Face renderer: 7 emotions with blinking (neutral, happy, sad, angry, surprised, thinking, sleepy)
- [x] Wake word via ESP-SR ("Hi ESP" — custom phrases require Espressif's commercial training)
- [x] ES8311 mic capture validated on hardware
- [x] Touch-to-wake + mic VU meter
- [x] TTS voice replies through the board speaker (Piper, `es_MX-claude-high`, `/tts` endpoint)
- [ ] Lip-sync / talking animation while speaking
- [ ] Conversation memory (multi-turn context)
