#pragma once

// Fase 2: transiciones boot/idle/conversation/dashboard/...
enum class AuraScene {
  Boot, Idle, Conversation, Dashboard, Music, Sleep
};

class SceneManager {
 public:
  void setScene(AuraScene s) { _scene = s; }
  AuraScene scene() const { return _scene; }

 private:
  AuraScene _scene = AuraScene::Dashboard;
};
