#pragma once

#include <Arduino.h>

// Indicador de reproducción musical — estado mínimo para music_screen overlay.
class MusicPlayerWidget {
 public:
  void setPlaying(bool on, const char *title = nullptr) {
    _playing = on;
    _title = title ? title : "";
  }

  bool playing() const { return _playing; }
  const String &title() const { return _title; }

 private:
  bool _playing = false;
  String _title;
};
