#pragma once

#include <Arduino.h>

// Indicador de reproducción musical — estado mínimo para music_screen overlay.
class MusicPlayerWidget {
 public:
  void setTitle(const char *title) { _title = title ? title : ""; }

  void setPlaying(bool on, const char *title = nullptr) {
    _playing = on;
    _title = title ? title : "";
    if (!on) _status = "";
  }

  void setStatus(const char *status) { _status = status ? status : ""; }

  bool playing() const { return _playing; }
  const String &title() const { return _title; }
  const String &status() const { return _status; }

 private:
  bool _playing = false;
  String _title;
  String _status;
};
