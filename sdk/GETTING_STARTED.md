# AURA Platform SDK — Getting Started

## 1. Clone and run the brain

```powershell
cd server
.\start.ps1
```

## 2. Flash the device

- Board: ESP32S3 Dev Module, partition **ESP SR 16M**
- Sketch folder: `firmware/agente-ia/`
- Set `secrets.h` from `secrets.example.h`

## 3. Create a theme

1. Copy `examples/custom-theme/` or `themes/kitt/`.
2. Edit `theme.json`, `colors.json`, `layout.json`, `widgets.json`.
3. List themes: `GET http://localhost:8000/api/themes`
4. Device sync: firmware downloads `colors.json` on presentation change.

## 4. Create a brain plugin

1. Copy `examples/hello-plugin/`.
2. Implement `handle_intent(text)` in `plugin.py`.
3. Restart server — plugins load from `examples/hello-plugin/`.

## 5. Contracts

Validate payloads against `sdk/contracts/*.schema.json`:

- `scene_command.schema.json`
- `emotion_state.schema.json`
- `widget_spec.schema.json`

## 6. Examples index

| Folder | Purpose |
|--------|---------|
| `minimal-theme/` | Skeleton theme JSON |
| `custom-theme/` | Full color override |
| `custom-widget/` | `GaugeWidget.h` sample |
| `custom-personality/` | Server personality dict |
| `custom-device/` | Hardware profile stub |
| `hello-plugin/` | Intent plugin |

See also `sdk/PLUGIN_GUIDE.md`.
