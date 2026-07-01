// Reproducción directa de emisoras HTTP (MP3) en el ESP — el PC solo entrega stream_url.
#pragma once

#include <Arduino.h>
#include "Audio.h"
#include "config.h"
#include "es8311.h"
#include "settings.h"
#include "audio_output.h"

extern ES8311 codec;
extern volatile bool g_musicStopRequested;

class RadioPlayer {
public:
  bool play(I2SClass &i2s, const String &url, void (*uiPump)(const char *phase));
  void stop();
  void teardown();

private:
  Audio *_audio = nullptr;

  void ensureAudio();
  void releaseI2s(I2SClass &i2s);
  void prepareCodec();
  void restoreCodec();
};

inline void RadioPlayer::ensureAudio() {
  // ESP32-audioI2S (Arduino 3.x): Audio(uint8_t i2sPort). Tras hardEndI2s() NUM_0 queda libre.
  if (!_audio) _audio = new Audio(I2S_NUM_0);
}

inline void RadioPlayer::teardown() {
  if (_audio) {
    _audio->stopSong();
    delete _audio;
    _audio = nullptr;
  }
}

inline void RadioPlayer::prepareCodec() {
  codec.configureClock(RADIO_MCLK_HZ, RADIO_I2S_HZ);
  codec.setPlaybackVolumePercent(g_settings.volume);
}

inline void RadioPlayer::restoreCodec() {
  codec.configureClock(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE);
}

inline void RadioPlayer::releaseI2s(I2SClass &i2s) { releaseI2sBusForRadio(i2s); }

inline void RadioPlayer::stop() {
  if (_audio) _audio->stopSong();
}

inline bool RadioPlayer::play(I2SClass &i2s, const String &url, void (*uiPump)(const char *phase)) {
  if (!url.length()) return false;

  Serial.println("RADIO play: teardown+release");
  teardown();
  releaseI2s(i2s);
  Serial.printf("RADIO after release: tx=%p rx=%p\n", (void *)i2s.txChan(), (void *)i2s.rxChan());
  ensureAudio();

  digitalWrite(PIN_SPK_EN, LOW);
  prepareCodec();

  // setPinout(BCLK, LRC, DOUT, MCK) — API Arduino 3.x / audioI2S nueva.
  Serial.println("RADIO setPinout...");
  _audio->setPinout(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_MCLK);
  Serial.println("RADIO setPinout done");
  _audio->forceMono(true);
  _audio->setOutput48KHz(true);
  _audio->setVolume(21);
  _audio->setConnectionTimeout(10000, 20000);

  if (uiPump) uiPump("Conectando...");
  Serial.printf("RADIO connect %s\n", url.c_str());
  if (!_audio->connecttohost(url.c_str())) {
    Serial.println("RADIO connecttohost failed");
    digitalWrite(PIN_SPK_EN, HIGH);
    teardown();
    restoreCodec();
    return false;
  }

  const uint32_t t0 = millis();
  bool heardSound = false;

  while (_audio->isRunning() && !g_musicStopRequested) {
    _audio->loop();
    if (g_musicStopRequested) {
      stop();
      break;
    }

    const uint16_t vu = _audio->getVUlevel();
    const uint32_t sr = _audio->getSampleRate();
    if (vu > 3 || sr >= 8000) {
      heardSound = true;
      if (uiPump) uiPump("En vivo");
    } else if (uiPump) {
      uiPump("Buffering...");
    }

    if (!heardSound && (millis() - t0) > (uint32_t)RADIO_STALL_TIMEOUT_MS) {
      Serial.println("RADIO stall timeout");
      stop();
      break;
    }

    if (uiPump) uiPump(nullptr);
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  stop();
  delay(40);
  digitalWrite(PIN_SPK_EN, HIGH);
  teardown();
  restoreCodec();
  return heardSound && !g_musicStopRequested;
}

inline RadioPlayer g_radioPlayer;
