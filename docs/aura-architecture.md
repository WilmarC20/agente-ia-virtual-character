# AURA Platform Architecture

agenteIA evolves from a monolithic ESP32 sketch + Python server into a **modular platform** for physical AI assistants. The first vertical slice is **AURA** (Adaptive User Rendering Architecture): data-driven themes on-device, with the "brain" on PC/Raspberry Pi.

## Split: thin device / fat brain

| Layer | Location | Responsibility |
|-------|----------|----------------|
| **Device** | ESP32-S3 | AURA render, HAL, Event Bus, playback, wake/touch |
| **Brain** | `server/` | Conversation, Personality, Behavior, Voice, plugins |

Communication: HTTP poll today; WebSocket event stream (planned, poll fallback).

## AURA (device)

```
Theme JSON (SPIFFS/SD) → ThemeManager + LayoutManager
                      → SceneManager → Widgets → Renderer (sole gfx access)
```

### Phase 1 (current)

- `firmware/agente-ia/aura/` — Renderer, ThemeManager, LayoutManager, AnimationEngine, AtlasText
- KITT dashboard behind `USE_AURA` in `config.h`
- Theme data in `themes/kitt/*.json` (colors, layout, labels, widgets, animations)
- Labels: hybrid text — Michroma extended bitmap font (static); glyph atlas for dynamic text later

### Phase 2+

- SceneManager, ExpressionEngine, widget library (EmotionFace, VoiceIndicator, MusicPlayer, …)
- Theme sync over WiFi from brain

## Contracts (`sdk/contracts/`)

JSON Schemas for cross-engine messages:

- `EventEnvelope` — bus envelope (id, type, source, payload)
- `IntentPacket` — user intent from STT
- `EmotionState` — emotion + intensity for face/widgets
- `ActuationPlan` — brain → device actuation (face, voice, scene)
- `SceneCommand` — scene transitions
- `ThemeManifest`, `WidgetSpec`, `LayoutSpec` — theme packaging

## Brain engines (`server/engines/`)

Strangler-fig extraction from `main.py`:

- `conversation`, `personality`, `behavior`, `voice`, `notification`, `automation`, `guardian`, `plugins`, `scheduler`, `device_manager`, `event_bus`

Each engine exposes a thin adapter; `main.py` remains the integration point until fully split.

## Hardware abstraction (`firmware/agente-ia/hal/`)

Wraps display, touch, audio, RGB LED. Drivers stay in existing headers; HAL is the future boundary for new boards.

## Building themes

```powershell
python tools/atlas_builder.py --theme kitt
```

Produces `labels_atlas.bin` + manifest when a source PNG is provided.

## Feature flag

```cpp
#define USE_AURA 1  // config.h — KITT via AURA; 0 = legacy face_kitt.h path
```

## References

- `docs/behavior-engine-architecture.md` — M1–M12 behavior model
- `themes/kitt/` — reference theme (1:1 Knight Rider HMI, 240×320 portrait)
