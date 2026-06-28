// Reply captions below the face: off (default), karaoke word, or ASCII plain.
#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

// 0 = oculto · 1 = karaoke (palabra a palabra) · 2 = texto ASCII sin tildes
enum SpeechCaptionMode : uint8_t {
  kSpeechCaptionOff = 0,
  kSpeechCaptionKaraoke = 1,
  kSpeechCaptionAscii = 2,
};

inline void foldSpanishAscii(const String &in, String &out) {
  out = "";
  out.reserve(in.length());
  for (unsigned i = 0; i < in.length();) {
    const uint8_t b0 = (uint8_t)in[i];
    if (b0 < 0x80) {
      const char c = in[i++];
      if (c == '\n' || c == '\r') {
        out += ' ';
      } else {
        out += c;
      }
      continue;
    }
    if (b0 == 0xC2 && i + 1 < in.length()) {
      const uint8_t b1 = (uint8_t)in[i + 1];
      if (b1 == 0xA1 || b1 == 0xBF) {  // ¡ ¿
        i += 2;
        continue;
      }
    }
    if (b0 == 0xC3 && i + 1 < in.length()) {
      const uint8_t b1 = (uint8_t)in[i + 1];
      char repl = 0;
      switch (b1) {
        case 0xA1: case 0x81: repl = 'a'; break;
        case 0xA9: case 0x89: repl = 'e'; break;
        case 0xAD: case 0x8D: repl = 'i'; break;
        case 0xB3: case 0x93: repl = 'o'; break;
        case 0xBA: case 0x9A: repl = 'u'; break;
        case 0xBC: case 0x9C: repl = 'u'; break;
        case 0xB1: case 0x91: repl = 'n'; break;
        default: break;
      }
      if (repl) {
        out += repl;
        i += 2;
        continue;
      }
    }
    i++;
  }
  out.trim();
  while (out.indexOf("  ") >= 0) {
    out.replace("  ", " ");
  }
}

class SpeechCaption {
public:
  static constexpr int kMaxWords = 32;

  void setMode(uint8_t mode) {
    _mode = (mode > kSpeechCaptionAscii) ? kSpeechCaptionOff : mode;
  }

  void begin(const String &utf8Text) {
    _active = false;
    _wordCount = 0;
    _wordIdx = 0;
    _startMs = 0;
    _msPerWord = 220;
    _ascii = "";
    if (_mode == kSpeechCaptionOff || utf8Text.length() == 0) return;

    foldSpanishAscii(utf8Text, _ascii);
    if (_ascii.length() == 0) return;

    tokenize(_ascii);
    if (_wordCount == 0) return;

    _active = true;
    _wordIdx = 0;
    const uint32_t estMs = (uint32_t)_ascii.length() * 48u + 350u;
    _msPerWord = estMs / (uint32_t)_wordCount;
    if (_msPerWord < 150) _msPerWord = 150;
    if (_msPerWord > 720) _msPerWord = 720;
  }

  void markPlaybackStart() {
    if (!_active || _mode != kSpeechCaptionKaraoke) return;
    _startMs = millis();
    _wordIdx = 0;
  }

  void end() { _active = false; _wordCount = 0; }

  bool active() const { return _active; }
  uint8_t mode() const { return _mode; }

  void tick(uint32_t now, bool talking) {
    if (!_active || _mode != kSpeechCaptionKaraoke || !talking || _startMs == 0) return;
    uint8_t idx = (uint8_t)((now - _startMs) / _msPerWord);
    if (idx >= _wordCount) idx = _wordCount - 1;
    if (idx != _wordIdx) _wordIdx = idx;
  }

  void draw(lgfx::LGFX_Device &gfx, int faceOffsetY, int faceH) {
    const int y = faceOffsetY + faceH + 2;
    const int h = gfx.height() - y;
    gfx.fillRect(0, y, gfx.width(), h, TFT_BLACK);
    if (!_active || _mode == kSpeechCaptionOff || h < 12) return;

    if (_mode == kSpeechCaptionKaraoke) {
      drawKaraoke(gfx, y, h);
    } else {
      drawAsciiWrap(gfx, y, h);
    }
  }

  bool needsRedraw(uint8_t &lastIdx) const {
    if (!_active || _mode != kSpeechCaptionKaraoke) return false;
    if (_wordIdx == lastIdx) return false;
    lastIdx = _wordIdx;
    return true;
  }

private:
  uint8_t _mode = kSpeechCaptionOff;
  bool _active = false;
  String _ascii;
  char _words[kMaxWords][20];
  uint8_t _wordCount = 0;
  uint8_t _wordIdx = 0;
  uint32_t _startMs = 0;
  uint32_t _msPerWord = 220;

  void tokenize(const String &text) {
    _wordCount = 0;
    int start = 0;
    while (start < (int)text.length() && _wordCount < kMaxWords) {
      while (start < (int)text.length() && text[start] == ' ') start++;
      if (start >= (int)text.length()) break;
      int end = start;
      while (end < (int)text.length() && text[end] != ' ') end++;
      const int len = end - start;
      if (len <= 0) break;
      int n = len;
      if (n > 18) n = 18;
      text.substring(start, start + n).toCharArray(_words[_wordCount], sizeof(_words[0]));
      _wordCount++;
      start = end;
    }
  }

  void drawKaraoke(lgfx::LGFX_Device &gfx, int y, int h) {
    if (_wordCount == 0) return;
    const char *word = _words[_wordIdx];

    gfx.setFont(&fonts::DejaVu18);
    gfx.setTextWrap(false);
    const int tw = gfx.textWidth(word);
    int tx = (gfx.width() - tw) / 2;
    if (tx < 4) tx = 4;
    const int ty = y + 6;
    gfx.setTextColor(0x39E7, TFT_BLACK);
    gfx.setCursor(tx, ty);
    gfx.print(word);

    if (_wordCount > 1 && h >= 28) {
      const int dotY = y + h - 10;
      const int span = min(11, (int)_wordCount);
      const int gap = 8;
      const int totalW = (span - 1) * gap;
      int x0 = (gfx.width() - totalW) / 2;
      const int seg = max(1, (int)_wordCount / span);
      for (int i = 0; i < span; i++) {
        const int wi = i * seg;
        const bool on = (wi <= (int)_wordIdx);
        gfx.fillCircle(x0 + i * gap, dotY, on ? 3 : 2, on ? 0x39E7 : 0x4208);
      }
    }
  }

  void drawAsciiWrap(lgfx::LGFX_Device &gfx, int y, int h) {
    gfx.setFont(&fonts::DejaVu12);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.setTextWrap(true);
    int tx = 8;
    if (tx < 2) tx = 2;
    gfx.setCursor(tx, y + 4);
    String line = _ascii;
    if (line.length() > 120) line = line.substring(0, 117) + "...";
    gfx.print(line);
  }
};
