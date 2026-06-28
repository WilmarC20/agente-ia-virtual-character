// Pin map for the Hosyond ES3C28P board (ESP32-S3, 2.8" 240x320 IPS, ES8311 codec).
// Source: official schematic in docs/datasheets/es3c28p-schematic.pdf
#pragma once

// --- Display (ILI9341 over SPI) ---
#define PIN_LCD_SCLK 12
#define PIN_LCD_MOSI 11
#define PIN_LCD_MISO 13
#define PIN_LCD_CS   10
#define PIN_LCD_DC   46
#define PIN_LCD_RST  -1  // tied to board reset
#define PIN_LCD_BL   45

// 0 = vertical 240x320 | 2 = vertical 180° | 1 = horizontal 320x240 (ancho)
#define DISPLAY_ROTATION  1

// --- Touch (FT6336, shares I2C bus) ---
#define PIN_I2C_SDA  16
#define PIN_I2C_SCL  15
#define PIN_TOUCH_INT 17
#define FT6336_I2C_ADDR 0x38
#if DISPLAY_ROTATION == 0
#define TOUCH_SWAP_XY   0
#define TOUCH_INVERT_X  0
#define TOUCH_INVERT_Y  0
#elif DISPLAY_ROTATION == 2
#define TOUCH_SWAP_XY   0
#define TOUCH_INVERT_X  1
#define TOUCH_INVERT_Y  1
#else
// Rot.1 landscape: el piñón (arriba-der) se activaba tocando abajo-izq -> esquina opuesta
// = ambos ejes invertidos. Corregido (antes INVERT_X 0 / INVERT_Y 1).
#define TOUCH_SWAP_XY   1
#define TOUCH_INVERT_X  1
#define TOUCH_INVERT_Y  0
#endif
#define TOUCH_DEBUG     0

// --- Audio (ES8311 codec: I2C control + I2S data) ---
#define ES8311_I2C_ADDR 0x18
#define PIN_I2S_MCLK 4
#define PIN_I2S_BCLK 5
#define PIN_I2S_WS   7
#define PIN_I2S_DIN  6   // microphone (codec ADC -> ESP32)
#define PIN_I2S_DOUT 8   // speaker (ESP32 -> codec DAC)
#define PIN_SPK_EN   1   // speaker amplifier enable, active LOW

// --- Misc ---
#define PIN_RGB_LED  42  // WS2812, 1 LED
#define PIN_BAT_ADC  9
#define ENABLE_RGB_LED 1 // ambient glow on the WS2812 that matches the emotion + breathes

// --- Audio capture ---
// ES8311: captura I2S estéreo y usa el canal con más señal (L/R).
#define MIC_I2S_MODE        I2S_SLOT_MODE_STEREO
#define MIC_I2S_SLOT        I2S_STD_SLOT_LEFT
#define SPK_I2S_SLOT        I2S_STD_SLOT_LEFT
#define MIC_PCM_GAIN        6       // software boost before WAV (peak was ~1500)
#define TTS_VOLUME_PERCENT  80      // ES8311 DAC (100 = max)
#define WAKE_COOLDOWN_MS    4000    // ignore WakeNet after TTS (speaker echo)
#define RECORD_NO_VOICE_MS  3000    // abort capture if no speech detected
#define I2S_DRAIN_MS              300
#define I2S_DRAIN_AFTER_PLAY_MS   500
#define SR_PAUSE_SETTLE_MS        280
// WakeNet (ESP-SR): "Hi ESP" on-device. Requires ESP SR 16M partition scheme.
// Kept compiled so we can RETAKE it later; the board mic is too quiet for it to
// fire reliably today, so the active wake path is WAKE_MODE_PC below.
#define ENABLE_WAKEWORD     1
// Wake path selector: 1 = PC-side "Hola asistente" (records a clip, server
// /wake-check transcribes it with Whisper). 0 = on-device WakeNet "Hi ESP".
// Touch-to-wake works in BOTH modes.
#define WAKE_MODE_PC        1
// PC-wake energy gate: only record+POST a /wake-check clip when the raw mic peak
// crosses this. Silence stays cheap (~80 ms) so the touch poll stays responsive —
// the blocking record+HTTP only runs when there's actually sound. Tune from the
// "wake-listen ambient peak" serial log (set it ~2-3x the idle ambient peak).
#define WAKE_LISTEN_PEAK    2200   // subir si hay falsos positivos (ver log ambient peak)
#define WAKE_REJECT_COOLDOWN_MS 3500 // tras wake-check fallido, ignorar ruido breve
// A sudden sound at least this loud makes the character glance (surprised) even if
// it's not the wake phrase — cheap ambient reactivity. Tune above WAKE_LISTEN_PEAK.
#define STARTLE_PEAK        4500
// How often (ms) the PC-wake energy gate runs. Between probes the loop is free, so
// touch stays responsive. The gate itself is ~60 ms; the rest of the interval idles.
#define WAKE_PROBE_INTERVAL_MS  200
// /wake-check HTTP cap. A short clip transcribes fast on GPU, so keep this low: a
// server stall must never freeze the touch loop for long.
#define WAKE_PROBE_NO_VOICE_MS  1500   // pausa breve entre wake y comando
#define WAKE_CHECK_TIMEOUT_MS   8000
// /tts con RVC en PC tarda 15-90s (guia + conversion). Sin esto: TTS fallo -11.
#define TTS_HTTP_TIMEOUT_MS     180000
// /music/play: stream yt-dlp|ffmpeg desde PC; timeout = duración canción + margen.
#define MUSIC_HTTP_TIMEOUT_MS   600000
// Proactive idle: after this long in Sleeping with no interaction, the character
// says something on its own (POST /idle). Re-armed with a random extra each time.
#define ENABLE_IDLE_REMARKS 0
#define IDLE_REMARK_MS      120000
// ES8311 places the mono mic on ONE stereo slot (L or R). WakeNet's input_format
// must point at that slot or it hears silence. Auto-detected at boot by energy.
// Fallback if probe is inconclusive; "MN" = mic LEFT, "NM" = mic RIGHT.
#define WAKEWORD_INPUT_FORMAT "MN"
// Uncomment to skip auto-detect and force a slot, e.g. if the probe misreads:
// #define WAKEWORD_INPUT_FORMAT_FORCE "NM"
// User sound effects live on the "spiffs" partition (NOT the "model" partition).
#define SPIFFS_PARTITION_LABEL "spiffs"
#define I2S_MCLK_MULTIPLE   384     // ES8311 on ES3C28P: 16 kHz * 384 = 6.144 MHz MCLK
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_MCLK_HZ       (AUDIO_SAMPLE_RATE * I2S_MCLK_MULTIPLE)
// 0 = solo habla (TTS edge). 1 = intenta /agent/sing (Bark+RVC, muy lento).
#define ENABLE_SINGING      0
#define RECORD_MAX_SECONDS  8
#define RECORD_SILENCE_MS   1200   // stop after this much trailing silence
#define RECORD_MIN_MS       600    // ignore recordings shorter than this
