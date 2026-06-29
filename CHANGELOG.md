# Changelog

Todos los cambios notables de este proyecto se documentan aquí.

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/)
y el proyecto adopta [Semantic Versioning](https://semver.org/lang/es/).

---

## [1.0.0-foundation] — 2026-06-29

**Hito:** cierre de la primera gran etapa del proyecto. Último estado estable
antes de iniciar la nueva arquitectura **Aura Engine**.

- **Versión:** `1.0.0-foundation`
- **Fecha:** 2026-06-29
- **Tag git:** `v1.0.0-foundation`
- **Rama:** `main`
- **Estado:** Estable / Checkpoint de restauración.

### Descripción

Primera versión consolidada del asistente conversacional con cara: firmware
ESP32-S3 + cerebro en PC (FastAPI). Incluye conversación con IA, 12 emociones con
lip-sync, 6 personalidades editables, TTS + RVC, música, modo historia, domótica
(Home Assistant), notificaciones para Cursor/Claude y panel web de administración.

### Añadido (resumen de esta etapa)

- Conversación IA con memoria y 6 personalidades editables.
- 12 emociones, expresiones (parpadeo, mirada, cejas, respiración idle) y lip-sync.
- TTS (edge-tts / Piper / SAPI) + conversión de voz RVC + canto opcional.
- Música por voz y panel web (YouTube / YouTube Music).
- Modo historia con timeline de emociones sincronizadas.
- Domótica opcional vía Home Assistant.
- Hooks de notificación de IDE (`POST /api/dev/notify`).
- Motor de **presentaciones de cara** por personaje (Bender landscape / KITT portrait).
- Tablero **KITT** (UI portrait 240×320) y fuente GFX embebida estilo "bold extended"
  (Michroma, OFL) con conversor reutilizable `tools/ttf_to_gfx.py`.

### Notas

- Este tag es un **punto de restauración completo** (ver `RESTORE.md`).
- Artefactos de build, modelos RVC, voces y configuración local quedan fuera del
  control de versiones (ver `.gitignore`). Restaurar el tag **no** regenera esos
  binarios: se reconstruyen compilando el firmware e instalando dependencias.
- La refactorización posterior se realizará en la rama `feature/aura-engine`
  (ver `AURA_MIGRATION_PLAN.md`), sin tocar `main` hasta que el nuevo sistema funcione.

---

## Tipos de cambio (referencia)

- **Añadido** — funcionalidades nuevas.
- **Cambiado** — cambios en funcionalidad existente.
- **Obsoleto** — funciones que se eliminarán pronto.
- **Eliminado** — funciones retiradas.
- **Corregido** — corrección de errores.
- **Seguridad** — vulnerabilidades.
