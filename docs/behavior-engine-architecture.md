# agenteIA — Behavior Engine Architecture
## Motor de Comportamiento Universal

**Versión:** 1.0  
**Fecha:** 2026-06-28  
**Alcance:** Motor de comportamiento independiente de personalidad  
**Checkpoint:** 8 — commit `02043b6`

---

## Principio rector

> La personalidad expresa **intención**.  
> El motor decide **cómo representarla**.

La personalidad nunca controla el rostro, la voz ni los gestos directamente. Solo emite un paquete de intención semántica. El Behavior Engine traduce esa intención en actuación física observable.

---

## Visión general del sistema

```
┌─────────────────────────────────────────────────────────────┐
│                      PERSONALIDAD                           │
│  (genera texto + intención — nunca controla hardware)       │
└──────────────────────────┬──────────────────────────────────┘
                           │ IntentPacket
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                  BEHAVIOR ENGINE                            │
│                                                             │
│  ┌──────────────┐  ┌───────────────┐  ┌─────────────────┐  │
│  │ M1 Emotional │  │ M4 Context    │  │ M7 Emotional    │  │
│  │    Engine    │◄─┤    System     │◄─┤    Memory       │  │
│  └──────┬───────┘  └───────────────┘  └─────────────────┘  │
│         │                                                   │
│         │  EmotionState                                     │
│         ▼                                                   │
│  ┌──────────────┐  ┌───────────────┐  ┌─────────────────┐  │
│  │ M6 Expres-   │  │ M2 Micro-     │  │ M3 Attention    │  │
│  │    sivity    │  │    expressions│  │    State        │  │
│  └──────┬───────┘  └───────┬───────┘  └────────┬────────┘  │
│         │                  │                   │            │
│         └──────────┬───────┘                   │            │
│                    │ GestureSet                │            │
│                    ▼                           │            │
│  ┌──────────────────────────────────────────┐  │            │
│  │           M5 Rhythm Engine               │◄─┘            │
│  │    (pausas / latencias / respiración)    │               │
│  └──────────────────┬───────────────────────┘               │
│                     │                                        │
│  ┌──────────────────┴───────────────────────┐               │
│  │            M9 Acting Layer               │               │
│  │  (traduce intención → voz+rostro+mirada) │               │
│  └──────────────────┬───────────────────────┘               │
│                     │                                        │
│  ┌──────────────────┴───────────────────────┐               │
│  │         M10 Priority Manager             │               │
│  │  (Guardian / emergencias / transiciones) │               │
│  └──────────────────┬───────────────────────┘               │
│                     │                                        │
└─────────────────────┼───────────────────────────────────────┘
                      │ ActuationPlan
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                   HARDWARE LAYER                            │
│                                                             │
│   TFT Display    │   I2S Audio    │   LEDs    │   Servo    │
│   (face.h)       │   (ES8311)     │           │   (futuro) │
└─────────────────────────────────────────────────────────────┘
```

---

## Contratos de datos

### IntentPacket (Personalidad → Motor)

```json
{
  "text":        "string",
  "intent":      "explain | celebrate | warn | comfort | tease | wonder | ...",
  "tone":        "ironic | worried | proud | curious | flat | ...",
  "energy":      0.0–1.0,
  "urgency":     0.0–1.0,
  "silence_ok":  true | false
}
```

### EmotionState (Motor Emocional → resto del sistema)

```json
{
  "emotion":     "happy | sad | thinking | excited | ...",
  "intensity":   0.0–1.0,
  "duration_ms": 1200,
  "transition":  "snap | smooth | slow_fade",
  "recovery":    "neutral | previous | context_default"
}
```

### ActuationPlan (Motor → Hardware)

```json
{
  "face": {
    "emotion":       "happy",
    "intensity":     0.75,
    "brow_dy":       -2.0,
    "brow_tilt":     0.1,
    "mouth_bow":     0.35,
    "mouth_segs":    8
  },
  "voice": {
    "speed":         1.0,
    "pitch_offset":  0.0,
    "pause_before":  300,
    "pause_after":   150
  },
  "micro": {
    "glance":        "user",
    "blink_rate":    "normal",
    "head_tilt":     0.05
  },
  "timing": {
    "pre_response_ms":  400,
    "post_response_ms": 200
  }
}
```

---

## Módulo 1 — Motor Emocional

### Responsabilidad

Transforma una intención semántica (ej. `"warn"` con `energy=0.8`) en un estado emocional completo con intensidad, duración, transición y recuperación.

### Mapeo intención → emoción

| Intención     | Emoción base  | Intensidad base | Modulada por energy |
|---------------|---------------|-----------------|---------------------|
| `celebrate`   | `happy`       | 0.8             | × energy            |
| `explain`     | `thinking`    | 0.4             | × energy × 0.6      |
| `warn`        | `angry`       | 0.5             | × energy            |
| `comfort`     | `love`        | 0.6             | × energy × 0.8      |
| `tease`       | `cool`        | 0.5             | × energy × 0.7      |
| `wonder`      | `surprised`   | 0.4             | × energy            |
| `flat`        | `neutral`     | 0.2             | fijo                |
| `ponder`      | `thinking`    | 0.25–0.80       | × urgency           |
| `acknowledge` | `neutral`     | 0.3             | × energy × 0.5      |
| `empathize`   | `sad`         | 0.4             | × energy            |

### Intensidades: el mismo estado, tres actuaciones diferentes

```
thinking @ 0.25  →  cejas ligeramente alzadas, boca neutra, mirada lateral suave
thinking @ 0.55  →  cejas más pronunciadas, boca levemente fruncida, pausa antes de hablar
thinking @ 0.85  →  cejas en V marcada, boca cerrada firme, silencio de 800 ms, mirada arriba
```

### Curvas de transición

```
snap       ████████  (0 ms) — sorpresa, susto, comando urgente
smooth     ▁▃▅▇█     (250–400 ms) — conversación normal
slow_fade  ▁▂▃▄▅▆▇█  (600–1200 ms) — transición de contexto, despertar, dormir
```

### Recuperación post-emoción

```
neutral        →  vuelve a cara base
previous       →  vuelve a la emoción que tenía antes
context_default →  vuelve al default del contexto activo (ej. modo "programando" → thinking@0.3)
```

### Diagrama de estado simplificado

```
         IntentPacket
              │
              ▼
     ┌─────────────────┐
     │  Resolver       │  context_default + personality_config
     │  (intent → base)│ ◄───────────────────────────────────
     └────────┬────────┘
              │
              ▼
     ┌─────────────────┐
     │  Modulate       │  energy × expressivity_scale
     │  (intensidad)   │
     └────────┬────────┘
              │
              ▼
     ┌─────────────────┐
     │  Memory check   │  emotional_memory modifica resultado
     │                 │  (ej: sesión difícil → +tristeza −0.1 en happy)
     └────────┬────────┘
              │
              ▼
         EmotionState
```

---

## Módulo 2 — Microexpresiones

### Responsabilidad

Gestos pequeños y automáticos que el motor agrega sin intervención de la personalidad. Hacen que el personaje "respire" visualmente.

### Catálogo de microexpresiones

| ID              | Descripción                          | Duración  | Trigger                        |
|-----------------|--------------------------------------|-----------|--------------------------------|
| `glance_user`   | Giro leve hacia la cámara            | 300 ms    | Usuario habla / saludo         |
| `glance_screen` | Mirada leve hacia el monitor         | 400 ms    | Contexto "programando"         |
| `glance_up`     | Mirada hacia arriba                  | 500 ms    | Pensando / calculando          |
| `soft_blink`    | Parpadeo suave doble                 | 200 ms    | Idle > 4 s sin parpadear       |
| `brow_raise_l`  | Ceja izquierda sola sube             | 350 ms    | Tono irónico / escéptico       |
| `brow_raise_r`  | Ceja derecha sola sube               | 350 ms    | Curiosidad puntual             |
| `half_squint`   | Medio cierre de ojo                  | 400 ms    | Modo nocturno / cansancio      |
| `micro_smile`   | Boca sube 0.08 bow por 600 ms        | 600 ms    | Silencio post-logro            |
| `head_tilt_r`   | Inclinación derecha leve             | 400 ms    | Pregunta / confusión           |
| `sigh_visual`   | Brow cae + boca se abre leve         | 700 ms    | Post-error / espera larga      |
| `sniff`         | Micro-movimiento nariz (futuro)      | 200 ms    | Sorpresa olfativa (placeholder)|
| `focus_lock`    | Mirada directa al frente, sin mover  | 1000 ms   | Urgencia / alerta              |

### Pipeline de selección

```
contexto activo
       +
emoción actual
       +
última microexpresión (evitar repetición)
       +
expressivity_scale
       │
       ▼
  ┌──────────────────────────┐
  │  Micro Expression        │
  │  Scheduler               │
  │                          │
  │  1. Filtra compatibles   │
  │  2. Aplica cooldown      │  cada microexpresión tiene cooldown propio
  │  3. Pondera por contexto │
  │  4. Random weighted pick │
  └────────────┬─────────────┘
               │
        MicroEvent (timing + params)
               │
               ▼
         face.h / AnimLayer
```

### Cooldowns por defecto

| Microexpresión  | Cooldown mínimo |
|-----------------|-----------------|
| `soft_blink`    | 3–6 s           |
| `glance_*`      | 8–15 s          |
| `micro_smile`   | 20 s            |
| `sigh_visual`   | 30 s            |
| `brow_raise_*`  | 5 s             |

---

## Módulo 3 — Estado de Atención (Idle Behavior)

### Responsabilidad

El personaje nunca se ve "apagado". Este módulo controla qué hace el personaje cuando NO está procesando ni respondiendo.

### Máquina de estados Idle

```
                 ┌──────────────────────┐
                 │       ACTIVO         │  (hablando, procesando)
                 └──────────┬───────────┘
                            │ silencio > 2s
                            ▼
                 ┌──────────────────────┐
          ┌──────│     PRESENTE         │──────┐
          │      │  (escucha, observa)  │      │
          │      └──────────────────────┘      │
          │           silencio > 15s           │
          │                                    │
          ▼                                    ▼
┌──────────────────┐               ┌──────────────────────┐
│   EXPLORANDO     │               │      ESPERANDO       │
│  (mira alrededor,│               │  (respira, parpadea, │
│   pantalla,      │               │   micro-movs suaves) │
│   notificaciones)│               └──────────────────────┘
└──────────────────┘                      silencio > 60s
                                               │
                                               ▼
                                    ┌──────────────────────┐
                                    │     SEMI-DORMIDO     │
                                    │  (ojos medio cerrados│
                                    │   respiración lenta) │
                                    └──────────────────────┘
```

### Comportamientos por estado

**PRESENTE**
- Tracking visual del usuario (parpadeo a ritmo normal)
- Microexpresiones de escucha (`glance_user` cada 8–12 s)
- Emoción en baja intensidad (0.2–0.35)

**EXPLORANDO**
- `glance_screen` → `glance_up` → `glance_user` rotando
- Microsonrisas si memory tiene eventos positivos recientes
- Cejas relajadas

**ESPERANDO**
- Breathing animation activo (visor)
- Soft blink cada 4–5 s
- Emoción: `neutral @ 0.15`
- Silencio total salvo interrupciones externas

**SEMI-DORMIDO**
- `half_squint` sostenido
- Breathing lento (periodo 6s en lugar de 4s)
- No responde a contextos no urgentes
- Wake trigger: voz, toque, notificación urgente

---

## Módulo 4 — Sistema de Contexto

### Responsabilidad

Un contexto activo modifica globalmente el comportamiento de todos los módulos. Es una "escena" que define tono, energía y foco de atención.

### Tabla de contextos

| Contexto        | Emoción default    | Expressivity | Ritmo     | Microexp. frecuentes     | Silencios |
|-----------------|--------------------|--------------|-----------|--------------------------|-----------|
| `idle`          | neutral @ 0.2      | config       | normal    | glance_user, soft_blink  | libres    |
| `programming`   | thinking @ 0.35    | 0.4          | lento     | glance_screen, glance_up | permitidos|
| `music`         | happy @ 0.6        | 0.8          | rítmico   | micro_smile, head_tilt   | musicales |
| `domotics`      | neutral @ 0.3      | 0.3          | preciso   | focus_lock               | mínimos   |
| `camera_watch`  | thinking @ 0.25    | 0.25         | lento     | glance_up, brow_raise_r  | frecuentes|
| `waiting`       | neutral @ 0.2      | 0.2          | muy lento | soft_blink               | libres    |
| `compiling`     | thinking @ 0.5     | 0.45         | tenso     | sigh_visual              | permitidos|
| `gaming`        | excited @ 0.7      | 0.9          | rápido    | brow_raise_l, micro_smile| mínimos   |
| `night_mode`    | sleepy @ 0.4       | 0.25         | muy lento | half_squint, soft_blink  | libres    |
| `emergency`     | angry @ 0.8        | 0.6          | urgente   | focus_lock               | prohibidos|
| `visitor`       | happy @ 0.55       | 0.75         | social    | glance_user, micro_smile | breves    |

### API de cambio de contexto

```
setContext(name, transition="smooth", duration_ms=600)
  → emite ContextChange event
  → todos los módulos leen nuevo config
  → M5 Rhythm Engine ajusta timing
  → M3 Idle reconfigura thresholds
  → M10 prioriza si es emergencia
```

### Herencia y stack de contextos

Los contextos se apilan. Un contexto "visitante" puede existir sobre un contexto "programando":

```
stack: [programming, visitor]
resultado: expresividad del visitor + foco del programming
```

Al desapilarse `visitor`, el comportamiento vuelve gradualmente a `programming`.

---

## Módulo 5 — Ritmo de Conversación

### Responsabilidad

Elimina la sensación robótica. Controla tiempos antes, durante y después de hablar.

### Parámetros de timing

```
pre_response_delay    tiempo entre que "escucha" y empieza a responder
                      rango: 200–1200 ms (depende de urgency + context)

intra_sentence_pause  pausa entre oraciones largas
                      rango: 100–400 ms

post_response_pause   silencio después de hablar antes de volver a idle
                      rango: 150–600 ms

thinking_breath       respiración visual visible mientras "piensa"
                      activa si pre_response_delay > 500 ms

word_pace             multiplicador de velocidad TTS
                      rango: 0.7–1.3×
```

### Tabla de ritmo por contexto + energía

| Contexto       | Energy | pre_ms | intra_ms | post_ms | word_pace |
|----------------|--------|--------|----------|---------|-----------|
| `idle`         | 0.5    | 400    | 200      | 300     | 1.0×      |
| `programming`  | 0.4    | 600    | 300      | 400     | 0.95×     |
| `gaming`       | 0.9    | 150    | 80       | 100     | 1.15×     |
| `night_mode`   | 0.2    | 900    | 400      | 600     | 0.85×     |
| `emergency`    | 1.0    | 80     | 40       | 50      | 1.2×      |
| `visitor`      | 0.7    | 300    | 150      | 250     | 1.05×     |

### Breathing visual

Mientras `pre_response_delay > 500 ms`, el visor (drawCapsule) activa el modo "pensando": amplitud de breathing aumenta 30%, periodo se acorta a 3 s, cejas suben levemente.

---

## Módulo 6 — Expresividad

### Responsabilidad

Una escala global 0.0–1.0 que amplifica o atenúa todos los outputs del motor. La personalidad configura su valor base; el contexto puede modificarlo temporalmente.

### Escala de efectos

| Nivel | mouth_bow mult | brow_dy mult | microexp rate | pre_delay mult | voz         |
|-------|---------------|--------------|---------------|----------------|-------------|
| 0.0   | 0.1×          | 0.05×        | nunca         | 1.5×           | monotónica  |
| 0.25  | 0.4×          | 0.3×         | rara           | 1.2×          | suave       |
| 0.5   | 1.0× (base)   | 1.0× (base)  | normal         | 1.0×          | normal      |
| 0.75  | 1.4×          | 1.5×         | frecuente      | 0.85×         | expresiva   |
| 1.0   | 1.8×          | 2.0×         | muy frecuente  | 0.7×          | teatral     |

### Interacción con intensidad emocional

```
face_output = base_value × expressivity_scale × intensity
```

Ejemplo:
```
happy base bow = 0.40
intensity = 0.75
expressivity = 0.6

face_output bow = 0.40 × 0.6 × 0.75 = 0.18   (sonrisa discreta)

mismo con expressivity = 1.0:
face_output bow = 0.40 × 1.0 × 0.75 = 0.30   (sonrisa clara)
```

---

## Módulo 7 — Memoria Emocional

### Responsabilidad

Recuerda eventos recientes para que el comportamiento refleje el "estado acumulado" de la sesión. Sin datos sensibles, sin texto del usuario, solo eventos de tipo.

### Tipos de evento (solo metadatos)

```
event_type: compile_error | compile_ok | long_wait | music_played |
            voice_interaction | silence_long | context_switch |
            emotion_peak | night_started | visitor_arrived
```

### Estructura del evento

```json
{
  "type":       "compile_error",
  "timestamp":  1234567890,
  "severity":   0.0–1.0,
  "expires_in": 1800
}
```

**No se almacena**: texto del usuario, comandos específicos, datos personales.

### Efecto sobre el Motor Emocional

```
memoria reciente          efecto en M1
─────────────────         ──────────────────────────────────────────
3+ compile_error          baja intensity máx de happy en 0.15
                          sube baseline de thinking en 0.1
                          habilita sigh_visual más frecuente

long_wait reciente        aumenta pre_response_delay en 20%
                          activa micro_smile post-respuesta

music_played reciente     sube expressivity base en 0.1 por 10 min

night_started             activa night_mode context gradualmente

visitor_arrived           boost happy baseline +0.2, expressivity +0.15
```

### Ventana de memoria

La memoria es una ventana deslizante de **1 hora** máximo con **50 eventos máximo**. Los eventos tienen peso decreciente según edad:

```
peso = severity × exp(−age_minutes / decay_constant)
decay_constant: 20 min (por defecto, configurable)
```

---

## Módulo 8 — Reglas de Silencio

### Responsabilidad

El silencio es una herramienta expresiva. Define cuándo el personaje NO debe hablar.

### Tabla de reglas de silencio

| Regla                      | Condición                                          | Duración silencio |
|----------------------------|----------------------------------------------------|-------------------|
| `post_complex_answer`      | Respuesta > 4 oraciones                            | 400–700 ms        |
| `post_emotion_peak`        | Emoción intensity > 0.85                           | 600–900 ms        |
| `rhetorical_question`      | intent = `wonder` + silence_ok = true              | 1200–2000 ms      |
| `dramatic_pause`           | tone = `proud` antes de dato importante            | 300–600 ms        |
| `error_acknowledgment`     | compile_error o similar en memoria reciente        | 500–800 ms        |
| `night_mode_transition`    | Contexto cambia a night_mode                       | 1500–2500 ms      |
| `respect_user_pause`       | Usuario dejó de hablar pero micrófono sigue abierto| indefinido        |
| `context_switch`           | Cambio de contexto importante                      | 200–400 ms        |
| `guardian_takeover`        | M10 activa protocolo de emergencia                 | 0 ms (corte)      |

### Reglas de NO silencio

Situaciones donde el silencio está prohibido:
- `urgency > 0.8`
- `emergency` context activo
- `visitor_arrived` evento en los últimos 5 s

---

## Módulo 9 — Capa de Actuación (Acting Layer)

### Responsabilidad

Traduce el tono e intención de la personalidad en parámetros concretos de voz, rostro y mirada. Es el "director de actuación" del sistema.

### Tonos reconocidos y su mapeo

| Tono          | Voz (speed, pitch) | Rostro                          | Mirada              |
|---------------|--------------------|---------------------------------|---------------------|
| `ironic`      | speed+5%, pitch−2% | brow_raise_l + half_squint_r    | glance lateral      |
| `worried`     | speed−8%, pitch−4% | brow tilt negativo, boca abierta| glance_up           |
| `proud`       | speed−3%, pitch+3% | brow alto, boca sonrisa firme   | focus_lock al frente|
| `curious`     | speed+2%, pitch+2% | brow_raise_r, boca abierta leve | head_tilt_r         |
| `flat`        | speed 0%, pitch 0% | neutral @ 0.15                  | sin movimiento      |
| `excited`     | speed+15%,pitch+6% | happy @ 0.9, brow alto          | glance_user rápido  |
| `empathetic`  | speed−5%, pitch−2% | love @ 0.5, brow suave          | glance_user sostenido|
| `sarcastic`   | speed+3%, pitch 0% | cool @ 0.6, half_squint_l       | glance lateral lento|
| `urgent`      | speed+20%,pitch+8% | angry @ 0.7, brow en V          | focus_lock          |

### Flujo de la capa de actuación

```
IntentPacket.tone + IntentPacket.intent
              │
              ▼
    ┌─────────────────────┐
    │  Tone Resolver      │  → VoiceParams
    │                     │  → FaceOverlay
    │                     │  → GazePlan
    └──────────┬──────────┘
               │
               ▼
    ┌─────────────────────┐
    │  Blend with         │
    │  EmotionState       │  prioridad: tono > emoción si urgency > 0.6
    └──────────┬──────────┘
               │
               ▼
         ActuationPlan
```

---

## Módulo 10 — Cambio de Prioridad

### Responsabilidad

Cuando un sistema externo (Guardian, alerta de seguridad, emergencia) toma control, la personalidad NO desaparece. Solo se suspende temporalmente y el motor gestiona la transición.

### Estados de prioridad

```
NORMAL     →   motor corre completo, personalidad activa
PROTOCOL   →   Guardian activo, motor en modo restringido
RECOVERY   →   Guardian liberó, motor vuelve gradualmente
```

### Transición NORMAL → PROTOCOL

```
t=0 ms     Guardian emite takeover()
t=0 ms     M5 Rhythm: silencio inmediato, corte de TTS si está en curso
t=50 ms    M1 Emotional: snap a emergency@0.8
t=50 ms    M2 Micro: solo focus_lock permitido
t=100 ms   M4 Context: push "emergency" al stack
t=100 ms   M9 Acting: tone forzado a "urgent"
```

### Transición PROTOCOL → RECOVERY

```
t=0 ms     Guardian emite release()
t=0 ms     M10 inicia recovery timer (default: 3000 ms)
t=0 ms     M4 Context: pop "emergency" del stack
t=500 ms   M1 Emotional: slow_fade al context_default anterior
t=1000 ms  M2 Micro: reactivar microexpresiones gradualmente
t=2000 ms  M5 Rhythm: volver a timings normales
t=3000 ms  M9 Acting: tone vuelve a personalidad
t=3000 ms  M3 Idle: reactivar estado de atención normal
```

### Diagrama de prioridades

```
Prioridad 1 (máxima)   EMERGENCY / GUARDIAN
Prioridad 2            VISITOR (override expresividad)
Prioridad 3            USER CONTEXT (programming, gaming, etc.)
Prioridad 4            EMOTIONAL_MEMORY modifiers
Prioridad 5 (base)     PERSONALITY config
```

---

## Módulo 11 — Configuración

### Responsabilidad

Todo el motor es configurable por personalidad. Ningún valor es fijo en el núcleo.

### Schema de configuración de personalidad

```json
{
  "personality_id": "string",
  "behavior": {
    "expressivity":          0.0–1.0,
    "energy_baseline":       0.0–1.0,
    "humor_level":           0.0–1.0,
    "sarcasm_enabled":       true | false,
    "silence_comfort":       0.0–1.0,
    "initiative_level":      0.0–1.0,
    "observation_rate":      0.0–1.0,
    "idle_remarks_enabled":  true | false,
    "idle_remarks_interval": 60–600,
    "voice_speed_base":      0.7–1.3,
    "emotion_recovery_ms":   500–3000,
    "memory_decay_minutes":  5–60,
    "microexp_rate":         0.0–1.0,
    "night_mode_auto":       true | false
  },
  "context_overrides": {
    "programming": { "expressivity": 0.3 },
    "gaming":      { "expressivity": 1.0, "energy_baseline": 0.9 }
  },
  "emotion_biases": {
    "happy":    +0.1,
    "thinking": −0.05
  }
}
```

### Valores por defecto del motor (sin config de personalidad)

| Parámetro             | Default |
|-----------------------|---------|
| expressivity          | 0.5     |
| energy_baseline       | 0.5     |
| humor_level           | 0.4     |
| sarcasm_enabled       | false   |
| silence_comfort       | 0.5     |
| initiative_level      | 0.4     |
| observation_rate      | 0.3     |
| voice_speed_base      | 1.0×    |
| emotion_recovery_ms   | 1200    |
| memory_decay_minutes  | 20      |
| microexp_rate         | 0.6     |
| night_mode_auto       | true    |

---

## Módulo 12 — Extensibilidad

### Principios de diseño

1. **Open/Closed**: Agregar nuevas emociones, gestos o contextos sin modificar el núcleo.
2. **Dependency Inversion**: El núcleo depende de interfaces, no de implementaciones concretas.
3. **Single Responsibility**: Cada módulo tiene una sola razón para cambiar.
4. **Interface Segregation**: El hardware layer recibe solo lo que necesita del ActuationPlan.

### Puntos de extensión

```
EMOCIONES
  Definir en emotions.json:
    id, display_name, mouth_bow, brow_preset, eye_state

MICROEXPRESIONES
  Definir en microexpressions.json:
    id, face_params, cooldown_ms, compatible_emotions[], context_weights{}

CONTEXTOS
  Definir en contexts.json:
    id, emotion_default, expressivity, rhythm_preset, microexp_whitelist[]

TONOS
  Definir en tones.json:
    id, voice_delta{speed,pitch}, face_overlay{}, gaze_behavior

INTENCIONES
  Definir en intents.json:
    id, emotion_mapping{}, tone_compatibility[]
```

### Estructura de carpetas propuesta

```
behavior-engine/
├── core/
│   ├── emotional_engine.py      # M1
│   ├── microexpressions.py      # M2
│   ├── attention_state.py       # M3
│   ├── context_system.py        # M4
│   ├── rhythm_engine.py         # M5
│   ├── expressivity.py          # M6
│   ├── emotional_memory.py      # M7
│   ├── silence_rules.py         # M8
│   ├── acting_layer.py          # M9
│   └── priority_manager.py      # M10
├── config/
│   ├── emotions.json
│   ├── microexpressions.json
│   ├── contexts.json
│   ├── tones.json
│   └── intents.json
├── interfaces/
│   ├── intent_packet.py
│   ├── emotion_state.py
│   └── actuation_plan.py
└── hardware_bridge/
    ├── face_bridge.py           # → face.h
    ├── voice_bridge.py          # → TTS
    └── led_bridge.py            # → LEDs
```

---

## Ejemplo de flujo completo

**Escenario**: Usuario dice "¡compilé por primera vez!" en contexto `programming`.

```
1. IntentPacket recibido:
   {
     intent: "celebrate",
     tone: "proud",
     energy: 0.8,
     urgency: 0.2,
     silence_ok: true
   }

2. M4 Context activo: programming
   → expressivity_override = 0.4 → pero celebrate lo sube a 0.65 temporal

3. M7 Memoria: 2 compile_error anteriores en sesión
   → aplica +0.05 a intensity (el logro importa más)

4. M1 Motor Emocional:
   celebrate → happy
   intensity = 0.8 × energy × 0.65 (expressivity) × 1.05 (memory boost) = 0.55
   duration = 1800 ms
   transition = smooth
   recovery = context_default (thinking@0.35)

5. M9 Acting Layer:
   tone "proud" →
     pre_response_delay: 400 ms (pausa dramática)
     face_overlay: brow_dy = +1.8, brow_tilt = -0.05 (orgulloso)
     voice: speed × 0.97, pitch +3%

6. M2 Microexpresiones (scheduladas durante la respuesta):
   t=0    → glance_user (mira al usuario al empezar)
   t=600  → micro_smile (0.08 bow extra por 500 ms)
   t=1800 → focus_lock breve (0.4 s)

7. M5 Ritmo:
   pre_response_delay = 400 ms (desde M9) → activa thinking_breath visual
   post_response_pause = 350 ms

8. M8 Silencios:
   silence_ok=true + tone=proud → aplica dramatic_pause de 450 ms antes de dato clave

9. M3 Idle: suspendido durante respuesta activa → se reactiva post-response

10. ActuationPlan emitido a hardware:
    face:  happy @ 0.55, bow=0.22, brow_dy=+1.8
    voice: speed=0.97×, pitch=+3%, pre=400ms, post=350ms
    micro: [glance_user t=0, micro_smile t=600, focus_lock t=1800]
    silence: dramatic_pause 450ms en mid-sentence

11. M1 Recovery:
    t=1800ms → slow_fade back to thinking@0.35 (context_default)

12. M7 Memoria actualiza:
    evento: compile_ok, severity=0.9, expires_in=1800s
    → próxima sesión: happy más fácil de activar, sigh_visual menos frecuente
```

---

## Recomendaciones de implementación

### Fase 1 — Núcleo mínimo viable
Implementar M1 (Motor Emocional) + M6 (Expresividad) + M11 (Config).  
El resto del sistema puede funcionar con placeholders.

### Fase 2 — Vida en reposo
Agregar M3 (Idle Behavior) + M2 (Microexpresiones básicas: blink, glance).  
El personaje ya se ve vivo sin necesidad de conversación.

### Fase 3 — Contexto
Implementar M4 (Contexto) + M5 (Ritmo).  
El personaje cambia de "mood" según lo que está haciendo.

### Fase 4 — Memoria y actuación
Agregar M7 (Memoria Emocional) + M9 (Acting Layer).  
Las sesiones se sienten continuas y el personaje "actúa" los tonos.

### Fase 5 — Silencios y prioridades
Agregar M8 (Silencios) + M10 (Prioridades).  
El personaje sabe cuándo no hablar y cómo manejar interrupciones.

### Recomendaciones técnicas para ESP32

- Todos los parámetros del ActuationPlan caben en <200 bytes (struct C++)
- M2 y M3 corren en el main loop con timers de bajo overhead
- M7 (Memoria) se almacena en PSRAM (8 MB disponibles)
- M11 (Config) se carga desde JSON en SPIFFS al arrancar
- M10 (Priority) usa un simple enum + atomic flag, sin locks
- Los módulos de "intención" (M9) corren server-side en Python

### Recomendaciones técnicas para el servidor Python

- IntentPacket se añade al response JSON de `/converse` como campo `"behavior": {...}`
- El Behavior Engine principal corre en Python (más fácil iterar, sin reflashear)
- El ESP solo implementa el hardware bridge: recibe ActuationPlan y lo ejecuta
- Usar un canal WebSocket o polling liviano para eventos de contexto

---

## Resumen de interfaces entre módulos

```
IntentPacket      →  M1, M9
EmotionState      →  M2, M5, M6, M8, M9
ContextState      →  M1, M2, M3, M5, M6, M8
MemorySnapshot    →  M1, M2
PriorityLevel     →  M1, M2, M3, M5, M8, M9
PersonalityConfig →  M1, M2, M3, M5, M6, M7, M8, M9
ActuationPlan     →  Hardware (face.h, TTS, LEDs)
```

---

*Documento generado: 2026-06-28 — agenteIA behavior engine v1.0*  
*Este documento describe la arquitectura. No modifica ninguna personalidad existente.*
