// agenteIA - virtual character with emotions on a Hosyond ES3C28P (ESP32-S3).
//
// Wake: touch OR WakeNet "Hi ESP" (ESP-SR, on-device) ->
// record speech -> /converse -> show emotion + TTS.

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP_I2S.h>

#include "config.h"
#if ENABLE_WAKEWORD
#include <ESP_SR.h>
#if !(defined(CONFIG_MODEL_IN_FLASH) || defined(CONFIG_MODEL_IN_SDCARD))
#error WakeNet needs Partition Scheme "ESP SR 16M (3MB APP/7MB SPIFFS/2.9MB MODEL)"
#endif
#endif

#include "secrets.h"
#include "display_setup.h"
#include "face.h"
#include "es8311.h"
#include "touch.h"
#include "settings.h"
#include "audio_recorder.h"
#include "audio_output.h"
#include "sound_fx.h"
#include <driver/i2s_common.h>

LGFX_ES3C28P gfx;
Face face(gfx);
ES8311 codec;
I2SClass i2s;
AudioRecorder recorder;
AppSettings g_settings;
SettingsScreen g_settingsScreen;
uint8_t g_wakePhraseIdx = 0;    // index into WAKE_PRESET_LABELS, sent to /wake-check

enum class State { Sleeping, Listening, Thinking };
volatile bool wakeDetected = false;
State state = State::Sleeping;
uint32_t emotionHoldUntil = 0;
uint32_t wakeIgnoreUntil = 0;
uint32_t lastActivityMs = 0;    // last interaction; drives proactive idle remarks
bool g_wakeNetRunning = false;  // true only while on-device WakeNet is actively feeding
bool g_voiceWakeEnabled = true; // PC-side "Hola asistente" listener; toggled from Settings

#if WAKE_MODE_PC
#define WAKE_HINT "Di \"Hola asistente\" o toca"
#else
#define WAKE_HINT "Di \"Hi ESP\" o toca la pantalla"
#endif

#if ENABLE_WAKEWORD
// WakeNet input format: which stereo slot carries the mic. Set at boot by
// detectMicInputFormat(). "MN" = mic LEFT, "NM" = mic RIGHT.
static const char *g_srInputFormat = WAKEWORD_INPUT_FORMAT;

static const sr_cmd_t srCommands[] = {};

// The ES8311 mono ADC sits on ONE I2S slot; the other reads ~silence. WakeNet
// must listen to the active slot. Probe the noise floor of each channel at boot
// and pick the louder one. Returns "MN" (mic left) or "NM" (mic right).
const char *detectMicInputFormat(I2SClass &i2s) {
#ifdef WAKEWORD_INPUT_FORMAT_FORCE
  Serial.printf("Mic format forced: %s\n", WAKEWORD_INPUT_FORMAT_FORCE);
  return WAKEWORD_INPUT_FORMAT_FORCE;
#else
  int16_t buf[512 * 2];  // stereo frames
  uint64_t accL = 0, accR = 0;
  size_t frames = 0;
  uint32_t deadline = millis() + 400;
  i2s.setTimeout(100);
  while (frames < 1600 && (int32_t)(deadline - millis()) > 0) {
    size_t n = i2s.readBytes((char *)buf, sizeof(buf));
    size_t got = n / 4;
    for (size_t i = 0; i < got; i++) {
      accL += (uint32_t)abs(buf[i * 2]);
      accR += (uint32_t)abs(buf[i * 2 + 1]);
    }
    if (got == 0) delay(5);
    frames += got;
  }
  i2s.setTimeout(1000);
  bool micRight = accR > accL;
  const char *fmt = micRight ? "NM" : "MN";
  Serial.printf("Mic probe: L=%llu R=%llu frames=%u -> mic on %s (%s)\n",
                (unsigned long long)accL, (unsigned long long)accR,
                (unsigned)frames, micRight ? "RIGHT" : "LEFT", fmt);
  return fmt;
#endif
}

void onSrEvent(sr_event_t event, int command_id, int phrase_id) {
  (void)phrase_id;
  if (event == SR_EVENT_WAKEWORD || event == SR_EVENT_WAKEWORD_CHANNEL) {
    Serial.printf("WakeNet: event=%d ch=%d\n", event, command_id);
    wakeDetected = true;
  }
}
#endif

bool touchPressed() {
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)FT6336_I2C_ADDR, (uint8_t)1);
  if (!Wire.available()) return false;
  return (Wire.read() & 0x0F) > 0;
}

void onMicLevel(uint32_t rms) {
  face.drawMicLevel(rms);
}

void pauseWakeListener() {
#if ENABLE_WAKEWORD
  if (g_wakeNetRunning) {
    ESP_SR.pause();
    delay(SR_PAUSE_SETTLE_MS);
  }
#endif
}

bool verifyMicRx(I2SClass &i2s) {
  if (i2s.rxChan() == nullptr) return false;
  char probe[64];
  size_t got = 0;
  esp_err_t e = i2s_channel_read(i2s.rxChan(), probe, sizeof(probe), &got, 200);
  return e == ESP_OK || e == ESP_ERR_TIMEOUT;
}

// After mono-TX playback, restore the mic bus with a CLEAN driver re-init so the
// I2SClass internal state (sample rate, slot, transform, channel handles) matches
// the known-good boot config. Raw channel reconfig left I2SClass out of sync and the
// 2nd capture saw "channel not enabled". i2s.end()/begin() itself never hung — the
// post-TTS hang was the EXTRA ESP_SR.end()/begin() rebuild, which we no longer do:
// ESP_SR reads through the I2SClass object, so resume() alone picks up the fresh
// handle. ESP_SR is paused here (speak() paused it), so there's no concurrent read.
bool restoreMicBusAfterPlayback(I2SClass &i2s) {
  Serial.println("I2S restore for mic...");

  // i2s.end() disables each channel THEN deletes it, but bails (I2S_ERROR_CHECK_RETURN
  // returns on any error) if the disable fails — and here both channels are already
  // disabled (preparePlayback/endPlayback), so i2s_del_channel never runs and the I2S
  // port LEAKS. The ESP32-S3 has only 2 ports, so it died on the 3rd conversation.
  // Re-enable both first so end() can disable+delete cleanly and free the port.
  if (i2s.txChan() && !g_i2sTxRunning) {
    safeChanEnable(i2s.txChan());
    g_i2sTxRunning = true;
  }
  if (i2s.rxChan() && !g_i2sRxRunning) {
    safeChanEnable(i2s.rxChan());
    g_i2sRxRunning = true;
  }

  i2s.end();
  delay(20);
  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  i2s.setTimeout(1000);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, MIC_I2S_MODE)) {
    Serial.println("I2S restore begin failed");
    g_i2sTxRunning = false;
    g_i2sRxRunning = false;
    return false;
  }
  g_i2sTxRunning = true;   // begin() enables both channels
  g_i2sRxRunning = true;

  applyI2sStdClock(i2s, AUDIO_SAMPLE_RATE, (i2s_mclk_multiple_t)I2S_MCLK_MULTIPLE);
  codec.configureClock(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE);

  bool ok = verifyMicRx(i2s);
  if (ok) drainI2sRx(i2s, I2S_DRAIN_AFTER_PLAY_MS);
  Serial.printf("I2S mic restore %s (RX=%d)\n", ok ? "OK" : "FAILED", g_i2sRxRunning);
  return ok;
}

void resumeWakeListener() {
#if ENABLE_WAKEWORD
  if (!g_wakeNetRunning) return;  // PC wake mode: nothing to resume
  // ESP_SR holds a pointer to the I2SClass OBJECT and reads via i2s->readBytes(),
  // which resolves the current rx channel on every call. So even though restore did
  // i2s.end()/begin() and swapped the underlying handle, resume() picks it up — no
  // ESP_SR.end()/begin() rebuild needed (that rebuild was the post-TTS hang).
  if (!verifyMicRx(i2s)) {
    Serial.println("WakeNet: RX probe failed");
    return;
  }
  ESP_SR.resume();
  ESP_SR.setMode(SR_MODE_WAKEWORD);
#endif
}

bool askBrain(const uint8_t *wav, size_t size, String &emotion, String &reply,
              bool &sing, bool &doSpeak, String &soundEffect);
void speak(const String &text, bool sing);
bool checkWakePhrase(const uint8_t *wav, size_t size);
void proactiveIdleRemark();
void openSettings();

void setup() {
  Serial.begin(115200);

  gfx.init();
  gfx.setRotation(1);
  gfx.setBrightness(200);
  face.begin();
  face.setEmotion(Emotion::Sleepy);
  face.update();
  face.showText("Conectando WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK: %s\n", WiFi.localIP().toString().c_str());

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!codec.begin(Wire)) {
    Serial.println("ES8311 not found!");
    face.showText("Error: codec de audio no responde");
  }
  pinMode(PIN_SPK_EN, OUTPUT);
  digitalWrite(PIN_SPK_EN, HIGH);

  i2s.setPins(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, PIN_I2S_MCLK);
  i2s.setTimeout(1000);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                 MIC_I2S_MODE)) {
    Serial.println("I2S init failed!");
  }
  syncI2sRunningFlags(i2s);
  g_i2sTxRunning = true;
  g_i2sRxRunning = true;
  if (applyI2sStdClock(i2s, AUDIO_SAMPLE_RATE, (i2s_mclk_multiple_t)I2S_MCLK_MULTIPLE)) {
    Serial.printf("I2S MCLK %u Hz (x%d)\n", AUDIO_MCLK_HZ, I2S_MCLK_MULTIPLE);
  } else {
    Serial.println("I2S MCLK reconfig failed");
  }
  codec.configureClock(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE);

  // User preferences (volume / voice-wake / wake phrase) persisted in NVS.
  loadSettings(g_settings);
  codec.setPlaybackVolumePercent(g_settings.volume);
  g_voiceWakeEnabled = g_settings.voiceWake;
  g_wakePhraseIdx = g_settings.phraseIdx;
  face.setShowGear(true);
  Serial.printf("Settings: vol=%u%% voiceWake=%d phrase='%s'\n",
                g_settings.volume, g_settings.voiceWake, WAKE_PRESET_LABELS[g_wakePhraseIdx]);

  if (initSoundFx()) {
    face.showText("Sonidos OK");
    delay(400);
  } else {
    face.showText("Sin efectos SPIFFS", TFT_ORANGE);
    delay(600);
  }

#if ENABLE_WAKEWORD && !WAKE_MODE_PC
  // On-device WakeNet ("Hi ESP"). Disabled while WAKE_MODE_PC is on so it doesn't
  // fight the PC-wake recorder for the I2S bus. Kept here to RETAKE later.
  g_srInputFormat = detectMicInputFormat(i2s);
  ESP_SR.onEvent(onSrEvent);
  if (!ESP_SR.begin(i2s, srCommands, 0, SR_CHANNELS_STEREO, SR_MODE_WAKEWORD, g_srInputFormat)) {
    Serial.println("ESP_SR init failed! Check partition scheme: ESP SR 16M");
    face.showText("Error: WakeNet no inicio", TFT_RED);
  } else {
    g_wakeNetRunning = true;
    Serial.printf("WakeNet ready (Hi ESP), input=%s\n", g_srInputFormat);
  }
#endif

  face.showText(WAKE_HINT);
#if WAKE_MODE_PC
  Serial.println("Ready - PC wake (\"Hola asistente\") + touch");
#else
  Serial.println("Ready - WakeNet + touch");
#endif
  lastActivityMs = millis();
}

void loop() {
  face.update();

  switch (state) {
    case State::Sleeping: {
      // Bored mood after 20 s idle (half-lids + slower saccades). Signed cast guards
      // against lastActivityMs being parked in the future by the idle-remark logic.
      face.setBored((int32_t)(millis() - lastActivityMs) > 20000);

      // Touch: a SHORT tap on the gear opens Settings, a short tap anywhere else wakes.
      // A LONG press (>700 ms) ANYWHERE also opens Settings — a mapping-independent
      // fallback so config is reachable even before the touch X/Y is calibrated. The
      // "tap screen=(x,y)" log lets us calibrate the gear hit-zone from the serial.
      static bool pressing = false, longHandled = false;
      static uint32_t pressStart = 0;
      static int pressX = 0, pressY = 0;
      int tx, ty;
      bool down = touchReadPoint(gfx.width(), gfx.height(), tx, ty);
      if (down || pressing) {
        if (down && !pressing) {
          pressing = true; longHandled = false; pressStart = millis();
          pressX = tx; pressY = ty;
          Serial.printf("tap screen=(%d,%d) gearHit=%d\n", tx, ty, Face::gearHit(tx, ty));
        } else if (down && !longHandled && millis() - pressStart > 700) {
          longHandled = true;
          openSettings();                  // long-press anywhere -> settings
        } else if (!down) {                // finger released
          pressing = false;
          if (!longHandled) {              // short tap
            if (Face::gearHit(pressX, pressY)) {
              openSettings();
            } else {
              face.clearMicLevel();
              state = State::Listening;
            }
          }
        }
        break;   // a finger is interacting: skip the voice wake gate this iteration
      }

#if WAKE_MODE_PC
      // PC-side wake WITHOUT starving touch. The energy gate runs only every
      // WAKE_PROBE_INTERVAL_MS (the loop is otherwise free for the per-loop touch poll),
      // and the expensive record + /wake-check only fires when the mic peak crosses
      // WAKE_LISTEN_PEAK. Touch beats the probe at every step: it aborts the record
      // mid-chunk and is rechecked before the HTTP. Turning g_voiceWakeEnabled off
      // (Settings) skips all of this, so touch is then instant.
      static uint32_t lastWakeProbe = 0;
      if (g_voiceWakeEnabled && millis() >= wakeIgnoreUntil &&
          millis() - lastWakeProbe >= WAKE_PROBE_INTERVAL_MS) {
        lastWakeProbe = millis();
        static uint32_t lastPeakLog = 0;
        uint32_t peak = recorder.quickPeak(i2s, 60);
        if (millis() - lastPeakLog > 2000) {
          Serial.printf("wake-listen ambient peak=%u (umbral %u)\n", peak, WAKE_LISTEN_PEAK);
          lastPeakLog = millis();
        }
        if (peak >= STARTLE_PEAK && millis() > emotionHoldUntil) {
          face.setEmotion(Emotion::Surprised);  // glance at a sudden loud sound
          face.update();
          emotionHoldUntil = millis() + 900;
        }
        if (peak >= WAKE_LISTEN_PEAK && !touchPressed()) {
          Serial.printf("wake-listen: peak=%u -> probing\n", peak);
          Recording probe = recorder.record(i2s, nullptr, 500, true, touchPressed);
          if (touchPressed()) {           // touched during/after the probe -> wake by touch
            if (probe.wav) free(probe.wav);
            face.clearMicLevel();
            state = State::Listening;
            break;
          }
          if (probe.wav) {
            bool wake = checkWakePhrase(probe.wav, probe.size);
            free(probe.wav);
            if (wake) {
              Serial.println("PC wake detected (Hola asistente)");
              face.clearMicLevel();
              state = State::Listening;
              break;
            }
          }
        }
      }
#endif

#if ENABLE_WAKEWORD && !WAKE_MODE_PC
      if (wakeDetected && millis() < wakeIgnoreUntil) {
        wakeDetected = false;
      } else if (wakeDetected) {
        wakeDetected = false;
        face.clearMicLevel();
        state = State::Listening;
      }
#endif

      // Proactive: after a while idle, say something on its own.
      if (millis() - lastActivityMs > IDLE_REMARK_MS && millis() >= wakeIgnoreUntil) {
        proactiveIdleRemark();
        lastActivityMs = millis() + random(0, 60000);  // vary the next one
      }
      break;
    }

    case State::Listening: {
      face.setEmotion(Emotion::Surprised);
      face.update();
      face.showText("Te escucho...", TFT_GREEN);

      pauseWakeListener();
      Recording rec = recorder.record(i2s, onMicLevel);
      face.clearMicLevel();

      if (!rec.wav) {
        face.showText("No escuche nada. " WAKE_HINT);
        face.setEmotion(Emotion::Neutral);
        resumeWakeListener();
        lastActivityMs = millis();
        state = State::Sleeping;
        break;
      }

      state = State::Thinking;
      face.setEmotion(Emotion::Thinking);
      face.update();
      face.showText("Pensando...", TFT_YELLOW);

      String emotion, reply, soundEffect;
      bool sing = false;
      bool doSpeak = true;
      soundEffect = "none";
      bool ok = askBrain(rec.wav, rec.size, emotion, reply, sing, doSpeak, soundEffect);
      free(rec.wav);

      if (ok) {
        if (soundEffect.length() > 0 && soundEffect != "none") {
          playSoundEffect(soundEffect.c_str());
          delay(150);
        }
        face.setEmotion(emotionFromString(emotion));
        if (reply.length() > 0) {
          face.showText(reply);
        } else {
          face.showText("");
        }
        face.update();
        if (doSpeak && reply.length() > 0) {
          speak(reply, sing);
        } else {
          Serial.println("Face only — no TTS");
        }
      } else {
        face.setEmotion(Emotion::Sad);
        face.showText("No pude hablar con el cerebro :(", TFT_RED);
      }
      resumeWakeListener();
      emotionHoldUntil = millis() + 8000;
      lastActivityMs = millis();
      state = State::Sleeping;
      break;
    }

    case State::Thinking:
      break;
  }

  if (emotionHoldUntil && millis() > emotionHoldUntil) {
    emotionHoldUntil = 0;
    face.setEmotion(Emotion::Neutral);
    face.showText(WAKE_HINT);
  }

  delay(10);
}

void speak(const String &text, bool sing) {
  pauseWakeListener();

  if (WiFi.status() != WL_CONNECTED) {
    face.showText("WiFi perdido", TFT_RED);
    face.update();
    resumeWakeListener();
    return;
  }

  face.showText("Generando voz...", TFT_CYAN);
  face.update();

  JsonDocument doc;
  doc["text"] = text;
  doc["sing"] = sing;
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(10000);
  http.setTimeout(90000);

  String url = String(BRAIN_SERVER_URL) + "/tts";
  Serial.printf("TTS POST %u chars...\n", text.length());
  if (!http.begin(url)) {
    Serial.println("TTS http.begin failed");
    face.showText("TTS error HTTP", TFT_RED);
    face.update();
    resumeWakeListener();
    return;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  int code = http.POST(body);
  Serial.printf("TTS POST code=%d\n", code);
  if (code != 200) {
    String err = http.getString();
    Serial.printf("TTS error: %d %s\n", code, err.c_str());
    face.showText("TTS fallo (" + String(code) + ")", TFT_RED);
    face.update();
    http.end();
    resumeWakeListener();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  int remaining = http.getSize();
  static uint8_t buf[2048];
  size_t headerSkip = 44;
  size_t pcmWritten = 0;
  uint32_t lastMouthUpdate = 0;

  preparePlayback(i2s);
  if (!g_i2sTxRunning) {
    Serial.println("TTS: TX not ready after preparePlayback");
    http.end();
    face.showText("Error audio TX", TFT_RED);
    endPlayback(i2s);
    resumeWakeListener();
    return;
  }
  face.showText(text);  // show what it's saying during playback (not "Generando voz...")
  face.setTalking(true);
  face.update();
  digitalWrite(PIN_SPK_EN, LOW);

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    size_t avail = stream->available();
    if (avail == 0) {
      if (millis() - lastMouthUpdate > 40) {
        face.update();
        lastMouthUpdate = millis();
      }
      delay(2);
      if (!stream->connected() && stream->available() == 0) break;
      continue;
    }
    size_t n = stream->readBytes(buf, min(avail, sizeof(buf)));
    if (n == 0) break;
    if (remaining > 0) remaining -= n;

    size_t off = 0;
    if (headerSkip > 0) {
      off = min(headerSkip, n);
      headerSkip -= off;
    }
    if (n > off) {
      // Lip-sync: drive the mouth from this chunk's peak amplitude before playing it.
      const int16_t *samples = (const int16_t *)(buf + off);
      size_t nSamples = (n - off) / 2;
      int32_t peak = 0;
      for (size_t i = 0; i < nSamples; i++) {
        int32_t a = abs(samples[i]);
        if (a > peak) peak = a;
      }
      int32_t level = peak / 150;  // ~full-scale TTS peak -> ~100
      face.setMouthAmplitude((uint8_t)(level > 100 ? 100 : level));
      pcmWritten += playMonoPcm16(i2s, buf + off, n - off);
    }

    if (millis() - lastMouthUpdate > 40) {
      face.update();
      lastMouthUpdate = millis();
    }
  }

  delay(50);
  digitalWrite(PIN_SPK_EN, HIGH);
  http.end();

  face.setTalking(false);
  face.update();
  endPlayback(i2s);
  Serial.printf("TTS played %u bytes PCM sing=%d\n", pcmWritten, sing);
  delay(400);
  wakeIgnoreUntil = millis() + WAKE_COOLDOWN_MS;
}

// PC-side wake: POST a short clip to /wake-check; server transcribes it (Whisper)
// and returns whether it contains the wake phrase ("Hola asistente").
bool checkWakePhrase(const uint8_t *wav, size_t size) {
  HTTPClient http;
  http.setTimeout(WAKE_CHECK_TIMEOUT_MS);
  if (!http.begin(String(BRAIN_SERVER_URL) + "/wake-check")) return false;
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Wake-Phrase", WAKE_PRESET_LABELS[g_wakePhraseIdx]);  // user's chosen phrase
  int code = http.POST((uint8_t *)wav, size);
  if (code != 200) {
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  bool wake = doc["wake"] | false;
  const char *heard = doc["heard"] | "";
  if (heard[0]) Serial.printf("wake-check heard='%s' wake=%d\n", heard, wake);
  return wake;
}

bool askBrain(const uint8_t *wav, size_t size, String &emotion, String &reply,
              bool &sing, bool &doSpeak, String &soundEffect) {
  HTTPClient http;
  http.setTimeout(60000);
  http.begin(String(BRAIN_SERVER_URL) + "/converse");
  http.addHeader("Content-Type", "audio/wav");

  int code = http.POST((uint8_t *)wav, size);
  if (code != 200) {
    Serial.printf("Brain server error: %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;

  emotion = doc["emotion"].as<String>();
  reply = doc["reply"].as<String>();
  sing = doc["sing"] | false;
  doSpeak = doc["speak"] | true;
  soundEffect = doc["sound_effect"] | "none";
  Serial.printf("Heard: %s\nReply [%s] speak=%d sing=%d sfx=%s: %s\n",
                doc["heard"].as<const char *>(), emotion.c_str(), doSpeak, sing,
                soundEffect.c_str(), reply.c_str());
  return true;
}

// Spontaneous remark: ask the server for an in-character line and say it unprompted.
void proactiveIdleRemark() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setTimeout(60000);
  if (!http.begin(String(BRAIN_SERVER_URL) + "/idle")) return;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST("{}");
  if (code != 200) {
    http.end();
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return;

  String emotion = doc["emotion"].as<String>();
  String reply = doc["reply"].as<String>();
  bool sing = doc["sing"] | false;
  bool doSpeak = doc["speak"] | true;
  if (reply.length() == 0) return;

  Serial.printf("idle remark [%s]: %s\n", emotion.c_str(), reply.c_str());
  face.setEmotion(emotionFromString(emotion));
  face.showText(reply);
  face.update();
  if (doSpeak) speak(reply, sing);
  emotionHoldUntil = millis() + 8000;
}

// Modal settings menu (opened from the gear). Blocks until the user taps "Guardar",
// then applies and persists the choices and restores the idle screen.
void openSettings() {
  Serial.println("Settings opened");
  g_settingsScreen.run(gfx, g_settings, codec);

  g_voiceWakeEnabled = g_settings.voiceWake;
  g_wakePhraseIdx = g_settings.phraseIdx;
  codec.setPlaybackVolumePercent(g_settings.volume);
  Serial.printf("Settings saved: vol=%u%% voiceWake=%d phrase='%s'\n",
                g_settings.volume, g_settings.voiceWake, WAKE_PRESET_LABELS[g_wakePhraseIdx]);

  gfx.fillScreen(TFT_BLACK);     // clear the menu before the face repaints
  face.redraw();
  face.update();
  face.showText(WAKE_HINT);
  lastActivityMs = millis();
  wakeIgnoreUntil = millis() + 500;   // brief grace so we don't probe on the way out
}
