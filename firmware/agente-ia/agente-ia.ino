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

// Cambia en cada flash para verificar en Serial Monitor que cargó el binario nuevo.
static constexpr const char *FIRMWARE_BUILD_ID = "capt-off-250626";

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

// M4 / M5 / M9 — active context and expressivity from server
String   g_context     = "idle";
float    g_expressivity = 0.5f;

// M3 — Idle Behavior state machine
enum class IdlePhase { Active, Present, Waiting, SemiDormant };
IdlePhase idlePhase = IdlePhase::Active;
// M2 — Microexpression cooldown timers
uint32_t nextMicroExpAt    = 0;
uint32_t lastGlanceUpAt    = 0;
uint32_t lastGlanceUserAt  = 0;
uint32_t lastDoubleBlinkAt = 0;

// Full server reply parsed from /converse (replaces scattered out-params).
struct BrainReply {
  String emotion      = "neutral";
  float  intensity    = 0.7f;
  String tone         = "neutral";
  String context      = "idle";
  float  expressivity = 0.5f;
  String reply;
  bool   sing         = false;
  bool   doSpeak      = true;
  String soundEffect  = "none";
  int    preRespMs    = 300;
  int    postRespMs   = 200;
};
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

void applyDeviceUiSettings(const AppSettings &s) {
  face.setMouthAnim(s.mouthAnim);
  face.setSpeechCaptionMode(s.speechCaption);
}

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

bool askBrain(const uint8_t *wav, size_t size, BrainReply &out);
void speak(const String &text, bool sing, const String &emotion = "happy");
void playMusic(const String &videoId, const String &title);
void playStory(const String &storyId, const String &title);
bool checkWakePhrase(const uint8_t *wav, size_t size, String *commandOut = nullptr);
void proactiveIdleRemark();
void pollDevCommand();
void tickIdleBehavior();
void applyToneMicro(const String &tone);
void queueDevFace(const String &emotion, bool bored, uint32_t holdMs, uint8_t vibingMic,
                  uint16_t vibingFloor, uint16_t vibingCeil);
void queueDevSpeak(const String &text, const String &emotion);
void processDevCommands();
void openSettings();
void touchCaress();
void touchHit();

// Single tap waking is deferred briefly so a quick second tap can register as a "hit".
volatile bool g_wakePending = false;
uint32_t g_wakePendingAt = 0;

String g_musicVideoId;
String g_musicTitle;
String g_musicNowTitle;
String g_musicNowVideoId;
volatile bool g_musicPlayPending = false;
volatile bool g_musicPlaying = false;
volatile bool g_musicStreaming = false;
volatile bool g_musicStopRequested = false;
static TaskHandle_t g_musicTaskHandle = nullptr;

struct StoryCue {
  uint32_t atMs;
  Emotion emotion;
};
static constexpr int kStoryMaxCues = 24;
static StoryCue g_storyCues[kStoryMaxCues];
static int g_storyCueCount = 0;
static int g_storyCueNext = 0;
static uint32_t g_storyPlayStart = 0;
static volatile bool g_storyActive = false;
static volatile bool g_storyAmpPending = false;
static String g_storyId;
static String g_storyTitle;
static TaskHandle_t g_storyTaskHandle = nullptr;

static void storyClearCues() {
  g_storyCueCount = 0;
  g_storyCueNext = 0;
  g_storyPlayStart = 0;
  g_storyActive = false;
  g_storyAmpPending = false;
}

static void storyLoadCues(JsonArray arr) {
  storyClearCues();
  for (JsonObject o : arr) {
    if (g_storyCueCount >= kStoryMaxCues) break;
    g_storyCues[g_storyCueCount].atMs = o["at_ms"] | 0u;
    const char *em = o["emotion"] | "neutral";
    g_storyCues[g_storyCueCount].emotion = emotionFromString(String(em));
    g_storyCueCount++;
  }
}

static void storyTickEmotions() {
  if (!g_storyActive || g_storyPlayStart == 0) return;
  const uint32_t elapsed = millis() - g_storyPlayStart;
  bool changed = false;
  while (g_storyCueNext < g_storyCueCount && g_storyCues[g_storyCueNext].atMs <= elapsed) {
    face.setEmotion(g_storyCues[g_storyCueNext].emotion);
    emotionHoldUntil = 0;
    g_storyCueNext++;
    changed = true;
  }
  if (changed) face.redraw();
}

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

static const char* devEmotionSound(const String &em) {
  if (em == "happy" || em == "excited") return "power_up";
  if (em == "love")                     return "laugh";
  if (em == "sad")                      return "yawn";
  if (em == "sleepy")                   return "yawn";
  if (em == "angry")                    return "glitch";
  if (em == "dizzy")                    return "glitch";
  if (em == "surprised")                return "beep";
  if (em == "confused")                 return "beep";
  if (em == "cool")                     return "beep";
  return nullptr;
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
    const char *sfx = devEmotionSound(g_devFaceEmotion);
    if (sfx) playSoundEffect(sfx);
  }
  if (g_devSpeakPending && state == State::Sleeping && !g_musicTaskHandle && !g_storyTaskHandle) {
    g_devSpeakPending = false;
    String text = g_devSpeakText;
    String em = g_devSpeakEmotion;
    // Notify/dev speak: no pisar vibing si el usuario ya está en ese modo.
    if (!face.isVibing()) {
      face.setEmotion(emotionFromString(em));
    }
    face.update();
    Serial.printf("local dev speak [%s] vibing=%d: %s\n", em.c_str(), face.isVibing(), text.c_str());
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

struct StoryTaskArgs {
  String storyId;
  String title;
};

static void playStoryTask(void *arg) {
  auto *a = static_cast<StoryTaskArgs *>(arg);
  playStory(a->storyId, a->title);
  delete a;
  g_storyTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

static void startStoryAsync(const String &storyId, const String &title) {
  if (g_storyTaskHandle || g_musicTaskHandle) return;
  g_storyId = storyId;
  g_storyTitle = title;
  auto *a = new StoryTaskArgs{storyId, title};
  xTaskCreatePinnedToCore(playStoryTask, "story", 32768, a, 5, &g_storyTaskHandle, 1);
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
  face.setTopTitle(String("En cola: ") + g_musicTitle);
  face.update();
}

void requestMusicStop() {
  g_musicStopRequested = true;
  g_musicPlayPending = false;
}

// Physical-touch feelings (instant, no LLM): petting him = tender; poking/hitting = startled.
void touchCaress() {
  Serial.println("touch: caress -> love");
  face.setEmotion(Emotion::Love);
  face.setTopTitle("");
  face.update();
  playSoundEffect("laugh");
  emotionHoldUntil = millis() + 2600;
  lastActivityMs = millis();
}

void touchHit() {
  Serial.println("touch: hit -> angry + shake");
  face.setEmotion(Emotion::Angry);
  face.setTopTitle("");
  face.shake();
  face.update();
  playSoundEffect("glitch");
  emotionHoldUntil = millis() + 1800;
  lastActivityMs = millis();
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
  if (rms < 90.0f && peak < 380) return 0;

  float pk = sqrtf((float)peak);
  float rm = sqrtf(rms);
  float pkN = (pk - 3.5f) / 26.0f;
  float rmN = (rm - 5.0f) / 26.0f;
  if (pkN < 0.0f) pkN = 0.0f;
  if (rmN < 0.0f) rmN = 0.0f;
  if (pkN > 1.0f) pkN = 1.0f;
  if (rmN > 1.0f) rmN = 1.0f;

  float mix = pkN * 0.88f + rmN * 0.12f;
  float levelF = powf(mix, 0.38f) * 100.0f;
  levelF = fminf(100.0f, levelF * 1.35f);
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
  static constexpr size_t kFacePumpChunk = 512;  // 256 muestras @ 16 kHz (~16 ms)

  auto spectroPlayback = [&]() -> bool {
    return enjoyMusic || (face.isVibing() && face.isTalking());
  };
  // 30 fps durante audio. Más alto (83 fps) bloquea el DMA I2S → underrun (golpe).
  // Story mode 3 usa 10 000 ms para diagnosticar si face.update() es la causa del golpe.
  const uint32_t kMouthFrameMs =
      (g_storyActive && g_settings.storyPlayMode == 3) ? 10000u : 33u;

  auto pumpFaceIfDue = [&]() {
    if (millis() - lastMouthUpdate >= kMouthFrameMs) {
      storyTickEmotions();
      face.update();
      lastMouthUpdate = millis();
    }
  };

  // I2S write con yield: evita congelar pantalla cuando el DMA está lleno.
  auto playMonoYield = [&](const uint8_t *data, size_t bytes) -> size_t {
    bytes &= ~1u;
    if (bytes < 2 || !g_i2sTxRunning) return 0;
    size_t total = 0;
    while (total < bytes && !g_musicStopRequested) {
      if (g_storyActive && g_storyPlayStart == 0) g_storyPlayStart = millis();
      if (g_storyAmpPending) {
        digitalWrite(PIN_SPK_EN, LOW);
        g_storyAmpPending = false;
      }
      size_t want = min(bytes - total, kFacePumpChunk);
      want &= ~1u;
      if (want < 2) break;
      size_t n = i2s.write(data + total, want);
      if (n == 0) {
        face.update();
        lastMouthUpdate = millis();
        g_webAdmin.loop();
        delay(1);
        continue;
      }
      if (n >= 2) {
        if (singingMode) face.feedPlaybackMouth(100);
        else face.feedPlaybackPcm((const int16_t *)(data + total), n / 2);
      }
      total += n;
      pumpFaceIfDue();
    }
    return total;
  };

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
      written += blockI2s ? playMonoYield(pair, 2) : playMonoPcm16(i2s, pair, 2, false);
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
      written += blockI2s ? playMonoYield(p, len) : playMonoPcm16(i2s, p, len, false);
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
      face.setTopTitle(*titleOnFirstPcm);
      face.setTalking(true);
      if (singingMode) {
        face.setSinging(true);
        face.setEmotion(Emotion::Happy);
      }
      face.update();
      showedTitle = true;
    }

    size_t pushed = 0;
    while (pushed < pcmLen) {
      size_t chunk = min(pcmLen - pushed, ringFree());
      if (chunk == 0) break;
      ringPush(netBuf + pcmOff + pushed, chunk);
      pushed += chunk;
      pumpFaceIfDue();
    }
    pumpFaceIfDue();
    return true;
  };

  auto pumpNetworkBurst = [&]() -> bool {
    bool got = false;
    for (int i = 0; i < 12 && ringFree() >= kIoChunk; i++) {
      if (pumpNetwork()) got = true;
      else break;
      pumpFaceIfDue();
    }
    return got;
  };

  auto playFromRing = [&](bool useFade) {
    while (ringCount >= kIoChunk && !g_musicStopRequested) {
      // Rellenar la red MIENTRAS se drena el ring (productor+consumidor en el mismo loop):
      // evita la fase "vaciar todo y luego leer red" que dejaba el DMA I2S sin datos y
      // provocaba el golpe rítmico (underrun) en música/historia.
      if (ringCount < kHighWater) pumpNetwork();
      size_t n = ringPop(playBuf, min(sizeof(playBuf), ringCount));
      if (n > 0) pcmWritten += writePcmAligned(playBuf, n, useFade, true);
      pumpFaceIfDue();
    }
    if (ringCount >= 2 && !g_musicStopRequested) {
      size_t n = ringPop(playBuf, ringCount & ~1u);
      if (n > 0) pcmWritten += writePcmAligned(playBuf, n, useFade, true);
      pumpFaceIfDue();
    }
  };

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    if (g_musicStopRequested) break;

    if (spectroPlayback() && primed && ringCount < kHighWater) pumpNetworkBurst();

    while (ringCount < kLowWater && pumpNetwork()) {
      pumpFaceIfDue();
    }

    if (!primed) {
      while (ringCount < kPrimeBytes && pumpNetwork()) {
        pumpFaceIfDue();
      }
      if (ringCount < kPrimeBytes && stream->available() == 0 && !stream->connected()) break;
      if (ringCount < kPrimeBytes && stream->available() == 0) {
        g_webAdmin.loop();
        pumpFaceIfDue();
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
          pumpFaceIfDue();
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
        pumpFaceIfDue();
        delay(1);
        continue;
      }
      if (ringCount == 0) continue;
    }

    if (spectroPlayback()) {
      while (ringCount < kLowWater && pumpNetwork()) {
        pumpFaceIfDue();
      }
      playFromRing(false);
    } else {
      size_t n = ringPop(playBuf, min(sizeof(playBuf), ringCount));
      if (n > 0) pcmWritten += writePcmAligned(playBuf, n, true, true);
    }

    g_webAdmin.loop();
    pumpFaceIfDue();
  }

  // Vaciar ring al final — fade-out suave (evita golpe al cortar I2S/amp en historia/TTS)
  static constexpr size_t kFadeOutSamples = 2400;  // ~150 ms @ 16 kHz
  const size_t tailSampleCount = ringCount / 2;
  size_t tailSamplePos = 0;
  while (ringCount > 0) {
    size_t n = ringPop(playBuf, min(sizeof(playBuf), ringCount));
    if (n >= 2) {
      int16_t *samples = (int16_t *)playBuf;
      const size_t ns = n / 2;
      for (size_t i = 0; i < ns; i++, tailSamplePos++) {
        const size_t fromEnd = (tailSampleCount > tailSamplePos)
                                   ? (tailSampleCount - tailSamplePos - 1)
                                   : 0;
        if (fromEnd < kFadeOutSamples) {
          const float g = (float)fromEnd / (float)kFadeOutSamples;
          samples[i] = (int16_t)(samples[i] * g);
        }
      }
      pcmWritten += writePcmAligned(playBuf, n, false, true);
    }
    pumpFaceIfDue();
  }
  if (hasCarry) hasCarry = false;

  // Vaciar DMA I2S con silencio antes de apagar TX (evita click de corte)
  if (pcmWritten > 0 && g_i2sTxRunning && !g_musicStopRequested) {
    static int16_t silence[512];
    memset(silence, 0, sizeof(silence));
    for (int i = 0; i < 10; i++) {
      playMonoYield((const uint8_t *)silence, sizeof(silence));
    }
  }

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
  face.setMouthAnim(g_settings.mouthAnim);
  face.setSpeechCaptionMode(g_settings.speechCaption);
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
  Serial.printf("Ready - web admin + touch  build=%s\n", FIRMWARE_BUILD_ID);
  lastActivityMs = millis();
}

void loop() {
  face.setMouthAnim(g_settings.mouthAnim);  // keep the talking-mouth style in sync (web admin/menu)
  // Mientras hay tarea de música, ella dibuja la UI (prefetch); durante el stream
  // solo STOP/volumen desde aquí — evita dos hilos tocando SPI (rayas en pantalla).
  if (g_musicTaskHandle || g_storyTaskHandle) {
    g_webAdmin.loop();
    if (g_musicTaskHandle && g_musicStreaming) {
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
      if (face.isVibing() && millis() - lastVibingMic > 16) {
        lastVibingMic = millis();
        uint8_t bands[12];
        pauseWakeListener(false);
        uint32_t peak = recorder.captureVibingBands(i2s, bands, 12, 32);
        resumeWakeListener();
        face.setVibingSpectrum(bands, 12, peak);
        lastActivityMs = millis();
      }

      // Deferred wake: a single tap fires Listening only after a short window passes with
      // no second tap (so a quick double-tap can be read as a "hit" instead).
      if (g_wakePending && millis() >= g_wakePendingAt) {
        g_wakePending = false;
        face.clearMicLevel();
        state = State::Listening;
        break;
      }

      // Touch gestures: gear -> Settings; long-press -> Settings; swipe on his face ->
      // caress; quick double-tap on his face -> hit; single tap -> wake (deferred above).
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
          if (!longHandled) {              // short gesture
            int moved = abs(lastX - pressX) + abs(lastY - pressY);
            bool gear = Face::gearHit(pressX, pressY, gfx.width()) || Face::gearHit(lastX, lastY, gfx.width());
            bool onBody = Face::bodyHit(pressX, pressY, gfx.width());
            static uint32_t lastBodyTapMs = 0;
            if (gear) {
              openSettings();
            } else if (onBody && moved > 34) {
              touchCaress();                 // swipe across his face -> caress
            } else if (onBody && (millis() - lastBodyTapMs) < 380) {
              lastBodyTapMs = 0;
              g_wakePending = false;
              touchHit();                    // quick second tap -> got hit/poked
            } else {
              lastBodyTapMs = millis();
              g_wakePending = true;          // single tap -> wake after the double-tap window
              g_wakePendingAt = millis() + 380;
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
        face.setListening(true);
      }

      pauseWakeListener();
      if (!usePendingWake) {
        rec = recorder.record(i2s, onMicLevel);
        face.setListening(false);
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

      BrainReply resp;
      bool ok = askBrain(rec.wav, rec.size, resp);
      free(rec.wav);

      if (ok) {
        // M4: store context + expressivity for idle behavior
        g_context     = resp.context;
        g_expressivity = resp.expressivity;

        if (resp.soundEffect.length() > 0 && resp.soundEffect != "none") {
          playSoundEffect(resp.soundEffect.c_str());
          delay(150);
        }
        if (!face.isVibing()) {
          face.setEmotion(emotionFromString(resp.emotion), resp.intensity);
          applyToneMicro(resp.tone);  // M9: tone → face microexpression
        }
        face.showText("");
        face.update();
        if (resp.doSpeak && resp.reply.length() > 0) {
          if (resp.preRespMs > 0) delay(resp.preRespMs);  // M5: pre-response pause
          speak(resp.reply, resp.sing, resp.emotion);
          if (resp.postRespMs > 0) delay(resp.postRespMs); // M8: post-response silence
        } else if (g_musicPlayPending) {
          face.setEmotion(Emotion::Happy);
          face.update();
          Serial.println("Music queued from voice — starting playback");
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

  tickIdleBehavior();
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

    face.beginReplyCaption(text);
    face.setTalking(true);
    if (sing) {
      face.setSinging(true);
      face.setEmotion(Emotion::Happy);
      face.setMouthAmplitude(100);
    }
    // Mantener vibing durante TTS (ecualizador espejo); solo canto fuerza happy.
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
    face.setTopTitle(String("Esperando PC... ") + label);
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
  face.setTopTitle(String("Esperando PC... ") + label);
  face.update();

  HTTPClient http;
  WiFiClient client;
  client.setNoDelay(true);
  client.setTimeout((uint32_t)MUSIC_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setConnectTimeout(30000);
  http.setTimeout((int32_t)MUSIC_HTTP_TIMEOUT_MS);
  // HTTP/1.1 + Content-Length (server acumula PCM completo antes de enviar).
  // Con HTTP/1.0 uvicorn respondía HTTP/1.1 chunked; los chunk headers se
  // interpretaban como PCM y causaban el golpe rítmico cada 16 KB.

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

  // Modo música = cara Vibing: boca espectrograma reactiva al PCM + notas musicales
  // flotando (igual que el gesto vibing). El título va en el sprite (doble búfer).
  face.setEmotion(Emotion::Vibing);
  face.setTalking(true);
  face.setTopTitle(label);
  MusicScreen::drawControls(gfx, g_settings.volume);
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
  face.setEmotion(Emotion::Neutral);
  face.clearTopTitle();
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

void playStory(const String &storyId, const String &title) {
  pauseWakeListener();

  if (WiFi.status() != WL_CONNECTED) {
    face.showText("WiFi perdido", TFT_RED);
    face.update();
    resumeWakeListener();
    return;
  }

  const String label = title.length() > 0 ? title : storyId;
  g_storyCueNext = 0;
  if (g_storyCueCount > 0) face.setEmotion(g_storyCues[0].emotion);
  face.setTalking(true);
  face.update();

  HTTPClient http;
  WiFiClient client;
  client.setTimeout((uint32_t)MUSIC_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setConnectTimeout(30000);
  http.setTimeout((int32_t)MUSIC_HTTP_TIMEOUT_MS);
  // HTTP/1.1 + Content-Length (server sends Response, not StreamingResponse).
  // Con HTTP/1.0 uvicorn podía responder en HTTP/1.1 chunked; los chunk headers
  // se interpretaban como PCM y causaban el golpe rítmico cada 16 KB.

  const String url = String(BRAIN_SERVER_URL) + "/story/play?id=" + storyId;
  Serial.printf("STORY GET %s\n", url.c_str());
  if (!http.begin(client, url)) {
    face.showText("Historia: error HTTP", TFT_RED);
    face.update();
    resumeWakeListener();
    return;
  }
  http.addHeader("Connection", "close");
  addBrainDeviceHeaders(http);

  const int code = http.GET();
  Serial.printf("STORY GET code=%d len=%d\n", code, http.getSize());
  if (code != 200) {
    const String err = http.getString();
    Serial.printf("story error: %d %s\n", code, err.c_str());
    face.showText(code < 0 ? "Historia error red" : "Historia fallo", TFT_RED);
    face.update();
    http.end();
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
    resumeWakeListener();
    return;
  }

  // storyPlayMode test variants (selectable from web admin):
  //   0 (baseline) : rawPcm=true, enjoyMusic=true,  amp deferred inside write
  //   1 (tts-like) : rawPcm=true, enjoyMusic=false, amp explicit before stream
  //   2 (mus+amp)  : rawPcm=true, enjoyMusic=true,  amp explicit before stream
  //   3 (no-face)  : rawPcm=true, enjoyMusic=true,  amp explicit, face update c/10s
  //                  DIAGNOSTIC: si mode 3 no tiene golpe → face.update() es el culpable
  const uint8_t sMode     = g_settings.storyPlayMode;
  const bool useRawPcm    = true;
  const bool useEnjoyMus  = (sMode != 1);
  const bool ampDeferred  = (sMode == 0);

  g_storyActive = true;
  g_storyPlayStart = 0;
  g_storyAmpPending = ampDeferred;

  if (!ampDeferred) {
    digitalWrite(PIN_SPK_EN, LOW);  // modes 1-3: enable amp before stream (like music/tts)
  }

  Serial.printf("STORY mode=%u rawPcm=%d enjoyMus=%d ampEarly=%d\n",
                sMode, useRawPcm, useEnjoyMus, !ampDeferred);

  // Reset auto-gain ceiling so story mouth is not silenced by a previous
  // loud music session that inflated _ampEnvMax way above story audio levels.
  face.resetAmpGain();

  const size_t pcmWritten =
      playHttpPcmStream(http, stream, remaining, false, false, nullptr,
                        useRawPcm, nullptr, useEnjoyMus);

  delay(20);
  digitalWrite(PIN_SPK_EN, HIGH);
  http.end();

  g_storyActive = false;
  storyClearCues();
  face.setTalking(false);
  face.setEmotion(Emotion::Neutral);
  face.showText("");
  face.update();
  endPlayback(i2s);
  Serial.printf("story played %u bytes PCM\n", pcmWritten);
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

bool askBrain(const uint8_t *wav, size_t size, BrainReply &out) {
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

  out.emotion      = doc["emotion"].as<String>();
  out.intensity    = max(0.0f, min(1.0f, doc["intensity"] | 0.7f));
  out.tone         = doc["tone"] | "neutral";
  out.context      = doc["context"] | "idle";
  out.expressivity = max(0.1f, min(1.0f, doc["expressivity"] | 0.5f));
  out.reply        = doc["reply"].as<String>();
  out.sing         = doc["sing"] | false;
  out.doSpeak      = doc["speak"] | true;
  out.soundEffect  = doc["sound_effect"] | "none";
  out.preRespMs    = max(0, min(2000, doc["pre_response_ms"] | 300));
  out.postRespMs   = max(0, min(2000, doc["post_response_ms"] | 200));

  // Smart speaker: the brain resolved a song to play -> stream it instead of speaking.
  String musicId = doc["music"]["video_id"] | "";
  if (musicId.length() > 0) {
    String musicTitle = doc["music"]["title"] | "";
    Serial.printf("Music intent -> %s (%s)\n", musicTitle.c_str(), musicId.c_str());
    queueMusicPlay(musicId, musicTitle);
    out.doSpeak = false;
    out.reply = "";
  }
  Serial.printf("Heard: %s\nReply [%s@%.2f tone=%s ctx=%s pre=%dms] speak=%d sing=%d sfx=%s: %s\n",
                doc["heard"].as<const char *>(), out.emotion.c_str(), out.intensity,
                out.tone.c_str(), out.context.c_str(), out.preRespMs,
                out.doSpeak, out.sing, out.soundEffect.c_str(), out.reply.c_str());
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
    const char *sfx = devEmotionSound(em);
    if (sfx) playSoundEffect(sfx);
    return;
  }

  if (strcmp(type, "speak") == 0) {
    String text = cmd["text"] | "";
    String em = cmd["emotion"] | "happy";
    if (text.length() == 0) return;
    face.setEmotion(emotionFromString(em));
    face.update();
    Serial.printf("dev speak [%s]: %s\n", em.c_str(), text.c_str());
    speak(text, false, em);
    emotionHoldUntil = millis() + 8000;
    return;
  }

  if (strcmp(type, "story") == 0) {
    String sid = cmd["story_id"] | "";
    String title = cmd["title"] | "";
    if (sid.length() == 0) return;
    JsonArray timeline = cmd["timeline"].as<JsonArray>();
    if (!timeline.isNull()) storyLoadCues(timeline);
    Serial.printf("dev story: %s (%s) cues=%d\n", sid.c_str(), title.c_str(), g_storyCueCount);
    startStoryAsync(sid, title);
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
  float intensity = max(0.0f, min(1.0f, doc["intensity"] | 0.7f));
  String reply = doc["reply"].as<String>();
  bool sing = doc["sing"] | false;
  bool doSpeak = doc["speak"] | true;
  if (reply.length() == 0) return;

  Serial.printf("idle remark [%s] intensity=%.2f: %s\n", emotion.c_str(), intensity, reply.c_str());
  face.setEmotion(emotionFromString(emotion), intensity);
  face.update();
  if (doSpeak) speak(reply, sing, emotion);
  emotionHoldUntil = millis() + 8000;
}

// M3 Idle Behavior + M2 Microexpressions — called from loop() every iteration.
void tickIdleBehavior() {
  // Only run while the face is idle (not recording, processing, or playing back).
  if (state != State::Sleeping || g_musicTaskHandle || g_storyTaskHandle) return;

  uint32_t now = millis();
  bool holdActive = emotionHoldUntil && now < emotionHoldUntil;

  // M10 Priority: emergency context overrides everything — snap to angry + focus gaze.
  if (g_context == "emergency") {
    if (idlePhase != IdlePhase::Active) {
      idlePhase = IdlePhase::Active;
      face.setEmotion(Emotion::Angry, 0.8f);
      face.setMicroGaze(0, 0, 5000);  // focus_lock
      face.setBored(false);
      face.update();
      nextMicroExpAt = now + 60000;
    }
    return;
  }

  // M4: scale idle intensities by context expressivity (1.0 at default 0.5).
  float exprFactor = g_expressivity / 0.5f;

  // Determine target idle phase from time-since-last-activity.
  IdlePhase target;
  if (holdActive) {
    target = IdlePhase::Active;
  } else if (now - lastActivityMs < 15000UL) {
    target = IdlePhase::Present;
  } else if (now - lastActivityMs < 60000UL) {
    target = IdlePhase::Waiting;
  } else {
    target = IdlePhase::SemiDormant;
  }

  // Apply phase transitions.
  if (target != idlePhase) {
    idlePhase = target;
    switch (idlePhase) {
      case IdlePhase::Active:
        nextMicroExpAt = now + 10000;
        break;
      case IdlePhase::Present:
        nextMicroExpAt = now + 6000 + random(4000);
        break;
      case IdlePhase::Waiting:
        face.setEmotion(Emotion::Neutral, max(0.10f, min(0.40f, 0.2f * exprFactor)));
        face.setBored(false);
        face.update();
        nextMicroExpAt = now + 4000 + random(4000);
        break;
      case IdlePhase::SemiDormant:
        face.setEmotion(Emotion::Sleepy, max(0.08f, min(0.30f, 0.15f * exprFactor)));
        face.setBored(true);
        face.update();
        nextMicroExpAt = now + 30000;
        break;
    }
  }

  // Microexpressions — only in Present/Waiting; skip while active or semi-dormant.
  if (idlePhase == IdlePhase::Active || idlePhase == IdlePhase::SemiDormant) return;
  if (holdActive || now < nextMicroExpAt) return;

  // Pick a weighted random microexpression.
  uint8_t r = random(0, 3);
  bool fired = false;

  if (r == 0 && now - lastGlanceUpAt > 10000UL) {
    face.setMicroGaze(0, -8, 600);
    lastGlanceUpAt = now;
    fired = true;
  } else if (r == 1 && now - lastGlanceUserAt > 8000UL) {
    face.setMicroGaze(0, 0, 400);
    lastGlanceUserAt = now;
    fired = true;
  } else if (r == 2 && now - lastDoubleBlinkAt > 12000UL) {
    face.triggerDoubleBlink();
    lastDoubleBlinkAt = now;
    fired = true;
  }

  nextMicroExpAt = now + (fired ? (7000 + random(8000)) : (2000 + random(3000)));
}

// M9 Acting Layer — map conversational tone to face microexpression.
void applyToneMicro(const String &tone) {
  if (tone == "ironic" || tone == "sarcastic") face.setMicroGaze(-8, 2, 500);
  else if (tone == "curious")                  face.setMicroGaze(4, -5, 400);
  else if (tone == "worried")                  face.setMicroGaze(0, -8, 600);
  else if (tone == "proud")                    face.setMicroGaze(0, 0, 800);
  else if (tone == "excited")                  face.triggerDoubleBlink();
  else if (tone == "urgent")                   face.setMicroGaze(0, 0, 1200);
  else if (tone == "empathetic")               face.setMicroGaze(2, -3, 500);
}

// Modal settings menu (opened from the gear). Blocks until the user taps "Guardar",
// then applies and persists the choices and restores the idle screen.
void openSettings() {
  Serial.println("Settings opened");
  g_settingsScreen.run(gfx, g_settings, codec);

  applySettingsGlobals(g_settings, g_wakePhraseIdx, g_voiceWakeEnabled, g_idleRemarksEnabled);
  syncWakeNetFromSettings();
  applyDeviceUiSettings(g_settings);
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
