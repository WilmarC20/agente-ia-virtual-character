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

// --- Touch (FT6336, shares I2C bus) ---
#define PIN_I2C_SDA  16
#define PIN_I2C_SCL  15
#define PIN_TOUCH_INT 17
#define FT6336_I2C_ADDR 0x38

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

// --- Audio capture ---
// ES8311: captura I2S estéreo y usa el canal con más señal (L/R).
#define MIC_I2S_MODE        I2S_SLOT_MODE_STEREO
#define MIC_I2S_SLOT        I2S_STD_SLOT_LEFT
#define SPK_I2S_SLOT        I2S_STD_SLOT_LEFT
#define MIC_PCM_GAIN        6       // software boost before WAV (peak was ~1500)
#define TTS_VOLUME_PERCENT  70      // ES8311 DAC (100 = max)
#define WAKE_COOLDOWN_MS    4000    // ignore WakeNet after TTS (speaker echo)
#define RECORD_NO_VOICE_MS  3000    // abort capture if no speech detected
#define I2S_DRAIN_MS              300
#define I2S_DRAIN_AFTER_PLAY_MS   500
#define SR_PAUSE_SETTLE_MS        280
// WakeNet (ESP-SR): "Hi ESP" on-device. Requires ESP SR 16M partition scheme.
#define ENABLE_WAKEWORD     1
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
#define RECORD_MAX_SECONDS  8
#define RECORD_SILENCE_MS   1200   // stop after this much trailing silence
#define RECORD_MIN_MS       600    // ignore recordings shorter than this
