// HTTP admin UI for the ESP32 — configure device settings from a browser.
// Serves at http://<device-ip>/ while the main loop keeps running.
#pragma once

#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "settings.h"
#include "es8311.h"
#include "config.h"
#include "secrets.h"

// Cola de reproducción musical (implementado en agente-ia.ino).
void queueMusicPlay(const String &videoId, const String &title);
void requestMusicStop();
extern volatile bool g_musicPlaying;
extern String g_musicNowTitle;
extern String g_musicNowVideoId;

// Called from agente-ia.ino after settings change (touch menu or web admin).
void syncWakeNetFromSettings();

// Pruebas locales de gesto / TTS (panel /test del ESP).
void queueDevFace(const String &emotion, bool bored, uint32_t holdMs, uint8_t vibingMic = 0);
void queueDevSpeak(const String &text, const String &emotion);

class WebAdmin {
public:
  void begin(AppSettings &settings, ES8311 &codec,
             uint8_t &wakePhraseIdx, bool &voiceWakeEnabled, bool &idleRemarksEnabled) {
    _settings = &settings;
    _codec = &codec;
    _wakePhraseIdx = &wakePhraseIdx;
    _voiceWakeEnabled = &voiceWakeEnabled;
    _idleRemarksEnabled = &idleRemarksEnabled;

    _server.on("/", HTTP_GET, [this]() { _server.send_P(200, "text/html", kAdminHtml); });
    _server.on("/test", HTTP_GET, [this]() { _server.send_P(200, "text/html", kDevTestHtml); });
    _server.on("/api/settings", HTTP_GET, [this]() { handleGetSettings(); });
    _server.on("/api/settings", HTTP_POST, [this]() { handlePostSettings(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/dev/face", HTTP_POST, [this]() { handlePostDevFace(); });
    _server.on("/api/dev/speak", HTTP_POST, [this]() { handlePostDevSpeak(); });
    _server.on("/api/music/play", HTTP_POST, [this]() { handlePostMusic(); });
    _server.on("/api/music/stop", HTTP_POST, [this]() { handlePostMusicStop(); });
    _server.on("/api/music/status", HTTP_GET, [this]() { handleGetMusicStatus(); });
    _server.onNotFound([this]() { _server.send(404, "text/plain", "Not found"); });
    _server.begin();
    Serial.printf("Web admin: http://%s/\n", WiFi.localIP().toString().c_str());
  }

  void loop() { _server.handleClient(); }

private:
  WebServer _server{80};
  AppSettings *_settings = nullptr;
  ES8311 *_codec = nullptr;
  uint8_t *_wakePhraseIdx = nullptr;
  bool *_voiceWakeEnabled = nullptr;
  bool *_idleRemarksEnabled = nullptr;

  void applyLive() {
    applySettingsGlobals(*_settings, *_wakePhraseIdx, *_voiceWakeEnabled, *_idleRemarksEnabled);
    _codec->setPlaybackVolumePercent(_settings->volume);
    syncWakeNetFromSettings();
  }

  void settingsToJson(JsonDocument &doc) {
    doc["volume"] = _settings->volume;
    doc["voice_wake"] = _settings->voiceWake;
#if ENABLE_WAKEWORD
    doc["hi_esp_wake"] = _settings->hiEspWake;
    doc["hi_esp_available"] = true;
#else
    doc["hi_esp_wake"] = false;
    doc["hi_esp_available"] = false;
#endif
    doc["idle_remarks"] = _settings->idleRemarks;
    doc["phrase_idx"] = _settings->phraseIdx;
    JsonArray phrases = doc["wake_phrases"].to<JsonArray>();
    for (int i = 0; i < WAKE_PRESET_COUNT; i++) phrases.add(WAKE_PRESET_LABELS[i]);
    doc["wake_phrase"] = WAKE_PRESET_LABELS[_settings->phraseIdx];
    doc["brain_url"] = BRAIN_SERVER_URL;
  }

  void handleGetSettings() {
    JsonDocument doc;
    settingsToJson(doc);
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
  }

  void handlePostSettings() {
    if (!_server.hasArg("plain")) {
      _server.send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
      _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }

    if (doc["volume"].is<uint8_t>()) {
      uint8_t v = doc["volume"];
      if (v > 100) v = 100;
      _settings->volume = v;
    }
    if (doc["voice_wake"].is<bool>()) _settings->voiceWake = doc["voice_wake"];
#if ENABLE_WAKEWORD
    if (doc["hi_esp_wake"].is<bool>()) _settings->hiEspWake = doc["hi_esp_wake"];
#endif
    if (doc["idle_remarks"].is<bool>()) _settings->idleRemarks = doc["idle_remarks"];
    if (doc["phrase_idx"].is<uint8_t>()) {
      uint8_t idx = doc["phrase_idx"];
      if (idx < WAKE_PRESET_COUNT) _settings->phraseIdx = idx;
    }

    saveSettings(*_settings);
    applyLive();

    JsonDocument out;
    settingsToJson(out);
    out["ok"] = true;
    String body;
    serializeJson(out, body);
    _server.send(200, "application/json", body);
  }

  void handleStatus() {
    JsonDocument doc;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
    doc["brain_url"] = BRAIN_SERVER_URL;
    doc["free_heap"] = ESP.getFreeHeap();
    extern bool g_wakeNetRunning;
    doc["wake_net_running"] = g_wakeNetRunning;
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
  }

  static bool validDevEmotion(const String &e) {
    return e == "neutral" || e == "happy" || e == "sad" || e == "angry" ||
           e == "surprised" || e == "thinking" || e == "sleepy" || e == "love" ||
           e == "excited" || e == "cool" || e == "confused" || e == "dizzy" ||
           e == "vibing";
  }

  void handlePostDevFace() {
    if (!_server.hasArg("plain")) {
      _server.send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
      _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    String emotion = doc["emotion"] | "neutral";
    emotion.toLowerCase();
    if (!validDevEmotion(emotion)) {
      _server.send(400, "application/json", "{\"error\":\"unknown emotion\"}");
      return;
    }
    bool bored = doc["bored"] | false;
    uint32_t hold = doc["hold_ms"] | 8000;
    if (hold < 1000) hold = 1000;
    if (hold > 120000) hold = 120000;
    int vmic = doc["vibing_mic"] | 0;
    uint8_t vibingMic = (vmic >= 50 && vmic <= 300) ? (uint8_t)vmic : 0;
    queueDevFace(emotion, bored, hold, vibingMic);
    _server.send(200, "application/json", "{\"ok\":true,\"emotion\":\"" + emotion + "\"}");
  }

  void handlePostDevSpeak() {
    if (!_server.hasArg("plain")) {
      _server.send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
      _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    String text = doc["text"] | "";
    text.trim();
    if (text.length() == 0) {
      _server.send(400, "application/json", "{\"error\":\"empty text\"}");
      return;
    }
    if (text.length() > 500) text = text.substring(0, 497) + "...";
    String emotion = doc["emotion"] | "happy";
    emotion.toLowerCase();
    if (!validDevEmotion(emotion)) emotion = "happy";
    queueDevSpeak(text, emotion);
    _server.send(202, "application/json", "{\"ok\":true,\"queued\":true}");
  }

  void handlePostMusic() {
    if (!_server.hasArg("plain")) {
      _server.send(400, "application/json", "{\"error\":\"missing body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
      _server.send(400, "application/json", "{\"error\":\"invalid json\"}");
      return;
    }
    const char *vid = doc["video_id"] | "";
    if (!vid[0]) vid = doc["id"] | "";
    if (!vid[0]) {
      _server.send(400, "application/json", "{\"error\":\"missing video_id\"}");
      return;
    }
    String title = doc["title"] | "";
    queueMusicPlay(String(vid), title);
    _server.send(202, "application/json", "{\"ok\":true,\"queued\":true}");
  }

  void handlePostMusicStop() {
    requestMusicStop();
    _server.send(200, "application/json", "{\"ok\":true,\"stop\":true}");
  }

  void handleGetMusicStatus() {
    JsonDocument doc;
    doc["playing"] = g_musicPlaying;
    doc["title"] = g_musicNowTitle;
    doc["video_id"] = g_musicNowVideoId;
    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
  }

  static const char kAdminHtml[] PROGMEM;
  static const char kDevTestHtml[] PROGMEM;
};

// Embedded admin page — same dark theme as server/sing_test.html
const char WebAdmin::kAdminHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>agenteIA — dispositivo</title>
<style>
:root{--bg:#0f1419;--panel:#1a2332;--accent:#3dd6c6;--accent-dim:#2a9d8f;--text:#e8eef4;--muted:#8b9cb3;--danger:#ff6b6b}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;font-family:"Segoe UI",system-ui,sans-serif;background:radial-gradient(ellipse at 20% 0%,#1e3a5f 0%,var(--bg) 55%);color:var(--text);padding:1.25rem}
.wrap{max-width:640px;margin:0 auto}
h1{font-size:1.4rem;margin:0 0 .2rem}
.sub{color:var(--muted);font-size:.88rem;margin-bottom:1.25rem}
.panel{background:var(--panel);border:1px solid #2a3548;border-radius:12px;padding:1.1rem;margin-bottom:1rem}
.panel h2{font-size:.95rem;margin:0 0 .85rem;color:var(--accent);font-weight:600}
label{display:block;font-size:.78rem;color:var(--muted);margin-bottom:.3rem}
select,input[type=range]{width:100%;background:#0d1117;border:1px solid #334155;border-radius:8px;color:var(--text);padding:.5rem;font-size:.95rem}
.row{display:flex;align-items:center;justify-content:space-between;gap:1rem;margin:.65rem 0}
.toggle{position:relative;width:48px;height:26px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.toggle span{position:absolute;inset:0;background:#334155;border-radius:13px;cursor:pointer;transition:.2s}
.toggle span:before{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.toggle input:checked+span{background:var(--accent-dim)}
.toggle input:checked+span:before{transform:translateX(22px)}
.vol-val{font-size:1.1rem;font-weight:700;color:var(--accent);min-width:3rem;text-align:right}
button.primary{background:linear-gradient(135deg,var(--accent),var(--accent-dim));color:#0a1628;border:none;border-radius:10px;padding:.8rem 1.5rem;font-size:1rem;font-weight:700;cursor:pointer;width:100%;margin-top:.5rem}
button.secondary{background:transparent;border:1px solid #334155;color:var(--text);border-radius:10px;padding:.65rem 1rem;font-size:.9rem;cursor:pointer;width:100%;margin-top:.5rem}
.msg{padding:.65rem .85rem;border-radius:8px;font-size:.88rem;margin-top:.75rem;display:none}
.msg.ok{display:block;background:#0d3328;color:#7dffc8;border:1px solid #1a6b52}
.msg.err{display:block;background:#3a1515;color:#ffb4b4;border:1px solid #6b2a2a}
a{color:var(--accent)}
.status{font-size:.8rem;color:var(--muted);line-height:1.5}
</style>
</head>
<body>
<div class="wrap">
<h1>agenteIA — dispositivo</h1>
<p class="sub">Configuración del ESP32. Pruebas de cara/voz en <a href="/test">/test</a>. Cerebro en <a id="brainLink" href="#">admin del servidor</a>.</p>

<div class="panel">
<h2>Activación por voz</h2>
<div class="row"><div><strong>Palabra gatillo (PC)</strong><br><span class="status">Whisper en el servidor escucha la frase elegida</span></div>
<label class="toggle"><input type="checkbox" id="voiceWake"><span></span></label></div>
<div class="row" id="hiEspRow"><div><strong>Hi ESP (WakeNet)</strong><br><span class="status">Detección local en el chip</span></div>
<label class="toggle"><input type="checkbox" id="hiEspWake"><span></span></label></div>
<label for="phrase">Frase gatillo</label>
<select id="phrase"></select>
<p class="status" style="margin-top:.5rem">El toque en pantalla siempre activa al asistente.</p>
</div>

<div class="panel">
<h2>Audio y comportamiento</h2>
<div class="row"><span>Volumen</span><span class="vol-val" id="volVal">80%</span></div>
<input type="range" id="volume" min="0" max="100" step="1"/>
<div class="row"><div><strong>Comentarios espontáneos</strong><br><span class="status">Habla solo tras estar inactivo</span></div>
<label class="toggle"><input type="checkbox" id="idleRemarks"><span></span></label></div>
</div>

<div class="panel">
<h2>Estado</h2>
<p class="status" id="statusLine">Cargando...</p>
</div>

<button class="primary" id="saveBtn">Guardar cambios</button>
<button class="secondary" id="reloadBtn">Recargar</button>
<div class="msg" id="msg"></div>
</div>
<script>
const $=id=>document.getElementById(id);
let cfg={};
async function load(){
  const r=await fetch('/api/settings');cfg=await r.json();
  $('volume').value=cfg.volume;$('volVal').textContent=cfg.volume+'%';
  $('voiceWake').checked=cfg.voice_wake;
  $('hiEspWake').checked=cfg.hi_esp_wake;
  $('idleRemarks').checked=cfg.idle_remarks;
  $('hiEspRow').style.display=cfg.hi_esp_available?'flex':'none';
  const sel=$('phrase');sel.innerHTML='';
  (cfg.wake_phrases||[]).forEach((p,i)=>{const o=document.createElement('option');o.value=i;o.textContent=p;sel.appendChild(o);});
  sel.value=cfg.phrase_idx;
  const brain=(cfg.brain_url||'').replace(/\/$/,'');
  $('brainLink').href=brain+'/admin';
  const st=await fetch('/api/status');const s=await st.json();
  $('statusLine').innerHTML=`IP <strong>${s.ip}</strong> · RSSI ${s.rssi} dBm · heap ${s.free_heap} B`+
    (s.wake_net_running!==undefined?` · WakeNet ${s.wake_net_running?'activo':'apagado'}`:'');
}
function showMsg(t,ok){const m=$('msg');m.textContent=t;m.className='msg '+(ok?'ok':'err');}
$('volume').oninput=()=>$('volVal').textContent=$('volume').value+'%';
$('saveBtn').onclick=async()=>{
  const body={volume:+$('volume').value,voice_wake:$('voiceWake').checked,
    hi_esp_wake:$('hiEspWake').checked,idle_remarks:$('idleRemarks').checked,phrase_idx:+$('phrase').value};
  try{const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const j=await r.json();if(!r.ok)throw new Error(j.error||r.status);showMsg('Guardado correctamente',true);cfg=j;load();}
  catch(e){showMsg('Error: '+e.message,false);}
};
$('reloadBtn').onclick=load;
load();
</script>
</body>
</html>)rawliteral";

const char WebAdmin::kDevTestHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es"><head>
<meta charset="utf-8"/><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>agenteIA — pruebas en dispositivo</title>
<style>
:root{--bg:#0f1419;--panel:#1a2332;--accent:#3dd6c6;--text:#e8eef4;--muted:#8b9cb3}
*{box-sizing:border-box}body{margin:0;padding:1rem;font-family:system-ui,sans-serif;background:var(--bg);color:var(--text)}
.wrap{max-width:520px;margin:0 auto}h1{font-size:1.2rem;margin:0 0 .3rem}.sub{color:var(--muted);font-size:.85rem;margin-bottom:1rem;line-height:1.4}
.panel{background:var(--panel);border:1px solid #2a3548;border-radius:12px;padding:1rem;margin-bottom:1rem}
.panel h2{font-size:.9rem;color:var(--accent);margin:0 0 .6rem}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(88px,1fr));gap:.4rem}
button.face{background:#0d1117;border:1px solid #334155;color:var(--text);border-radius:8px;padding:.5rem .3rem;font-size:.75rem;cursor:pointer}
button.face.active,button.face:hover{border-color:var(--accent);color:var(--accent)}
textarea{width:100%;min-height:80px;background:#0d1117;border:1px solid #334155;border-radius:8px;color:var(--text);padding:.5rem;font:inherit}
button.primary{width:100%;margin-top:.5rem;padding:.7rem;border:none;border-radius:10px;background:linear-gradient(135deg,#3dd6c6,#2a9d8f);color:#0a1628;font-weight:700;cursor:pointer}
.msg{margin-top:.6rem;padding:.5rem;border-radius:8px;font-size:.85rem;display:none}.msg.ok{display:block;background:#0d3328;color:#7dffc8}.msg.err{display:block;background:#3a1515;color:#ffb4b4}
a{color:var(--accent)}label{font-size:.78rem;color:var(--muted)}
</style></head><body><div class="wrap">
<h1>Pruebas en el robot</h1>
<p class="sub">El gesto se ve en la <strong>pantalla física</strong>. Vista previa en PC: <a id="brainDev" href="#">/dev del cerebro</a>. <a href="/">← Config</a></p>
<div class="panel"><h2>Gestos</h2><div class="grid" id="grid"></div>
<label style="display:flex;align-items:center;gap:.4rem;margin-top:.5rem"><input type="checkbox" id="bored"> Modo aburrido</label>
<div class="msg" id="faceMsg"></div></div>
<div class="panel"><h2>Hablar texto</h2>
<textarea id="text" placeholder="Texto para TTS…"></textarea>
<button class="primary" id="speakBtn">Hablar en el parlante</button>
<div class="msg" id="speakMsg"></div></div>
</div><script>
const EM=['neutral','happy','sad','angry','surprised','thinking','sleepy','love','excited','cool','confused','dizzy','vibing'];
const $=id=>document.getElementById(id);let cur='neutral';
function msg(el,t,ok){el.textContent=t;el.className='msg '+(ok?'ok':'err');}
fetch('/api/settings').then(r=>r.json()).then(c=>{
  const b=(c.brain_url||'').replace(/\/$/,'');$('brainDev').href=b+'/dev';
});
EM.forEach(e=>{const b=document.createElement('button');b.className='face'+(e==='neutral'?' active':'');b.textContent=e;
b.onclick=async()=>{cur=e;document.querySelectorAll('.face').forEach(x=>x.classList.remove('active'));b.classList.add('active');
try{const r=await fetch('/api/dev/face',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({emotion:e,bored:$('bored').checked,hold_ms:8000})});const j=await r.json();
if(!r.ok)throw new Error(j.error);msg($('faceMsg'),'Gesto '+e+' aplicado',true);}catch(err){msg($('faceMsg'),err.message,false);}};
$('grid').appendChild(b);});
$('speakBtn').onclick=async()=>{const t=$('text').value.trim();if(!t){msg($('speakMsg'),'Escribe texto',false);return;}
try{const r=await fetch('/api/dev/speak',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({text:t,emotion:cur})});const j=await r.json();if(!r.ok)throw new Error(j.error);
msg($('speakMsg'),'TTS en curso (puede tardar)',true);}catch(err){msg($('speakMsg'),err.message,false);}};
</script></body></html>)rawliteral";
