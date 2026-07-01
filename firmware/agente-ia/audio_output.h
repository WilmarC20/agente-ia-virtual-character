// ES3C28P: TTS = TX mono (RX off). Tras playback = restoreMicBusAfterPlayback() en agente-ia.ino.
#pragma once

#include <ESP_I2S.h>
#include <driver/i2s_std.h>
#include <driver/i2s_common.h>
#include <esp_log.h>
#include "config.h"
#include "es8311.h"
#include "settings.h"

extern ES8311 codec;
extern AppSettings g_settings;

bool restoreMicBusAfterPlayback(I2SClass &i2s);

static bool g_i2sTxRunning = false;
static bool g_i2sRxRunning = false;

inline esp_err_t safeChanDisable(i2s_chan_handle_t chan) {
  if (!chan) return ESP_ERR_INVALID_ARG;
  esp_err_t e = i2s_channel_disable(chan);
  return (e == ESP_OK || e == ESP_ERR_INVALID_STATE) ? ESP_OK : e;
}

inline esp_err_t safeChanEnable(i2s_chan_handle_t chan) {
  if (!chan) return ESP_ERR_INVALID_ARG;
  return i2s_channel_enable(chan);
}

inline i2s_std_clk_config_t makeI2sClkCfg() {
  i2s_std_clk_config_t clk = {};
  clk.sample_rate_hz = AUDIO_SAMPLE_RATE;
  clk.clk_src = I2S_CLK_SRC_DEFAULT;
  clk.ext_clk_freq_hz = 0;
  clk.mclk_multiple = (i2s_mclk_multiple_t)I2S_MCLK_MULTIPLE;
  return clk;
}

inline i2s_std_slot_config_t makeI2sSlotCfg(i2s_slot_mode_t mode, i2s_std_slot_mask_t mask) {
  i2s_std_slot_config_t slot = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, mode);
  slot.slot_mask = mask;
  return slot;
}

inline void syncI2sRunningFlags(I2SClass &i2s) {
  g_i2sTxRunning = i2s.txChan() != nullptr;
  g_i2sRxRunning = i2s.rxChan() != nullptr;
}

inline size_t readI2sRx(I2SClass &i2s, char *buf, size_t maxBytes, uint32_t timeoutMs) {
  (void)timeoutMs;
  if (maxBytes == 0 || buf == nullptr || i2s.rxChan() == nullptr) return 0;
  i2s.setTimeout(100);
  return i2s.readBytes(buf, maxBytes);
}

inline void drainI2sRx(I2SClass &i2s, uint32_t ms) {
  if (i2s.rxChan() == nullptr) return;
  char junk[512 * 4];
  uint32_t deadline = millis() + ms;
  i2s.setTimeout(50);
  while ((int32_t)(deadline - millis()) > 0) {
    if (readI2sRx(i2s, junk, sizeof(junk), 50) == 0) delay(5);
  }
  i2s.setTimeout(1000);
}

inline bool applyI2sStdClock(I2SClass &i2s, uint32_t rate, i2s_mclk_multiple_t mclkMult) {
  i2s_std_clk_config_t clk_cfg = {};
  clk_cfg.sample_rate_hz = rate;
  clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  clk_cfg.ext_clk_freq_hz = 0;
  clk_cfg.mclk_multiple = mclkMult;

  bool ok = true;
  if (i2s.txChan() != nullptr) {
    if (g_i2sTxRunning) safeChanDisable(i2s.txChan());
    g_i2sTxRunning = false;
    ok &= i2s_channel_reconfig_std_clock(i2s.txChan(), &clk_cfg) == ESP_OK;
    if (safeChanEnable(i2s.txChan()) == ESP_OK) g_i2sTxRunning = true;
  }
  if (i2s.rxChan() != nullptr) {
    safeChanDisable(i2s.rxChan());
    g_i2sRxRunning = false;
    ok &= i2s_channel_reconfig_std_clock(i2s.rxChan(), &clk_cfg) == ESP_OK;
    if (safeChanEnable(i2s.rxChan()) == ESP_OK) g_i2sRxRunning = true;
  }
  return ok;
}

// 1ª grabación (sin TTS previo): solo vaciar buffer RX.
inline void prepareCapture(I2SClass &i2s) {
  drainI2sRx(i2s, I2S_DRAIN_MS);
}

// TTS: RX off, TX mono on, MCLK x384 (checkpoint audio-2x-ok).
inline void preparePlayback(I2SClass &i2s, bool muteDac = false) {
  if (muteDac) {
    codec.setDacVolume(0);
  } else {
    codec.setPlaybackVolumePercent(g_settings.volume);
  }

  if (i2s.rxChan() != nullptr) {
    safeChanDisable(i2s.rxChan());
    g_i2sRxRunning = false;
  }

  if (i2s.txChan() != nullptr) {
    safeChanDisable(i2s.txChan());
    i2s_std_clk_config_t clk = makeI2sClkCfg();
    i2s_std_slot_config_t slot = makeI2sSlotCfg(I2S_SLOT_MODE_MONO, (i2s_std_slot_mask_t)SPK_I2S_SLOT);
    i2s_channel_reconfig_std_clock(i2s.txChan(), &clk);
    i2s_channel_reconfig_std_slot(i2s.txChan(), &slot);
    esp_err_t e = safeChanEnable(i2s.txChan());
    g_i2sTxRunning = (e == ESP_OK || e == ESP_ERR_INVALID_STATE);
  } else {
    g_i2sTxRunning = false;
  }

  codec.configureClock(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE);
  Serial.printf("TTS TX mono running=%d\n", g_i2sTxRunning);
}

inline size_t playMonoPcm16(I2SClass &i2s, const uint8_t *data, size_t bytes, bool blockComplete = true) {
  bytes &= ~1u;
  if (bytes < 2 || !g_i2sTxRunning) return 0;
  if (!blockComplete) return i2s.write(data, bytes);

  size_t total = 0;
  while (total < bytes) {
    size_t n = i2s.write(data + total, bytes - total);
    if (n == 0) {
      delay(1);
      continue;
    }
    total += n;
  }
  return total;
}

inline void endPlayback(I2SClass &i2s) {
  if (i2s.txChan() != nullptr && g_i2sTxRunning) {
    safeChanDisable(i2s.txChan());
    g_i2sTxRunning = false;
  }
  restoreMicBusAfterPlayback(i2s);
}

// ESP_I2S::end() aborta si disable falla (p. ej. canal ya deshabilitado) y NO
// ejecuta i2s_del_channel -> puerto I2S queda ocupado -> "no available channel".
inline void forceDeleteI2sChannel(i2s_chan_handle_t chan) {
  if (!chan) return;
  esp_err_t e = i2s_channel_disable(chan);
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_LOGW("agenteIA", "i2s disable err=%d", (int)e);
  }
  e = i2s_del_channel(chan);
  if (e != ESP_OK) ESP_LOGW("agenteIA", "i2s del err=%d", (int)e);
}

inline bool hardEndI2s(I2SClass &i2s, const char *tag) {
  if (i2s.txChan()) safeChanEnable(i2s.txChan());
  if (i2s.rxChan()) safeChanEnable(i2s.rxChan());
  g_i2sTxRunning = false;
  g_i2sRxRunning = false;
  if (i2s.end()) return true;
  ESP_LOGW("agenteIA", "%s: i2s.end() failed — force delete channels", tag);
  forceDeleteI2sChannel(i2s.txChan());
  forceDeleteI2sChannel(i2s.rxChan());
  return true;
}

inline void releaseI2sBusForRadio(I2SClass &i2s) { hardEndI2s(i2s, "releaseI2sBusForRadio"); }
