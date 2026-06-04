// =============================================================================
// Publisher.cpp — Publication MQTT + SSE + Logs
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

// Tous les includes de libs externes ICI uniquement
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "Publisher.h"
#include "ZoneManager.h"
#include "CommandHandler.h"
#include "TempoManager.h"
#include "LinkyReader.h"
#include "ScheduleManager.h"

// =============================================================================
// PIMPL — objets réseau cachés du header
// =============================================================================

struct Publisher::Impl {
  WiFiClient   wifiClient;
  PubSubClient mqttClient{wifiClient};
  WiFiServer   sseServer{SSE_PORT};
  WiFiClient   sseClients[SSE_MAX_CLIENTS];
  bool         sseStarted = false;
};

Publisher  publisher;
Publisher* Publisher::_instance = nullptr;

// =============================================================================
// INITIALISATION
// =============================================================================

void Publisher::begin(ZoneManager*    zoneMgr,
                      CommandHandler* cmdHandler,
                      TempoManager*   tempoMgr) {
  _zoneMgr    = zoneMgr;
  _cmdHandler = cmdHandler;
  _tempoMgr   = tempoMgr;
  _instance   = this;

  _impl = new Impl();
  _impl->mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  _impl->mqttClient.setCallback(mqttCallbackStatic);
  _impl->mqttClient.setBufferSize(2048);
  // sseServer.begin() appelé dans update() après connexion WiFi

  getVersion(_fwVersion, sizeof(_fwVersion));
  LOG(LOG_MQTT, "Publisher ready MQTT=%s:%d SSE=:%d",
      MQTT_BROKER, MQTT_PORT, SSE_PORT);
}

// =============================================================================
// LOOP
// =============================================================================

void Publisher::update() {
  if (!_impl) return;
  if (WiFi.isConnected()) {
    if (!_impl->mqttClient.connected()) reconnectMQTT();
    else                                _impl->mqttClient.loop();
  }
}

// =============================================================================
// CONNEXION
// =============================================================================

bool Publisher::isConnected()   { return WiFi.isConnected(); }

bool Publisher::isMqttConnected() {
  return _impl && _impl->mqttClient.connected();
}

void Publisher::reconnectMQTT() {
  if (!_impl) return;
  if (millis() - _lastMqttAttempt < MQTT_RECONNECT_MS) return;
  _lastMqttAttempt = millis();
  if (!WiFi.isConnected()) return;

  if (_impl->mqttClient.connect(MQTT_CLIENT_ID)) {
    LOG(LOG_MQTT, "MQTT connected");
    _impl->mqttClient.subscribe("heatingCtrl_v4/zone/+/set");
    _impl->mqttClient.subscribe("heatingCmd");
    _impl->mqttClient.subscribe("heatingCtrl_v4/tempo/setCounters"); // MAJ manuelle compteurs
    _impl->mqttClient.subscribe("utcClock");
    _impl->mqttClient.subscribe("utcClock/#");
    _impl->mqttClient.subscribe("ecowittDatas");
    _impl->mqttClient.subscribe("overrideValues");
    publishLog("INFO","MQTT","Connected fw=%s", _fwVersion);
  } else {
    LOG(LOG_MQTT, "MQTT failed rc=%d", _impl->mqttClient.state());
  }
}

// =============================================================================
// SSE
// =============================================================================

void Publisher::_sseAcceptClients() {
  if (!_impl || !_impl->sseServer.hasClient()) return;
  WiFiClient c = _impl->sseServer.accept();
  if (!c) return;
  for (uint8_t i = 0; i < SSE_MAX_CLIENTS; i++) {
    if (!_sseClientActive[i] || !_impl->sseClients[i].connected()) {
      _impl->sseClients[i] = c;
      _sseClientActive[i]  = true;
      _sseHandshake(&_impl->sseClients[i]);
      LOG(LOG_WEB, "SSE client %d connected", i);
      publishFullState();
      return;
    }
  }
  c.stop();
}

void Publisher::_sseHandshake(void* ptr) {
  WiFiClient& c = *(WiFiClient*)ptr;

  // Lit toute la requête HTTP jusqu'à la ligne vide
  unsigned long t = millis();
  while (c.connected() && millis() - t < 1000) {
    if (c.available()) {
      String line = c.readStringUntil('\n');
      if (line == "\r" || line.length() <= 1) break;
    }
  }

  // Répond avec headers SSE
  c.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "X-Accel-Buffering: no\r\n"
    "\r\n"
  );
  c.flush();
}

void Publisher::_ssePurgeDisconnected() {
  if (!_impl) return;
  for (uint8_t i = 0; i < SSE_MAX_CLIENTS; i++) {
    if (_sseClientActive[i] && !_impl->sseClients[i].connected()) {
      _impl->sseClients[i].stop();
      _sseClientActive[i] = false;
    }
  }
}

void Publisher::sseBroadcast(const char* json) {
  // Polling côté JS — pas besoin de push serveur
  (void)json;
}

void Publisher::wsBroadcast(const char* json) {
  (void)json;
}

// =============================================================================
// HEADER JSON COMMUN
// =============================================================================

void Publisher::_addHeader(char* buf, size_t bufLen) const {
  snprintf(buf, bufLen,
    "{\"ts\":%lu,\"fw\":\"%s\",\"sysState\":\"%s\"",
    (unsigned long)_timestamp,
    _fwVersion,
    systemStateToStr(_sysState));
}

// =============================================================================
// PUBLICATIONS
// =============================================================================

void Publisher::publishFullState() {
  JsonDocument doc;
  doc["ts"]       = _timestamp;
  doc["fw"]       = _fwVersion;
  doc["sysState"] = systemStateToStr(_sysState);

  JsonArray zones = doc["zones"].to<JsonArray>();
  if (_zoneMgr) {
    for (uint8_t z = 1; z <= NUM_ZONES; z++) {
      const ZoneData& zd = _zoneMgr->getZone(z);
      JsonObject zo = zones.add<JsonObject>();
      zo["id"]    = z;
      zo["state"] = zoneStateToStr(zd.state);
      zo["cmd"]   = heatingCmdToStr(zd.currentCmd);
      zo["pos"]   = zd.posState;
      zo["neg"]   = zd.negState;
      zo["timer"] = zd.timerActive;
    }
  }

  JsonObject temps = doc["temps"].to<JsonObject>();
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    char k[8]; snprintf(k, sizeof(k), "z%d", i+1);
    temps[k] = _tempZone[i];
  }

  char buf[768];
  serializeJson(doc, buf, sizeof(buf));
  _publishAndBroadcast(MQTT_TOPIC_STATUS, buf);
}

void Publisher::publishZoneState(uint8_t zoneId) {
  if (!_zoneMgr || zoneId < 1 || zoneId > NUM_ZONES) return;
  char buf[256];
  _buildZoneJson(zoneId, buf, sizeof(buf));
  char topic[64];
  snprintf(topic, sizeof(topic), MQTT_TOPIC_ZONE_FMT, zoneId, "state");
  _publishAndBroadcast(topic, buf);
}

void Publisher::publishCommand(const CommandContext& ctx, bool stored) {
  JsonDocument doc;
  doc["ts"]     = _timestamp;
  doc["zone"]   = ctx.zone;
  doc["cmd"]    = heatingCmdToStr(ctx.cmd);
  doc["input"]  = heatingCmdToStr(ctx.inputCmd);
  doc["origin"] = originToStr(ctx.origin);
  doc["source"] = sourceToStr(ctx.source);
  doc["prio"]   = (uint8_t)ctx.priority;
  doc["saved"]  = stored;
  char buf[256]; serializeJson(doc, buf, sizeof(buf));
  char topic[64];
  snprintf(topic, sizeof(topic), MQTT_TOPIC_ZONE_FMT, ctx.zone, "cmd");
  _publishAndBroadcast(topic, buf);

  // Publication pour affichage déporté (rétrocompatibilité)
  {
    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf),
             "{\"zone\":%d,\"cmd\":\"%s\"}",
             ctx.zone, heatingCmdToStr(ctx.cmd));
    _mqttPublish("newHeating/cmdStatus", statusBuf, false);
  }

  publishCommandLog(ctx, stored);
}

void Publisher::publishTempoState(const TempoState& state,
                                   TempoColor tomorrow) {
  JsonDocument doc;
  doc["ts"]       = _timestamp;
  doc["enabled"]  = state.active;
  doc["color"]    = tempoColorToStr(state.color);
  doc["period"]   = (state.period == TempoPeriod::HP) ? "HP" : "HC";
  doc["forceHG"]  = state.forceHG;
  doc["tomorrow"] = tempoColorToStr(tomorrow);
  char buf[256]; serializeJson(doc, buf, sizeof(buf));
  _publishAndBroadcast(MQTT_TOPIC_TEMPO, buf);
  // tempoValues publié chaque minute depuis publishLinkyValues avec codes TIC bruts
}

void Publisher::publishLinkyValues(uint8_t iinst, uint8_t isousc,
                                    const char* ptec, const char* demain,
                                    bool overload) {
  JsonDocument doc;
  doc["ts"]       = _timestamp;
  doc["Iinst"]    = iinst;
  doc["Isousc"]   = isousc;
  doc["PTEC"]     = ptec   ? ptec   : "";
  doc["DEMAIN"]   = demain ? demain : "";
  doc["overload"] = overload;
  char buf[256]; serializeJson(doc, buf, sizeof(buf));
  _publishAndBroadcast(MQTT_TOPIC_LINKY, buf);

  // Rétrocompatibilité afficheur déporté — 2 topics distincts
  // newHeating/IinstVal : {"Iinst":1,"overload":false}
  char iinstBuf[64];
  snprintf(iinstBuf, sizeof(iinstBuf),
    "{\"Iinst\":%u,\"overload\":%s}",
    iinst, overload ? "true" : "false");
  _mqttPublish("newHeating/IinstVal", iinstBuf, false);

  // newHeating/linkyValues : {"Isousc":45,"PTEC":"HPJB","DEMAIN":"BLEU"}
  char linkyBuf[128];
  snprintf(linkyBuf, sizeof(linkyBuf),
    "{\"Isousc\":%u,\"PTEC\":\"%s\",\"DEMAIN\":\"%s\"}",
    isousc,
    ptec   ? ptec   : "",
    demain ? demain : "");
  _mqttPublish("newHeating/linkyValues", linkyBuf, false);
}

void Publisher::publishLinkyIndexes(long hcjb, long hpjb,
                                     long hcjw, long hpjw,
                                     long hcjr, long hpjr) {
  JsonDocument doc;
  doc["ts"]      = _timestamp;
  doc["BBRHCJB"] = hcjb; doc["BBRHPJB"] = hpjb;
  doc["BBRHCJW"] = hcjw; doc["BBRHPJW"] = hpjw;
  doc["BBRHCJR"] = hcjr; doc["BBRHPJR"] = hpjr;
  char buf[256]; serializeJson(doc, buf, sizeof(buf));
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/linkyIndexes", MQTT_TOPIC_BASE);
  _mqttPublish(topic, buf, false);

  // Rétrocompatibilité afficheur déporté
  _mqttPublish("newHeating/IdxCompteur", buf, false);
}

// =============================================================================
// LOGS
// =============================================================================

void Publisher::publishLog(const char* level, const char* module,
                            const char* fmt, ...) {
  char msg[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  LOG(LOG_MQTT, "[%s][%s] %s", level, module, msg);

  char buf[384];
  snprintf(buf, sizeof(buf),
    "{\"ts\":%lu,\"fw\":\"%s\",\"sysState\":\"%s\","
    "\"level\":\"%s\",\"module\":\"%s\",\"msg\":\"%s\"}",
    (unsigned long)_timestamp, _fwVersion,
    systemStateToStr(_sysState),
    level, module, msg);

  _mqttPublish(MQTT_TOPIC_LOG, buf, false);
}

void Publisher::publishCommandLog(const CommandContext& ctx, bool stored) {
  JsonDocument doc;
  doc["ts"]       = _timestamp;

  // Commande
  doc["zone"]     = ctx.zone;
  doc["input"]    = heatingCmdToStr(ctx.inputCmd);
  doc["final"]    = heatingCmdToStr(ctx.cmd);

  // Contexte utilisateur (compatibilité ancien format)
  doc["origin"]   = originToStr(ctx.origin);
  doc["source"]   = sourceToStr(ctx.source);

  // Logique système
  doc["stop"]      = ctx.isStop;
  doc["temporary"] = ctx.isTemporary;
  doc["persistent"]= ctx.isPersistent;

  // Exécution
  doc["saved"]    = stored;

  // Contexte énergie / Tempo (compatibilité ancien format)
  if (_tempoMgr) {
    const TempoState& ts = _tempoMgr->getState();
    doc["ptec"]    = tempoColorToStr(ts.color);  // couleur = équivalent ptec
    doc["tempo"]   = ts.active && ts.forceHG;    // isTempoDay
    doc["tempoForceHG"] = ts.forceHG;
  }

  char buf[512]; serializeJson(doc, buf, sizeof(buf));

  // Publication double : nouveau topic + rétrocompatibilité
  _mqttPublish(MQTT_TOPIC_LOG,       buf, false);
  _mqttPublish("newHeating/logs",    buf, false);
}

// =============================================================================
// TEMPÉRATURES
// =============================================================================

void Publisher::updateTemperatures(const char* t1, const char* t2,
                                    const char* t3, const char* t4) {
  const char* temps[2] = {t1, t2};  // 2 zones uniquement
  (void)t3; (void)t4;
  bool changed = false;
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    if (temps[i] && strcmp(_tempZone[i], temps[i]) != 0) {
      strncpy(_tempZone[i], temps[i], 7);
      _tempZone[i][7] = '\0';
      changed = true;
    }
  }
  if (changed) {
    char buf[64];
    snprintf(buf, sizeof(buf),
      "{\"tempZ1\":\"%s\",\"tempZ2\":\"%s\"}",
      _tempZone[0], _tempZone[1]);
    sseBroadcast(buf);
  }
}

// =============================================================================
// CALLBACK MQTT
// =============================================================================

Publisher* Publisher::instance() { return _instance; }

void Publisher::mqttCallbackStatic(char* topic, byte* payload,
                                    unsigned int length) {
  if (_instance) _instance->_onMqttMessage(topic, payload, length);
}

void Publisher::_onMqttMessage(char* topic, byte* payload,
                                unsigned int length) {
  // Copie payload en string null-terminée
  char buf[256] = {0};
  if (length < sizeof(buf)) {
    memcpy(buf, payload, length);
  }

  LOG(LOG_MQTT, "RX topic='%s'", topic);

  if (strstr(topic, "/set"))                              { _handleZoneCmd(topic, buf);    return; }
  if (strcmp(topic, "heatingCmd") == 0)                  { _handleHeatingCmd(buf);         return; }
  if (strcmp(topic, "heatingCtrl_v4/tempo/setCounters") == 0) { _handleSetCounters(buf);  return; }
  if (strncmp(topic, "utcClock", 8) == 0)                { _handleUtcClock(buf);           return; }
  if (strcmp(topic, "ecowittDatas") == 0)                { _handleEcowitt(buf);            return; }
  if (strcmp(topic, "overrideValues") == 0)              { _handleOverrideValues(buf);     return; }
}

void Publisher::_handleZoneCmd(const char* topic, const char* payload) {
  int zoneId = 0;
  sscanf(topic, "heatingCtrl_v4/zone/%d/set", &zoneId);
  if (zoneId < 1 || zoneId > NUM_ZONES) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
  const char* cmd = doc["cmd"] | "";
  if (cmd[0] == '\0') return;

  if (_cmdHandler) {
    _cmdHandler->handle(zoneId, cmd,
                        CommandOrigin::USER,
                        CommandSource::SRC_MQTT);
  }
}

void Publisher::_handleUtcClock(const char* payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return;

  int hh = doc["hours"]|(-1), mm = doc["minutes"]|(-1);
  int ss = doc["seconds"]|0;
  int day = doc["day"]|(-1), month = doc["month"]|(-1);
  if (hh < 0 || mm < 0 || day < 1 || month < 1) return;

  _timestamp = (uint32_t)(hh*3600 + mm*60 + ss);
  LOG(LOG_MQTT, "Clock %02d:%02d:%02d %02d/%02d", hh, mm, ss, day, month);
  linkyReader.onClock(hh, mm, day, month);
  scheduleManager.onClock(hh, mm);
  if (_tempoMgr) _tempoMgr->onClock(hh, mm, (uint8_t)day, (uint8_t)month);
}

void Publisher::_handleEcowitt(const char* payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
  updateTemperatures(doc["tempZ1"]|"", doc["tempZ2"]|"",
                     doc["tempZ3"]|"", doc["tempZ4"]|"");
}

void Publisher::_handleHeatingCmd(const char* payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    LOG(LOG_MQTT, "heatingCmd JSON error");
    return;
  }

  int         zone = doc["zone"] | 0;
  const char* cmd  = doc["cmd"]  | "";

  if (zone < 1 || zone > NUM_ZONES || cmd[0] == '\0') {
    LOG(LOG_MQTT, "heatingCmd invalid zone=%d cmd=%s", zone, cmd);
    return;
  }

  // Anti-doublon : ignore commande identique reçue < 5s après la précédente
  unsigned long now = millis();
  if (zone == (int)_lastHeatCmdZone &&
      strcmp(cmd, _lastHeatCmd) == 0 &&
      now - _lastHeatCmdMs < 5000UL) {
    LOG(LOG_MQTT, "heatingCmd duplicate ignored Z%d %s", zone, cmd);
    return;
  }
  _lastHeatCmdZone = (uint8_t)zone;
  strncpy(_lastHeatCmd, cmd, sizeof(_lastHeatCmd) - 1);
  _lastHeatCmd[sizeof(_lastHeatCmd) - 1] = '\0';
  _lastHeatCmdMs = now;

  LOG(LOG_MQTT, "heatingCmd Z%d cmd=%s", zone, cmd);

  if (_cmdHandler) {
    _cmdHandler->handle(zone, cmd,
                        CommandOrigin::USER,
                        CommandSource::SRC_MQTT);
  }
}

void Publisher::_handleSetCounters(const char* payload) {
  // Mise à jour manuelle des compteurs Tempo
  // Format : {"red":5,"white":12}
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    LOG(LOG_MQTT, "setCounters JSON error");
    return;
  }
  uint8_t red   = doc["red"]   | 255;
  uint8_t white = doc["white"] | 255;

  if (red > 22 || white > 43) {
    LOG(LOG_MQTT, "setCounters invalid values red=%d white=%d", red, white);
    return;
  }

  LOG(LOG_MQTT, "setCounters red=%d white=%d", red, white);
  if (_tempoMgr) _tempoMgr->setCounters(red, white);
}

void Publisher::_handleOverrideValues(const char* payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
  uint8_t iinst  = doc["ovrIinst"]  | 0;
  uint8_t isousc = doc["ovrIsousc"] | 0;
  const char* ptec = doc["ovrPtec"] | "";
  LOG(LOG_MQTT, "Override Iinst=%u Isousc=%u ptec=%s", iinst, isousc, ptec);
  publishLog("WARN","MQTT","Override Iinst=%u Isousc=%u", iinst, isousc);
  // linkyReader.setOverride(...);
}

// =============================================================================
// HELPERS PRIVÉS
// =============================================================================

bool Publisher::_mqttPublish(const char* topic, const char* payload,
                              bool retain) {
  if (!_impl || !_impl->mqttClient.connected()) return false;
  return _impl->mqttClient.publish(topic, payload, retain);
}

void Publisher::_publishAndBroadcast(const char* mqttTopic, const char* json) {
  _mqttPublish(mqttTopic, json, false);
  sseBroadcast(json);
}

void Publisher::_buildZoneJson(uint8_t zoneId, char* buf, size_t bufLen) const {
  if (!_zoneMgr) { snprintf(buf, bufLen, "{}"); return; }
  const ZoneData& zd = _zoneMgr->getZone(zoneId);
  snprintf(buf, bufLen,
    "{\"id\":%d,\"state\":\"%s\",\"cmd\":\"%s\","
    "\"pos\":%d,\"neg\":%d,\"timer\":%d,\"prio\":%d}",
    zoneId,
    zoneStateToStr(zd.state),
    heatingCmdToStr(zd.currentCmd),
    zd.posState, zd.negState, zd.timerActive,
    (uint8_t)zd.activePriority);
}

void Publisher::setFirmwareVersion(const char* ver) {
  strncpy(_fwVersion, ver, sizeof(_fwVersion) - 1);
  _fwVersion[sizeof(_fwVersion) - 1] = '\0';
}
