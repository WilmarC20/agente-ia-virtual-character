# SDK — guía para la comunidad

## Crear un tema

1. Copiá `examples/minimal-theme/` o `themes/kitt/` como plantilla.
2. Editá `theme.json`, `colors.json`, `layout.json`, `widgets.json`.
3. Opcional: generá atlas con `python tools/atlas_builder.py --theme mi-tema --input source.png`.
4. Subí el paquete al ESP vía sync WiFi (futuro) o SPIFFS.

## Crear un plugin del cerebro

1. Copiá `examples/hello-plugin/`.
2. Implementá `handle_intent(text)` y `on_load(event_bus)`.
3. Registrá el plugin en la config del servidor (próximo: `PluginManager`).

## Contratos

Todos los mensajes entre motores deben validar contra `sdk/contracts/*.schema.json`.

## Widgets soportados

Ver `widget_spec.schema.json` — tipos: Button, EmotionFace, VoiceIndicator, MusicPlayer, StatusCard, Popup, Toast, Menu, etc.

## Hardware

Perfiles de placa en `hardware/`; pins por defecto en `firmware/agente-ia/config.h`.
