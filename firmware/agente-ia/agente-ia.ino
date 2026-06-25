// agenteIA - virtual character with emotions on a Hosyond ES3C28P (ESP32-S3).
//
// Wake: touch OR WakeNet "Hi ESP" (ESP-SR, on-device) ->
// record speech -> /converse -> show emotion + TTS.

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_timer.h>

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
#include "music_screen.h"
#include "web_admin.h"

static void addBrainDeviceHeaders(HTTPClient &http) {
  http.addHeader("X-Device-MAC", WiFi.macAddress());
}
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
WebAdmin g_webAdmin;
uint8_t g_wakePhraseIdx = 0;    // index into WAKE_PRESET_LABELS, sent to /wake-check

enum class State { Sleeping, Listening, Thinking };
volatile bool wakeDetected = false;
State state = State::Sleeping;
uint32_t emotionHoldUntil = 0;
uint32_t wakeIgnoreUntil = 0;
uint32_t lastActivityMs = 0;    // last interaction; drives proactive idle remarks
bool g_wakeNetRunning = false;  // true only while on-device WakeNet is actively feeding
bool g_voiceWakeEnabled = true; // PC-side wake phrase listener; toggled from Settings
bool g_idleRemarksEnabled = ENABLE_IDLE_REMARKS;
// Clip ya grabado con wake+comando en la misma frase (evita segunda escucha).
static uint8_t *g_pendingWakeWav = nullptr;
static size_t g_pendingWakeSize = 0;
static uint32_t wakeRejectUntil = 0;  // cooldown tras falso positivo de wake

void updateWakeHint() {
  // Idle prompt removed by request — keep the bottom area clean (black).
  face.showText("");
}

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

#if ENABLE_WAKEWORD
void syncWakeNetFromSettings() {
  if (g_settings.hiEspWake) {
    if (!g_wakeNetRunning) {
      g_srInputFormat = detectMicInputFormat(i2s);
      ESP_SR.onEvent(onSrEvent);
      static const sr_cmd_t srCommands[] = {};
      if (ESP_SR.begin(i2s, srCommands, 0, SR_CHANNELS_STEREO, SR_MODE_WAKEWORD, g_srInputFormat)) {
        g_wakeNetRunning = true;
        Serial.printf("WakeNet started, input=%s\n", g_srInputFormat);
      } else {
        Serial.println("WakeNet start failed");
      }
    }
  } else if (g_wakeNetRunning) {
    ESP_SR.end();
    g_wakeNetRunning = false;
    Serial.println("WakeNet stopped");
  }
}
#else
void syncWakeNetFromSettings() {}
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

void pauseWakeListener(bool settle = true) {
#if ENABLE_WAKEWORD
  if (g_wakeNetRunning) {
    ESP_SR.pause();
    if (settle) delay(SR_PAUSE_SETTLE_MS);
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

// Checkpoint audio-2x-ok: end/begin + MCLK×384; solo configureClock (sin codec.begin).
bool restoreMicBusAfterPlayback(I2SClass &i2s) {
  Serial.println("I2S restore for mic...");

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
  g_i2sTxRunning = true;
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
void speak(const String &text, bool sing, const String &emotion = "happy");
void playMusic(const String &videoId, const String &title);
bool checkWakePhrase(const uint8_t *wav, size_t size, String *commandOut = nullptr);
void proactiveIdleRemark();
void pollDevCommand();
void queueDevFace(const String &emotion, bool bored, uint32_t holdMs, uint8_t vibingMic,
                  uint16_t vibingFloor, uint16_t vibingCeil);
void queueDevSpeak(const String &text, const String &emotion);
void processDevCommands();
void openSettings();

String g_musicVideoId;
String g_musicTitle;
String g_musicNowTitle;
String g_musicNowVideoId;
volatile bool g_musicPlayPending = false;
volatile bool g_musicPlaying = false;
volatile bool g_musicStreaming = false;
volatile bool g_musicStopRequested = false;
static TaskHandle_t g_musicTaskHandle = nullptr;

static volatile bool g_devFacePending = false;
static volatile bool g_devSpeakPending = false;
static String g_devFaceEmotion;
static bool g_devFaceBored = false;
static uint32_t g_devFaceHoldMs = 8000;
static uint8_t g_devVibingMic = 0;
static uint16_t g_devVibingFloor = 0xFFFF;
static uint16_t g_devVibingCeil = 0xFFFF;
static String g_devSpeakText;
static String g_devSpeakEmotion;

static void applyVibingMicSettings(uint8_t mic, uint16_t floor, uint16_t ceil) {
  bool save = false;
  if (mic >= 50 && mic <= 300) {
    face.setVibingMicGain(mic);
    g_settings.vibingMic = mic;
    save = true;
  }
  if (floor != 0xFFFF && floor <= 500) {
    uint16_t c = (ceil != 0xFFFF && ceil <= 900 && ceil >= 200) ? ceil : g_settings.vibingCeil;
    face.setVibingRange(floor, c);
    g_settings.vibingFloor = floor;
    if (ceil != 0xFFFF && ceil >= 200 && ceil <= 900) g_settings.vibingCeil = ceil;
    save = true;
  } else if (ceil != 0xFFFF && ceil >= 200 && ceil <= 900) {
    face.setVibingRange(g_settings.vibingFloor, ceil);
    g_settings.vibingCeil = ceil;
    save = true;
  }
  if (save) saveSettings(g_settings);
}

void queueDevFace(const String &emotion, bool bored, uint32_t holdMs, uint8_t vibingMic,
                  uint16_t vibingFloor, uint16_t vibingCeil) {
  g_devFaceEmotion = emotion;
  g_devFaceBored = bored;
  g_devFaceHoldMs = holdMs;
  g_devVibingMic = vibingMic;
  g_devVibingFloor = vibingFloor;
  g_devVibingCeil = vibingCeil;
  g_devFacePending = true;
}

void queueDevSpeak(const String &text, const String &emotion) {
  g_devSpeakText = text;
  g_devSpeakEmotion = emotion;
  g_devSpeakPending = true;
}

void processDevCommands() {
  if (g_devFacePending) {
    g_devFacePending = false;
    face.setEmotion(emotionFromString(g_devFaceEmotion));
    face.setBored(g_devFaceBored);
    if (g_devVibingMic >= 50 || g_devVibingFloor != 0xFFFF || g_devVibingCeil != 0xFFFF) {
      applyVibingMicSettings(g_devVibingMic, g_devVibingFloor, g_devVibingCeil);
    }
    emotionHoldUntil = millis() + g_devFaceHoldMs;
    face.showText("");
    face.update();
    lastActivityMs = millis();
    Serial.printf("local dev face: %s bored=%d\n", g_devFaceEmotion.c_str(), g_devFaceBored);
  }
  if (g_devSpeakPending && state == State::Sleeping && !g_musicTaskHandle) {
    g_devSpeakPending = false;
    String text = g_devSpeakText;
    String em = g_devSpeakEmotion;
    face.setEmotion(emotionFromString(em));
    face.showText(text);
    face.update();
    Serial.printf("local dev speak [%s]: %s\n", em.c_str(), text.c_str());
    speak(text, false, em);
    emotionHoldUntil = millis() + 8000;
    lastActivityMs = millis();
  }
}

struct MusicTaskArgs {
  String videoId;
  String title;
};

static void playMusicTask(void *arg) {
  auto *a = static_cast<MusicTaskArgs *>(arg);
  playMusic(a->videoId, a->title);
  delete a;
  g_musicTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

static void startMusicAsync() {
  if (g_musicTaskHandle) return;
  auto *a = new MusicTaskArgs{g_musicVideoId, g_musicTitle};
  xTaskCreatePinnedToCore(playMusicTask, "music", 32768, a, 5, &g_musicTaskHandle, 1);
}

void queueMusicPlay(const String &videoId, const String &title) {
  if (g_musicTaskHandle) {
    requestMusicStop();
  }
  g_musicVideoId = videoId;
  g_musicTitle = title.length() > 0 ? title : videoId;
  g_musicNowTitle = g_musicTitle;
  g_musicPlayPending = true;
  Serial.printf("Music queued: %s (%s)\n", g_musicVideoId.c_str(), g_musicTitle.c_str());
  face.setEmotion(Emotion::Happy);
  MusicScreen::drawNowPlaying(gfx, String("En cola: ") + g_musicTitle, g_settings.volume);
  face.update();
}

void requestMusicStop() {
  g_musicStopRequested = true;
  g_musicPlayPending = false;
}

namespace {

constexpr size_t kWavHdrBytes = 44;
constexpr size_t kAgntPrefixBytes = 11;  // magic(4) + ver(1) + rsv(2) + json_len(4)

struct AudioStreamHeader {
  bool agnt = false;
  bool error = false;
  uint8_t prefix[11];
  size_t prefixGot = 0;
  uint32_t jsonLeft = 0;
  char jsonAcc[768];
  size_t jsonGot = 0;
  size_t wavLeft = kWavHdrBytes;

  void reset(bool useAgnt, bool rawPcm = false) {
    agnt = useAgnt;
    error = false;
    prefixGot = 0;
    jsonLeft = 0;
    jsonGot = 0;
    wavLeft = rawPcm ? 0 : kWavHdrBytes;
  }

  bool feed(const uint8_t *data, size_t n, size_t &pcmOff, size_t &pcmLen, String *emotionOut) {
    pcmOff = 0;
    pcmLen = 0;
    size_t i = 0;

    while (i < n) {
      if (!agnt) {
        if (wavLeft > 0) {
          size_t skip = min(wavLeft, n - i);
          wavLeft -= skip;
          i += skip;
          continue;
        }
        pcmOff = i;
        pcmLen = n - i;
        return true;
      }

      if (prefixGot < kAgntPrefixBytes) {
        prefix[prefixGot++] = data[i++];
        if (prefixGot == 4 && memcmp(prefix, "AGNT", 4) != 0) {
          error = true;
          return false;
        }
        if (prefixGot == kAgntPrefixBytes) {
          jsonLeft = ((uint32_t)prefix[7] << 24) | ((uint32_t)prefix[8] << 16) |
                     ((uint32_t)prefix[9] << 8) | (uint32_t)prefix[10];
          if (jsonLeft == 0 || jsonLeft >= sizeof(jsonAcc)) {
            error = true;
            return false;
          }
        }
        continue;
      }

      if (jsonLeft > 0) {
        jsonAcc[jsonGot++] = (char)data[i++];
        jsonLeft--;
        if (jsonLeft == 0 && emotionOut) {
          JsonDocument meta;
          if (!deserializeJson(meta, jsonAcc, jsonGot)) {
            const char *em = meta["emotion"] | "happy";
            *emotionOut = em;
          }
        }
        continue;
      }

      if (wavLeft > 0) {
        size_t skip = min(wavLeft, n - i);
        wavLeft -= skip;
        i += skip;
        continue;
      }

      pcmOff = i;
      pcmLen = n - i;
      return true;
    }
    return true;
  }
};

static uint8_t pcmToMouthLevel(const int16_t *samples, size_t nSamples) {
  if (nSamples == 0) return 0;
  int64_t sumSq = 0;
  int32_t peak = 0;
  for (size_t j = 0; j < nSamples; j++) {
    int32_t s = samples[j];
    int32_t a = s < 0 ? -s : s;
    if (a > peak) peak = a;
    sumSq += (int64_t)s * s;
  }
  float rms = sqrtf((float)sumSq / (float)nSamples);
  if (rms < 160.0f && peak < 650) return 0;

  float pk = sqrtf((float)peak);
  float rm = sqrtf(rms);
  float pkN = (pk - 5.5f) / 40.0f;
  float rmN = (rm - 9.0f) / 36.0f;
  if (pkN < 0.0f) pkN = 0.0f;
  if (rmN < 0.0f) rmN = 0.0f;
  if (pkN > 1.0f) pkN = 1.0f;
  if (rmN > 1.0f) rmN = 1.0f;

  float mix = pkN * 0.8f + rmN * 0.2f;
  float levelF = powf(mix, 0.58f) * 100.0f;
  int32_t level = (int32_t)(levelF + 0.5f);
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  return (uint8_t)level;
}

size_t playHttpPcmStream(HTTPClient &http, WiFiClient *stream, int remaining, bool agntHeader,
                         bool singingMode, String *streamEmotion, bool rawPcm = false,
                         const String *titleOnFirstPcm = nullptr, bool enjoyMusic = false) {
  static uint8_t netBuf[16384];
  static uint8_t playBuf[8192];
  static constexpr size_t kRingCap = 393216;  // ~12 s @ 16 kHz mono en PSRAM
  static uint8_t *ring = nullptr;
  static size_t ringCap = 0;
  if (!ring) {
    ring = (uint8_t *)heap_caps_malloc(kRingCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ring) ring = (uint8_t *)heap_caps_malloc(kRingCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ringCap = ring ? kRingCap : 0;
  }
  if (!ring) return 0;

  size_t ringHead = 0, ringTail = 0, ringCount = 0;
  auto ringFree = [&]() -> size_t { return ringCap - ringCount; };
  auto ringPush = [&](const uint8_t *data, size_t n) -> bool {
    if (n > ringFree()) return false;
    size_t first = min(n, ringCap - ringTail);
    memcpy(ring + ringTail, data, first);
    if (n > first) memcpy(ring, data + first, n - first);
    ringTail = (ringTail + n) % ringCap;
    ringCount += n;
    return true;
  };
  auto ringPop = [&](uint8_t *out, size_t max) -> size_t {
    size_t n = min(max, ringCount);
    size_t first = min(n, ringCap - ringHead);
    memcpy(out, ring + ringHead, first);
    if (n > first) memcpy(out + first, ring, n - first);
    ringHead = (ringHead + n) % ringCap;
    ringCount -= n;
    return n;
  };

  AudioStreamHeader hdr;
  hdr.reset(agntHeader, rawPcm);
  size_t pcmWritten = 0;
  bool showedTitle = false;
  bool primed = false;
  uint32_t lastMouthUpdate = 0;
  uint8_t pcmCarry = 0;
  bool hasCarry = false;
  size_t fadePos = 0;
  static constexpr size_t kFadeSamples = 2400;  // ~150 ms @ 16 kHz
  static constexpr size_t kPrimeBytes = 65536;    // ~2 s antes de sonar
  static constexpr size_t kLowWater = 49152;      // ~1.5 s mínimo
  static constexpr size_t kHighWater = 262144;   // ~8 s objetivo
  static constexpr size_t kIoChunk = 2048;       // ~64 ms por tick I2S
  static constexpr uint32_t kMouthFrameMs = 16;

  auto applyFadeIn = [&](int16_t *samples, size_t count) {
    for (size_t i = 0; i < count && fadePos < kFadeSamples; i++, fadePos++) {
      const float g = (float)fadePos / (float)kFadeSamples;
      samples[i] = (int16_t)(samples[i] * g);
    }
  };

  auto writePcmAligned = [&](const uint8_t *p, size_t len, bool useFade, bool blockI2s) -> size_t {
    if (len == 0) return 0;
    size_t written = 0;
    if (hasCarry) {
      uint8_t pair[2] = {pcmCarry, p[0]};
      int16_t *s = (int16_t *)pair;
      if (useFade) applyFadeIn(s, 1);
      written += playMonoPcm16(i2s, pair, 2, blockI2s);
      p += 1;
      len -= 1;
      hasCarry = false;
    }
    if (len & 1u) {
      pcmCarry = p[len - 1];
      hasCarry = true;
      len -= 1;
    }
    if (len >= 2) {
      if (useFade) {
        int16_t *samples = (int16_t *)p;
        applyFadeIn(samples, len / 2);
      }
      written += playMonoPcm16(i2s, p, len, blockI2s);
    }
    return written;
  };

  auto pumpNetwork = [&]() -> bool {
    if (!http.connected() && stream->available() == 0) return false;
    if (g_musicStopRequested) return false;
    size_t avail = stream->available();
    if (avail == 0) return false;

    size_t n = stream->readBytes(netBuf, min(avail, sizeof(netBuf)));
    if (n == 0) return false;
    if (remaining > 0) remaining -= (int)n;
    if (hdr.error) return false;

    size_t pcmOff = 0, pcmLen = 0;
    if (!hdr.feed(netBuf, n, pcmOff, pcmLen, streamEmotion)) {
      Serial.printf("Audio stream: invalid AGNT header (jsonLeft=%lu jsonGot=%u)\n",
                    (unsigned long)hdr.jsonLeft, (unsigned)hdr.jsonGot);
      hdr.error = true;
      return false;
    }
    if (pcmLen == 0) return true;

    if (titleOnFirstPcm && !showedTitle) {
      MusicScreen::drawTitle(gfx, *titleOnFirstPcm);
      face.setTalking(true);
      if (singingMode) {
        face.setSinging(true);
        face.setEmotion(Emotion::Happy);
      }
      face.update();
      showedTitle = true;
    }
    if (!singingMode && !enjoyMusic && pcmLen >= 2) {
      const int16_t *samples = (const int16_t *)(netBuf + pcmOff);
      face.setMouthAmplitude(pcmToMouthLevel(samples, pcmLen / 2));
    }

    size_t pushed = 0;
    while (pushed < pcmLen) {
      size_t chunk = min(pcmLen - pushed, ringFree());
      if (chunk == 0) break;
      ringPush(netBuf + pcmOff + pushed, chunk);
      pushed += chunk;
    }
    return true;
  };

  auto pumpNetworkBurst = [&]() -> bool {
    bool got = false;
    for (int i = 0; i < 12 && ringFree() >= kIoChunk; i++) {
      if (pumpNetwork()) got = true;
      else break;
    }
    return got;
  };

  auto playFromRing = [&](bool useFade) {
    while (ringCount >= kIoChunk && !g_musicStopRequested) {
      size_t n = ringPop(playBuf, min(sizeof(playBuf), ringCount));
      if (n > 0) pcmWritten += writePcmAligned(playBuf, n, useFade, true);
    }
    if (ringCount >= 2 && !g_musicStopRequested) {
      size_t n = ringPop(playBuf, ringCount & ~1u);
      if (n > 0) pcmWritten += writePcmAligned(playBuf, n, useFade, true);
    }
  };

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    if (g_musicStopRequested) break;

    if (enjoyMusic && primed && ringCount < kHighWater) pumpNetworkBurst();

    while (ringCount < kLowWater && pumpNetwork()) {}

    if (!primed) {
      while (ringCount < kPrimeBytes && pumpNetwork()) {}
      if (ringCount < kPrimeBytes && stream->available() == 0 && !stream->connected()) break;
      if (ringCount < kPrimeBytes && stream->available() == 0) {
        g_webAdmin.loop();
        if (!enjoyMusic) face.update();
        delay(1);
        if (!stream->connected() && stream->available() == 0) break;
        continue;
      }
      primed = true;
    }

    if (ringCount == 0) {
      if (primed) {
        for (int retry = 0; retry < 32 && ringCount == 0; retry++) {
          pumpNetwork();
          if (ringCount > 0) break;
          delay(1);
        }
        if (ringCount == 0) {
          if (!stream->connected() && stream->available() == 0) break;
          continue;
        }
      }
      if (!pumpNetwork()) {
        if (!stream->connected() && stream->available() == 0) break;
        g_webAdmin.loop();
        delay(1);
        continue;
      }
      if (ringCount == 0) continue;
    }

    if (enjoyMusic) {
      while (ringCount < kLowWater && pumpNetwork()) {}
      playFromRing(false);
    } else {
      size_t n = ringPop(playBuf, min(sizeof(playBuf), ringCount));
      if (n > 0) pcmWritten += writePcmAligned(playBuf, n, true, true);
    }

    g_webAdmin.loop();
    if (millis() - lastMouthUpdate > kMouthFrameMs) {
      face.update();
      lastMouthUpdate = millis();
    }
  }

  // Vaciar ring al final
  while (ringCount > 0) {
    size_t n = ringPop(playBuf, min(sizeof(playBuf), ringCount));
    if (n > 0) pcmWritten += writePcmAligned(playBuf, n, !enjoyMusic, true);
  }
  if (hasCarry) hasCarry = false;

  return pcmWritten;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  gfx.init();
  gfx.setRotation(DISPLAY_ROTATION);
  gfx.setBrightness(200);
  face.begin();
  face.setEmotion(Emotion::Neutral);
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
  applySettingsGlobals(g_settings, g_wakePhraseIdx, g_voiceWakeEnabled, g_idleRemarksEnabled);
  face.setVibingMicGain(g_settings.vibingMic);
  face.setVibingRange(g_settings.vibingFloor, g_settings.vibingCeil);
  codec.setPlaybackVolumePercent(g_settings.volume);
  syncWakeNetFromSettings();
  g_webAdmin.begin(g_settings, codec, g_wakePhraseIdx, g_voiceWakeEnabled, g_idleRemarksEnabled);
  face.setShowGear(true);
  Serial.printf("Settings: vol=%u%% voiceWake=%d phrase='%s' idle=%d\n",
                g_settings.volume, g_settings.voiceWake, WAKE_PRESET_LABELS[g_wakePhraseIdx],
                g_idleRemarksEnabled);
#if ENABLE_WAKEWORD
  Serial.printf("  hiEspWake=%d wakeNet=%d\n", g_settings.hiEspWake, g_wakeNetRunning);
#endif

  if (initSoundFx()) {
    face.showText("Sonidos OK");
    delay(400);
  } else {
    face.showText("Sin efectos SPIFFS", TFT_ORANGE);
    delay(600);
  }

  updateWakeHint();
  Serial.println("Ready - web admin + touch");
  lastActivityMs = millis();
}

void loop() {
  // Mientras hay tarea de música, ella dibuja la UI (prefetch); durante el stream
  // solo STOP/volumen desde aquí — evita dos hilos tocando SPI (rayas en pantalla).
  if (g_musicTaskHandle) {
    g_webAdmin.loop();
    if (g_musicStreaming) {
      static uint32_t lastTouchMs = 0;
      if (millis() - lastTouchMs >= 80) {
        lastTouchMs = millis();
        MusicScreen::pollTouch(gfx, g_settings, codec);
      }
    }
    vTaskDelay(2);
    return;
  }

  face.update();

  processDevCommands();

  switch (state) {
    case State::Sleeping: {
      // Bored mood after 20 s idle (half-lids + slower saccades). Signed cast guards
      // against lastActivityMs being parked in the future by the idle-remark logic.
      face.setBored(!face.isVibing() && (int32_t)(millis() - lastActivityMs) > 20000);

      // Gesto vibing: espectrograma en boca según micrófono ambiente.
      static uint32_t lastVibingMic = 0;
      if (face.isVibing() && millis() - lastVibingMic > 25) {
        lastVibingMic = millis();
        uint8_t bands[12];
        pauseWakeListener(false);
        uint32_t peak = recorder.captureVibingBands(i2s, bands, 12, 45);
        resumeWakeListener();
        face.setVibingSpectrum(bands, 12, peak);
        lastActivityMs = millis();
      }

      // Touch: a SHORT tap on the gear opens Settings, a short tap anywhere else wakes.
      // A LONG press (>700 ms) ANYWHERE also opens Settings — a mapping-independent
      // fallback so config is reachable even before the touch X/Y is calibrated. The
      // "tap screen=(x,y)" log lets us calibrate the gear hit-zone from the serial.
      static bool pressing = false, longHandled = false;
      static uint32_t pressStart = 0;
      static int pressX = 0, pressY = 0;
      static int lastX = 0, lastY = 0;
      int tx, ty;
      bool down = touchReadPoint(gfx.width(), gfx.height(), tx, ty);
      if (down || pressing) {
        if (down && !pressing) {
          pressing = true; longHandled = false; pressStart = millis();
          pressX = tx; pressY = ty;
          lastX = tx; lastY = ty;
          Serial.printf("tap screen=(%d,%d) gearHit=%d\n", tx, ty, Face::gearHit(tx, ty, gfx.width()));
        } else if (down) {
          lastX = tx; lastY = ty;
          if (!longHandled && millis() - pressStart > 700) {
            longHandled = true;
            openSettings();                // long-press anywhere -> settings
          }
        } else {                           // finger released
          pressing = false;
          if (!longHandled) {              // short tap
            if (Face::gearHit(pressX, pressY, gfx.width()) || Face::gearHit(lastX, lastY, gfx.width())) {
              openSettings();
            } else {
              face.clearMicLevel();
              state = State::Listening;
            }
          }
        }
        break;   // a finger is interacting: skip the voice wake gate this iteration
      }

      // PC-side wake phrase via /wake-check (gated by g_voiceWakeEnabled).
      static uint32_t lastWakeProbe = 0;
      if (!face.isVibing() && g_voiceWakeEnabled && millis() >= wakeIgnoreUntil &&
          millis() >= wakeRejectUntil &&
          millis() - lastWakeProbe >= WAKE_PROBE_INTERVAL_MS) {
        lastWakeProbe = millis();
        static uint32_t lastPeakLog = 0;
        uint32_t peak = recorder.quickPeak(i2s, 60);
        if (millis() - lastPeakLog > 2000) {
          Serial.printf("wake-listen ambient peak=%u (umbral %u)\n", peak, WAKE_LISTEN_PEAK);
          lastPeakLog = millis();
        }
        if (peak >= STARTLE_PEAK && millis() > emotionHoldUntil && !face.isVibing()) {
          face.setEmotion(Emotion::Surprised);  // glance at a sudden loud sound
          face.update();
          emotionHoldUntil = millis() + 900;
        }
        if (peak >= WAKE_LISTEN_PEAK && !touchPressed()) {
          Serial.printf("wake-listen: peak=%u -> probing\n", peak);
          Recording probe = recorder.record(i2s, nullptr, WAKE_PROBE_NO_VOICE_MS, true, touchPressed);
          if (touchPressed()) {           // touched during/after the probe -> wake by touch
            if (probe.wav) free(probe.wav);
            face.clearMicLevel();
            state = State::Listening;
            break;
          }
          if (probe.wav) {
            String inlineCmd;
            bool wake = checkWakePhrase(probe.wav, probe.size, &inlineCmd);
            if (wake) {
              if (inlineCmd.length() >= 3) {
                g_pendingWakeWav = probe.wav;
                g_pendingWakeSize = probe.size;
                probe.wav = nullptr;
                Serial.printf("PC wake+command: %s\n", inlineCmd.c_str());
              } else {
                free(probe.wav);
                Serial.println("PC wake detected (Hola asistente)");
              }
              face.clearMicLevel();
              state = State::Listening;
              break;
            }
            free(probe.wav);
            wakeRejectUntil = millis() + WAKE_REJECT_COOLDOWN_MS;
            Serial.println("wake-check: no wake phrase — cooldown");
          }
        }
      }

#if ENABLE_WAKEWORD
      if (wakeDetected) {
        wakeDetected = false;
        if (g_settings.hiEspWake && millis() >= wakeIgnoreUntil) {
          face.clearMicLevel();
          state = State::Listening;
        }
      }
#endif

      // Dev commands from PC (/dev panel): preview faces or speak test lines.
      pollDevCommand();

      // Proactive: after a while idle, say something on its own.
      if (g_idleRemarksEnabled && millis() - lastActivityMs > IDLE_REMARK_MS &&
          millis() >= wakeIgnoreUntil) {
        proactiveIdleRemark();
        lastActivityMs = millis() + random(0, 60000);
      }
      break;
    }

    case State::Listening: {
      Recording rec;
      const bool usePendingWake = (g_pendingWakeWav != nullptr);

      if (usePendingWake) {
        rec.wav = g_pendingWakeWav;
        rec.size = g_pendingWakeSize;
        g_pendingWakeWav = nullptr;
        g_pendingWakeSize = 0;
        face.setEmotion(Emotion::Thinking);
        face.showText("Pensando...", TFT_YELLOW);
        face.update();
      } else {
        face.setEmotion(Emotion::Surprised);
        face.update();
        face.showText("Te escucho...", TFT_GREEN);
      }

      pauseWakeListener();
      if (!usePendingWake) {
        rec = recorder.record(i2s, onMicLevel);
        face.clearMicLevel();
      }

      if (!rec.wav) {
        face.showText("No escuché nada. Toca la pantalla.");
        face.setEmotion(Emotion::Neutral);
        resumeWakeListener();
        lastActivityMs = millis();
        state = State::Sleeping;
        break;
      }

      state = State::Thinking;
      if (!usePendingWake) {
        face.setEmotion(Emotion::Thinking);
        face.update();
        face.showText("Pensando...", TFT_YELLOW);
      }

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
          speak(reply, sing, emotion);
        } else {
          Serial.println("Silent ignore — no TTS");
          emotionHoldUntil = 0;
          face.setEmotion(Emotion::Neutral);
          updateWakeHint();
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
    updateWakeHint();
  }

  if (g_musicPlayPending && !g_musicTaskHandle) {
    g_musicPlayPending = false;
    startMusicAsync();
    lastActivityMs = millis();
    state = State::Sleeping;
  }

  g_webAdmin.loop();
  delay(10);
}

void speak(const String &text, bool sing, const String &emotion) {
  pauseWakeListener();

#if !ENABLE_SINGING
  sing = false;
#endif

  if (WiFi.status() != WL_CONNECTED) {
    face.showText("WiFi perdido", TFT_RED);
    face.update();
    resumeWakeListener();
    return;
  }

  face.showText(sing ? "Componiendo canción..." : "Generando voz...", TFT_CYAN);
  face.update();

  bool useRvc = sing;
  bool triedTtsFallback = false;

  for (;;) {
    JsonDocument doc;
    String url;
    bool agntHeader = false;
    if (useRvc) {
      doc["lyrics"] = text;
      doc["emotion"] = emotion;
      doc["expand"] = false;
      url = String(BRAIN_SERVER_URL) + "/agent/sing";
      agntHeader = true;
    } else {
      String ttsText = text;
      if (ttsText.length() > 300) {
        ttsText = ttsText.substring(0, 297) + "...";
      }
      doc["text"] = ttsText;
      doc["sing"] = sing;
      url = String(BRAIN_SERVER_URL) + "/tts";
    }
    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(15000);
    http.setTimeout(useRvc ? 180000 : TTS_HTTP_TIMEOUT_MS);

    Serial.printf("%s POST %u chars...\n", useRvc ? "SING" : "TTS", text.length());
    if (!http.begin(url)) {
      Serial.println("speak http.begin failed");
      face.showText("TTS error HTTP", TFT_RED);
      face.update();
      resumeWakeListener();
      return;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    addBrainDeviceHeaders(http);

    int code = http.POST(body);
    Serial.printf("%s POST code=%d len=%d\n", useRvc ? "SING" : "TTS", code, http.getSize());

    if (useRvc && code == 503 && !triedTtsFallback) {
      http.end();
      Serial.println("SING 503 — fallback to /tts (sing prosody)");
      useRvc = false;
      triedTtsFallback = true;
      face.showText("Canto sin RVC...", TFT_YELLOW);
      face.update();
      continue;
    }

    if (code != 200) {
      String err = http.getString();
      Serial.printf("speak error: %d %s\n", code, err.c_str());
      face.showText(useRvc ? "Canto falló (" + String(code) + ")" : "TTS falló (" + String(code) + ")",
                    TFT_RED);
      face.update();
      http.end();
      resumeWakeListener();
      return;
    }

    WiFiClient *stream = http.getStreamPtr();
    int remaining = http.getSize();

    preparePlayback(i2s);
    if (!g_i2sTxRunning) {
      Serial.println("speak: TX not ready after preparePlayback");
      http.end();
      face.showText("Error audio TX", TFT_RED);
      endPlayback(i2s);
      resumeWakeListener();
      return;
    }

    face.showText(text);
    face.setTalking(true);
    if (sing) {
      face.setSinging(true);
      face.setEmotion(Emotion::Happy);
      face.setMouthAmplitude(100);
    }
    face.update();
    digitalWrite(PIN_SPK_EN, LOW);

    String streamEmotion;
    size_t pcmWritten = playHttpPcmStream(http, stream, remaining, agntHeader, sing, &streamEmotion);
    if (sing && streamEmotion.length() > 0) {
      face.setEmotion(emotionFromString(streamEmotion));
    }

    delay(50);
    digitalWrite(PIN_SPK_EN, HIGH);
    http.end();

    face.setSinging(false);
    face.setTalking(false);
    face.update();
    endPlayback(i2s);
    Serial.printf("speak played %u bytes PCM sing=%d rvc=%d\n", pcmWritten, sing, useRvc);
    if (sing && pcmWritten == 0) {
      Serial.println("WARN: sing stream produced 0 PCM bytes");
      face.showText("Sin audio de canto", TFT_RED);
      face.update();
    }
    delay(400);
    wakeIgnoreUntil = millis() + WAKE_COOLDOWN_MS;
    resumeWakeListener();
    return;
  }
}

static bool waitMusicPrefetchReady(const String &videoId, const String &label) {
  const unsigned long deadline = millis() + 200000UL;
  while (millis() < deadline && !g_musicStopRequested) {
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    const String statusUrl =
        String(BRAIN_SERVER_URL) + "/music/prefetch/status?id=" + videoId;
    if (http.begin(statusUrl)) {
      addBrainDeviceHeaders(http);
      const int code = http.GET();
      if (code == 200) {
        http.end();
        Serial.println("MUSIC prefetch ready");
        return true;
      }
      if (code == 503) {
        Serial.printf("MUSIC prefetch error: %s\n", http.getString().c_str());
        http.end();
        face.showText("Musica: PC sin audio", TFT_RED);
        face.update();
        return false;
      }
      http.end();
    }
    MusicScreen::drawNowPlaying(gfx, String("Esperando PC... ") + label, g_settings.volume);
    face.update();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  if (g_musicStopRequested) return false;
  face.showText("Timeout PC (audio lento)", TFT_RED);
  face.update();
  return false;
}

void playMusic(const String &videoId, const String &title) {
  pauseWakeListener();
  g_musicStopRequested = false;

  if (WiFi.status() != WL_CONNECTED) {
    face.showText("WiFi perdido", TFT_RED);
    face.update();
    resumeWakeListener();
    return;
  }

  const String label = title.length() > 0 ? title : videoId;
  g_musicNowTitle = label;
  g_musicNowVideoId = videoId;
  g_musicPlaying = true;

  face.setEmotion(Emotion::Happy);
  MusicScreen::drawNowPlaying(gfx, String("Esperando PC... ") + label, g_settings.volume);
  face.update();

  if (!waitMusicPrefetchReady(videoId, label)) {
    g_musicPlaying = false;
    resumeWakeListener();
    return;
  }

  HTTPClient http;
  WiFiClient client;
  client.setNoDelay(true);
  client.setTimeout((uint32_t)MUSIC_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setConnectTimeout(30000);
  http.setTimeout((int32_t)MUSIC_HTTP_TIMEOUT_MS);
  http.useHTTP10(true);

  const String url = String(BRAIN_SERVER_URL) + "/music/play?id=" + videoId;
  Serial.printf("MUSIC GET %s timeout=%d\n", url.c_str(), (int)MUSIC_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    face.showText("Musica: error HTTP", TFT_RED);
    face.update();
    g_musicPlaying = false;
    resumeWakeListener();
    return;
  }
  http.addHeader("Connection", "close");
  addBrainDeviceHeaders(http);

  const int code = http.GET();
  Serial.printf("MUSIC GET code=%d len=%d\n", code, http.getSize());

  if (code != 200 || g_musicStopRequested) {
    const String err = http.getString();
    Serial.printf("music error: %d %s\n", code, err.c_str());
    String msg;
    if (g_musicStopRequested) {
      msg = "Musica detenida";
    } else if (code == HTTPC_ERROR_READ_TIMEOUT) {
      msg = "Timeout PC (audio lento)";
    } else if (code < 0) {
      msg = "Musica error red (" + String(code) + ")";
    } else {
      msg = "Musica fallo (" + String(code) + ")";
    }
    face.showText(msg, g_musicStopRequested ? TFT_YELLOW : TFT_RED);
    face.update();
    http.end();
    g_musicPlaying = false;
    resumeWakeListener();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (stream) stream->setTimeout((uint32_t)MUSIC_HTTP_TIMEOUT_MS);
  const int remaining = http.getSize() <= 0 ? -1 : http.getSize();

  preparePlayback(i2s);
  if (!g_i2sTxRunning) {
    http.end();
    face.showText("Error audio TX", TFT_RED);
    endPlayback(i2s);
    g_musicPlaying = false;
    resumeWakeListener();
    return;
  }

  face.setTalking(false);
  face.setSinging(false);
  face.setEmotion(Emotion::Happy);
  MusicScreen::drawNowPlaying(gfx, label, g_settings.volume);
  face.update();
  digitalWrite(PIN_SPK_EN, LOW);

  g_musicStreaming = true;
  const size_t pcmWritten =
      playHttpPcmStream(http, stream, remaining, false, false, nullptr, true, nullptr, true);
  g_musicStreaming = false;

  delay(50);
  digitalWrite(PIN_SPK_EN, HIGH);
  http.end();

  face.setTalking(false);
  face.update();
  endPlayback(i2s);
  g_musicPlaying = false;
  Serial.printf("music played %u bytes PCM stop=%d\n", pcmWritten, (int)g_musicStopRequested);
  if (g_musicStopRequested) {
    face.showText("Musica detenida", TFT_YELLOW);
    face.update();
  } else if (pcmWritten == 0) {
    face.showText("Sin audio de musica", TFT_RED);
    face.update();
  } else if (!label.isEmpty()) {
    updateWakeHint();
  }
  g_musicStopRequested = false;
  delay(400);
  wakeIgnoreUntil = millis() + WAKE_COOLDOWN_MS;
  resumeWakeListener();
}

// PC-side wake: POST a short clip to /wake-check; server transcribes it (Whisper)
// and returns whether it contains the wake phrase ("Hola asistente").
bool checkWakePhrase(const uint8_t *wav, size_t size, String *commandOut) {
  HTTPClient http;
  http.setTimeout(WAKE_CHECK_TIMEOUT_MS);
  if (!http.begin(String(BRAIN_SERVER_URL) + "/wake-check")) return false;
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("X-Wake-Phrase", WAKE_PRESET_LABELS[g_wakePhraseIdx]);  // user's chosen phrase
  addBrainDeviceHeaders(http);
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
  if (commandOut) *commandOut = doc["command"] | "";
  if (heard[0]) {
    Serial.printf("wake-check heard='%s' wake=%d", heard, wake);
    if (commandOut && commandOut->length() > 0) Serial.printf(" cmd='%s'", commandOut->c_str());
    Serial.println();
  }
  return wake;
}

bool askBrain(const uint8_t *wav, size_t size, String &emotion, String &reply,
              bool &sing, bool &doSpeak, String &soundEffect) {
  HTTPClient http;
  http.setTimeout(60000);
  http.begin(String(BRAIN_SERVER_URL) + "/converse");
  http.addHeader("Content-Type", "audio/wav");
  addBrainDeviceHeaders(http);

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

// Pull one dev command queued from the PC panel (/dev): preview face or speak text.
void pollDevCommand() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (state != State::Sleeping) return;

  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 2000) return;
  lastPoll = millis();

  HTTPClient http;
  http.setTimeout(4000);
  if (!http.begin(String(BRAIN_SERVER_URL) + "/api/dev/poll")) return;
  addBrainDeviceHeaders(http);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err || doc["cmd"].isNull()) return;

  JsonObject cmd = doc["cmd"];
  const char *type = cmd["type"];
  if (!type) return;

  lastActivityMs = millis();

  if (strcmp(type, "face") == 0) {
    String em = cmd["emotion"] | "neutral";
    em.toLowerCase();
    face.setEmotion(emotionFromString(em));
    face.setBored(cmd["bored"] | false);
    uint32_t hold = cmd["hold_ms"] | (em == "vibing" ? 60000u : 8000u);
    int vmic = cmd["vibing_mic"] | 0;
    int vflo = cmd["vibing_floor"] | -1;
    int vcei = cmd["vibing_ceil"] | -1;
    uint8_t mic = (vmic >= 50 && vmic <= 300) ? (uint8_t)vmic : 0;
    uint16_t flo = (vflo >= 0 && vflo <= 500) ? (uint16_t)vflo : 0xFFFF;
    uint16_t cei = (vcei >= 200 && vcei <= 900) ? (uint16_t)vcei : 0xFFFF;
    if (mic || flo != 0xFFFF || cei != 0xFFFF) {
      applyVibingMicSettings(mic, flo, cei);
    }
    emotionHoldUntil = millis() + hold;
    face.showText("");
    face.update();
    Serial.printf("dev face: %s bored=%d hold=%ums\n", em.c_str(), (bool)cmd["bored"], hold);
    return;
  }

  if (strcmp(type, "speak") == 0) {
    String text = cmd["text"] | "";
    String em = cmd["emotion"] | "happy";
    if (text.length() == 0) return;
    face.setEmotion(emotionFromString(em));
    face.showText(text);
    face.update();
    Serial.printf("dev speak [%s]: %s\n", em.c_str(), text.c_str());
    speak(text, false, em);
    emotionHoldUntil = millis() + 8000;
  }
}

// Spontaneous remark: ask the server for an in-character line and say it unprompted.
void proactiveIdleRemark() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setTimeout(60000);
  if (!http.begin(String(BRAIN_SERVER_URL) + "/idle")) return;
  http.addHeader("Content-Type", "application/json");
  addBrainDeviceHeaders(http);
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
  if (doSpeak) speak(reply, sing, emotion);
  emotionHoldUntil = millis() + 8000;
}

// Modal settings menu (opened from the gear). Blocks until the user taps "Guardar",
// then applies and persists the choices and restores the idle screen.
void openSettings() {
  Serial.println("Settings opened");
  g_settingsScreen.run(gfx, g_settings, codec);

  applySettingsGlobals(g_settings, g_wakePhraseIdx, g_voiceWakeEnabled, g_idleRemarksEnabled);
  syncWakeNetFromSettings();
  codec.setPlaybackVolumePercent(g_settings.volume);
  Serial.printf("Settings saved: vol=%u%% voiceWake=%d phrase='%s' idle=%d\n",
                g_settings.volume, g_settings.voiceWake, WAKE_PRESET_LABELS[g_wakePhraseIdx],
                g_idleRemarksEnabled);
#if ENABLE_WAKEWORD
  Serial.printf("  hiEspWake=%d\n", g_settings.hiEspWake);
#endif

  gfx.fillScreen(TFT_BLACK);
  face.redraw();
  face.update();
  updateWakeHint();
  lastActivityMs = millis();
  wakeIgnoreUntil = millis() + 500;   // brief grace so we don't probe on the way out
}
