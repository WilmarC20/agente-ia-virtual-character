// ES8311 codec driver for ES3C28P — clock table from working retro-go / es8311 example.
#pragma once

#include <Wire.h>
#include "config.h"

class ES8311 {
public:
  bool begin(TwoWire &wire = Wire) {
    _wire = &wire;

    _wire->beginTransmission(ES8311_I2C_ADDR);
    if (_wire->endTransmission() != 0) return false;

    writeReg(0x00, 0x1F);
    delay(20);
    writeReg(0x00, 0x00);
    writeReg(0x00, 0x80);
    delay(10);

    writeReg(0x01, 0x3F);

    uint8_t reg06 = readReg(0x06);
    reg06 &= ~(1 << 5);
    writeReg(0x06, reg06);

    if (!configureClock(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE)) return false;

    uint8_t reg00 = readReg(0x00);
    reg00 &= 0xBF;
    writeReg(0x00, reg00);

    writeReg(0x09, (3 << 2));
    writeReg(0x0A, (3 << 2));

    writeReg(0x0D, 0x01);
    writeReg(0x0E, 0x02);
    writeReg(0x12, 0x00);
    writeReg(0x13, 0x10);
    writeReg(0x1C, 0x6A);
    writeReg(0x37, 0x08);

    setMicGain(7);
    setAdcVolume(0xFF);
    setPlaybackVolumePercent(TTS_VOLUME_PERCENT);

    if (!configureClock(AUDIO_MCLK_HZ, AUDIO_SAMPLE_RATE)) return false;

    Serial.printf("ES8311 OK: %u Hz, MCLK %u Hz\n", AUDIO_SAMPLE_RATE, AUDIO_MCLK_HZ);
    return true;
  }

  bool configureClock(uint32_t mclkHz, uint32_t sampleHz) {
    const CoeffDiv *coeff = findCoeff(mclkHz, sampleHz);
    if (!coeff) {
      Serial.printf("ES8311: no coeff mclk=%u rate=%u\n", mclkHz, sampleHz);
      return false;
    }

    uint8_t regv = readReg(0x02);
    regv &= 0x07;
    regv |= (coeff->pre_div - 1) << 5;
    regv |= coeff->pre_multi << 3;
    writeReg(0x02, regv);

    writeReg(0x03, (coeff->fs_mode << 6) | coeff->adc_osr);
    writeReg(0x04, coeff->dac_osr);

    regv = ((coeff->adc_div - 1) << 4) | (coeff->dac_div - 1);
    writeReg(0x05, regv);

    regv = readReg(0x06);
    regv &= 0xE0;
    if (coeff->bclk_div < 19) {
      regv |= (coeff->bclk_div - 1);
    } else {
      regv |= coeff->bclk_div;
    }
    writeReg(0x06, regv);

    regv = readReg(0x07);
    regv &= 0xC0;
    regv |= coeff->lrck_h;
    writeReg(0x07, regv);
    writeReg(0x08, coeff->lrck_l);

    return true;
  }

  void setMicGain(uint8_t steps) {
    if (steps > 7) steps = 7;
    writeReg(0x14, 0x10 | steps);
  }

  void setAdcVolume(uint8_t v) { writeReg(0x17, v); }
  void setDacVolume(uint8_t v) { writeReg(0x32, v); }

  // Reg 0x32 DAC volume: 0x00 = mute, 0xBF ~= 0 dB, 0xFF ~= +32 dB.
  void setPlaybackVolumePercent(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint8_t v = (uint8_t)((255UL * percent) / 100UL);
    setDacVolume(v);
    Serial.printf("ES8311 DAC vol %u%% reg=0x%02X\n", percent, v);
  }

  uint8_t readReg(uint8_t reg) {
    _wire->beginTransmission(ES8311_I2C_ADDR);
    _wire->write(reg);
    _wire->endTransmission(false);
    _wire->requestFrom((uint8_t)ES8311_I2C_ADDR, (uint8_t)1);
    return _wire->available() ? _wire->read() : 0;
  }

private:
  struct CoeffDiv {
    uint32_t mclk;
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
  };

  static constexpr CoeffDiv kCoeffs[] = {
      // 16 kHz @ MCLK = Fs * 384 (ES3C28P default)
      {6144000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
      {12288000, 16000, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  };

  TwoWire *_wire = nullptr;

  const CoeffDiv *findCoeff(uint32_t mclkHz, uint32_t sampleHz) const {
    for (const CoeffDiv &c : kCoeffs) {
      if (c.mclk == mclkHz && c.rate == sampleHz) {
        return &c;
      }
    }
    return nullptr;
  }

  void writeReg(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(ES8311_I2C_ADDR);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
  }
};
