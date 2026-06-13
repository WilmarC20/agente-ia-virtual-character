// Records speech from the I2S microphone into PSRAM and wraps it as WAV.
#pragma once

#include <ESP_I2S.h>
#include "config.h"
#include "audio_output.h"

struct Recording {
  uint8_t *wav = nullptr;
  size_t size = 0;
};

typedef void (*MicLevelCallback)(uint32_t rms);

class AudioRecorder {
public:
  static constexpr bool kCaptureStereo = (MIC_I2S_MODE == I2S_SLOT_MODE_STEREO);

  static void drainI2s(I2SClass &i2s, uint32_t ms = I2S_DRAIN_MS) {
    drainI2sRx(i2s, ms);
  }

  // noVoiceTimeoutMs: how long to wait for speech before giving up (short for the
  // PC-wake probe so the loop stays responsive). quiet: suppress serial logging
  // (the wake probe runs repeatedly, so it shouldn't spam the log).
  Recording record(I2SClass &i2s, MicLevelCallback onLevel = nullptr,
                   uint32_t noVoiceTimeoutMs = RECORD_NO_VOICE_MS, bool quiet = false) {
    if (!quiet) Serial.println("record: start");
    prepareCapture(i2s);

    Recording rec;
    const size_t maxSamples = AUDIO_SAMPLE_RATE * RECORD_MAX_SECONDS;
    const size_t wavBytes = 44 + maxSamples * 2;

    uint8_t *buf = (uint8_t *)heap_caps_malloc(wavBytes, MALLOC_CAP_SPIRAM);
    if (!buf) return rec;
    int16_t *pcm = (int16_t *)(buf + 44);

    const size_t chunkSamples = 512;
    int16_t raw[chunkSamples * 2];
    size_t total = 0;
    uint32_t silenceMs = 0;
    bool voiceStarted = false;
    uint32_t emptyReads = 0;
    uint32_t noVoiceMs = 0;
    const uint32_t deadlineMs = millis() + (RECORD_MAX_SECONDS + 2) * 1000UL;
    i2s.setTimeout(100);

    while (total + chunkSamples <= maxSamples && (int32_t)(deadlineMs - millis()) > 0) {
      size_t rawBytes = kCaptureStereo ? chunkSamples * 4 : chunkSamples * 2;
      size_t nbytes = readI2sRx(i2s, reinterpret_cast<char *>(raw), rawBytes, 100);
      if (nbytes == 0) {
        if (++emptyReads > 200) break;
        delay(5);
        continue;
      }
      emptyReads = 0;
      size_t rawCount = nbytes / 2;
      if (rawCount == 0) continue;

      size_t got = 0;
      uint64_t acc = 0;
      if (kCaptureStereo) {
        size_t frames = rawCount / 2;
        if (frames == 0) continue;
        for (size_t i = 0; i < frames; i++) {
          int32_t l = raw[i * 2];
          int32_t r = raw[i * 2 + 1];
          int32_t s = (abs(l) >= abs(r)) ? l : r;
          int32_t boosted = (int32_t)(s * MIC_PCM_GAIN);
          if (boosted > 32767) boosted = 32767;
          if (boosted < -32768) boosted = -32768;
          pcm[total + i] = (int16_t)boosted;
          acc += (uint64_t)(s * s);
        }
        got = frames;
      } else {
        for (size_t i = 0; i < rawCount; i++) {
          int32_t boosted = (int32_t)(raw[i] * MIC_PCM_GAIN);
          if (boosted > 32767) boosted = 32767;
          if (boosted < -32768) boosted = -32768;
          pcm[total + i] = (int16_t)boosted;
          acc += (uint64_t)(raw[i] * raw[i]);
        }
        got = rawCount;
      }

      uint32_t rms = sqrtf((float)(acc / got));
      total += got;
      if (onLevel) onLevel(rms);

      uint32_t chunkMs = got * 1000 / AUDIO_SAMPLE_RATE;
      if (rms > VAD_THRESHOLD) {
        voiceStarted = true;
        silenceMs = 0;
        noVoiceMs = 0;
      } else if (voiceStarted) {
        silenceMs += chunkMs;
        if (silenceMs >= RECORD_SILENCE_MS) break;
      } else {
        noVoiceMs += chunkMs;
        if (noVoiceMs >= noVoiceTimeoutMs) {
          if (!quiet) Serial.println("record: no voice timeout");
          break;
        }
      }
    }

    i2s.setTimeout(1000);

    uint32_t durationMs = total * 1000 / AUDIO_SAMPLE_RATE;
    if (!voiceStarted || durationMs < RECORD_MIN_MS) {
      if (!quiet) Serial.printf("record: no valid audio (voice=%d ms=%u)\n", voiceStarted, durationMs);
      free(buf);
      return rec;
    }

    writeWavHeader(buf, total * 2);
    rec.wav = buf;
    rec.size = 44 + total * 2;
    if (!quiet) Serial.printf("Recorded %u samples, %u ms @ %u Hz\n", total, durationMs, AUDIO_SAMPLE_RATE);
    return rec;
  }

  static constexpr uint32_t VAD_THRESHOLD = 120;

private:
  void writeWavHeader(uint8_t *h, uint32_t pcmBytes) {
    uint32_t sr = AUDIO_SAMPLE_RATE;
    uint32_t byteRate = sr * 2;
    memcpy(h, "RIFF", 4);
    u32(h + 4, 36 + pcmBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    u32(h + 16, 16);
    u16(h + 20, 1);
    u16(h + 22, 1);
    u32(h + 24, sr);
    u32(h + 28, byteRate);
    u16(h + 32, 2);
    u16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    u32(h + 40, pcmBytes);
  }
  static void u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
  static void u16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
};
