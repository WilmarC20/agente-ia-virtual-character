#pragma once

#include <Arduino.h>

// Escenas AURA — alineadas con sdk/contracts/scene_command.schema.json
enum class AuraScene : uint8_t {
  Boot,
  Idle,
  Conversation,
  Programming,
  Music,
  Guardian,
  Emergency,
  Camera,
  Dashboard,
  Sleep,
  Shutdown,
};

inline const char *auraSceneName(AuraScene s) {
  switch (s) {
    case AuraScene::Boot: return "boot";
    case AuraScene::Idle: return "idle";
    case AuraScene::Conversation: return "conversation";
    case AuraScene::Programming: return "programming";
    case AuraScene::Music: return "music";
    case AuraScene::Guardian: return "guardian";
    case AuraScene::Emergency: return "emergency";
    case AuraScene::Camera: return "camera";
    case AuraScene::Dashboard: return "dashboard";
    case AuraScene::Sleep: return "sleep";
    case AuraScene::Shutdown: return "shutdown";
    default: return "idle";
  }
}

inline AuraScene auraSceneFromName(const char *name) {
  if (!name) return AuraScene::Idle;
  if (strcasecmp(name, "boot") == 0) return AuraScene::Boot;
  if (strcasecmp(name, "conversation") == 0) return AuraScene::Conversation;
  if (strcasecmp(name, "programming") == 0) return AuraScene::Programming;
  if (strcasecmp(name, "music") == 0) return AuraScene::Music;
  if (strcasecmp(name, "guardian") == 0) return AuraScene::Guardian;
  if (strcasecmp(name, "emergency") == 0) return AuraScene::Emergency;
  if (strcasecmp(name, "camera") == 0) return AuraScene::Camera;
  if (strcasecmp(name, "dashboard") == 0) return AuraScene::Dashboard;
  if (strcasecmp(name, "sleep") == 0) return AuraScene::Sleep;
  if (strcasecmp(name, "shutdown") == 0) return AuraScene::Shutdown;
  return AuraScene::Idle;
}

class SceneManager {
 public:
  void setScene(AuraScene s, uint32_t transitionMs = 0) {
    _scene = s;
    _transitionUntil = transitionMs ? (millis() + transitionMs) : 0;
  }

  void setSceneByName(const char *name, uint32_t transitionMs = 0) {
    setScene(auraSceneFromName(name), transitionMs);
  }

  AuraScene scene() const { return _scene; }
  bool transitioning() const { return _transitionUntil && millis() < _transitionUntil; }

 private:
  AuraScene _scene = AuraScene::Dashboard;
  uint32_t _transitionUntil = 0;
};
