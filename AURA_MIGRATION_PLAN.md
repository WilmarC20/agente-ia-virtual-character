# AURA ENGINE — Plan de migración

> Plan de la refactorización de gran escala (*Aura Engine*) partiendo del checkpoint
> estable `v1.0.0-foundation`. Documento vivo: el alcance fino se confirmará con el
> autor antes de escribir código. **Nada de este plan se ejecuta hasta tu aprobación.**

## Objetivo de Aura Engine

Evolucionar la base actual hacia un **motor modular de personajes** con bajo
acoplamiento y alta cohesión, donde *personalidad + voz + presentación visual +
comportamiento* sean componentes intercambiables y declarativos (data-driven),
manteniendo el sistema funcionando en todo momento.

Principios guía: **abstraer → migrar → eliminar lo viejo solo cuando lo nuevo funciona.**

---

## 1. Qué componentes CAMBIARÁN

| Componente | Cambio previsto |
|------------|-----------------|
| `face.h` (firmware) | Se divide: estado del personaje vs. renderizado. Render detrás de una interfaz `IPresentation`. |
| `face_presentation.h` | Se eleva a contrato estable del motor (registro de presentaciones Bender/KITT/futuros). |
| `face_kitt.h` / UI | Se convierte en una "skin" declarativa registrada en el motor, no en código incrustado. |
| `server/server_config.py` | Personalidades pasan a definición declarativa (datos) consumida por el motor. |
| `server/main.py` | Se adelgaza: orquestación → servicios desacoplados (STT, LLM, TTS, RVC, música). |
| Capa de emociones/expresiones | API unificada de emociones reutilizable por cualquier presentación. |

## 2. Qué componentes PERMANECERÁN (sin tocar al inicio)

- Drivers de bajo nivel: `es8311.h`, `audio_recorder.h`, `audio_output.h`, `display_setup.h`, `touch.h`.
- Workers de voz: `edge_worker.py`, `piper_worker.py`, `sapi_worker.py`, `rvc_worker.py`, `applio_*`.
- Integraciones: `ha_client.py` (Home Assistant), `music_service.py`, `story_mode.py`.
- Contratos de red existentes: `/converse`, `/tts`, `/music`, `/api/dev/notify`.
- Esquema de secretos y configuración (`secrets.*`, `.env`, panel `/admin`).

## 3. Riesgos

| Riesgo | Impacto | Mitigación |
|--------|---------|------------|
| Romper lip-sync / timing de audio al refactorizar `face.h` | Alto | Mantener ruta vieja activa tras una interfaz; pruebas A/B en hardware. |
| Regresión de táctil al cambiar rotación/presentaciones | Medio | Tests manuales por presentación; conservar `g_activeDisplayRotation`. |
| Cambios en contratos de API rompen el firmware | Alto | Versionar endpoints; mantener compatibilidad hacia atrás. |
| Inestabilidad del toolchain de build (ICE/enlazado) | Medio | Compilar `--clean --jobs 1`; CI reproducible documentado. |
| Crecimiento de PSRAM/flash por nuevas skins/fuentes | Medio | Medir tamaño tras cada merge; presupuesto de memoria. |
| Personalidades declarativas incompatibles con prompts actuales | Medio | Migrar perfil por perfil con fallback al formato viejo. |

## 4. Cómo dividir la migración (fases incrementales)

1. **F0 — Andamiaje**: definir interfaces (`IPresentation`, `ICharacter`, `IEmotionState`) sin cambiar comportamiento.
2. **F1 — Adaptadores**: envolver el código actual (Bender/KITT) detrás de las interfaces (sin borrar nada).
3. **F2 — Registro/loader**: motor que selecciona presentación/personalidad por datos.
4. **F3 — Migrar personalidades** del servidor a definición declarativa (una por una).
5. **F4 — Migrar presentaciones** a "skins" del motor (Bender, luego KITT).
6. **F5 — Adelgazar `main.py`/`face.h`** moviendo lógica a servicios/módulos.
7. **F6 — Limpieza**: eliminar el código viejo **solo** cuando el nuevo cubre el 100%.

## 5. Cómo mantener compatibilidad

- Patrón **Strangler Fig**: lo nuevo convive con lo viejo tras una fachada.
- **Feature flags** para activar el motor nuevo sin quitar el camino antiguo.
- **No** cambiar contratos de red sin versión; mantener defaults actuales.
- Cada PR deja el sistema **compilando y funcional**.

## 6. Qué pruebas realizar

- **Firmware**: compilación limpia; arranque; render de cada presentación; lip-sync;
  táctil (despertar/caricia/golpe/ajustes); reproducción de audio; música/*vibing*.
- **Servidor**: `/converse` end-to-end (STT→LLM→TTS→RVC); `/tts`; `/music`;
  `/api/dev/notify`; cambio de personalidad desde `/admin`.
- **Integración**: ESP32 ↔ servidor con cada personalidad; Home Assistant (si aplica).
- **No-regresión**: comparar contra el comportamiento de `v1.0.0-foundation`.

## 7. Dependencias que pueden verse afectadas

- **LovyanGFX** (firmware): cambios de render/sprites/fuentes.
- **ArduinoJson** (firmware): si cambia el formato de configuración declarativa.
- **FastAPI / pydantic** (servidor): si se versionan/rediseñan endpoints o modelos.
- **Ollama / Whisper / RVC**: solo si se reorganiza la orquestación (no su lógica interna).

## 8. Orden de migración

**Primero (núcleo, bajo riesgo):**
1. Interfaces/contratos del motor (F0).
2. Adaptadores sobre código existente (F1).
3. Registro/loader de presentaciones y personalidades (F2).

**Después (migración de contenido):**
4. Personalidades declarativas (servidor).
5. Skins de presentación (Bender → KITT).

**Al final (mayor riesgo / limpieza):**
6. Adelgazar `face.h` y `main.py`.
7. Eliminar código antiguo y rutas legacy (solo con el nuevo sistema validado).

---

## 9. Git Flow de la refactorización

```
main  (estable, intocable durante la refactor)
  └── feature/aura-engine        (rama de trabajo de la refactorización)
        ├── commit pequeño (1 mejora concreta)
        ├── commit pequeño …
        └── merge → main  (solo cuando todo funciona y está validado)
```

- Commits pequeños y descriptivos; nada de commits gigantes.
- Toda funcionalidad nueva queda documentada y comentada.
- Merge a `main` únicamente con el sistema funcionando completo.
