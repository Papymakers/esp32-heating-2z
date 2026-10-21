#pragma once
// =============================================================================
// Publisher.h — Publication MQTT + SSE + Logs
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations — aucun include de lib externe dans ce header
class ZoneManager;
class TempoManager;
class CommandHandler;
class LinkyReader;

// =============================================================================
// CONFIGURATION SSE
// =============================================================================

constexpr uint8_t  SSE_MAX_CLIENTS = 4;
constexpr uint16_t SSE_PORT        = 81;
static constexpr const char* MQTT_TOPIC_ACK = "newHeating/IdxCompteur/ack";
static constexpr const char* MQTT_TOPIC_REQUEST = "newHeating/IdxCompteur/request";
// =============================================================================
// CLASSE PUBLISHER
// =============================================================================

class Publisher {
public:

  void begin(ZoneManager*    zoneMgr,
             CommandHandler* cmdHandler,
             TempoManager*   tempoMgr,
             LinkyReader*    linky);

  void update();

  // Connexion
  bool isConnected();
  bool isMqttConnected();
  void reconnectMQTT();

  // Supervision
  uint8_t  getMqttFailCount()    const { return _mqttFailCount; }
  uint32_t getMqttConnectCount() const { return _mqttConnectCount; }
  uint32_t getMqttRetryDelay()   const { return _mqttRetryMs; }
  uint32_t getMqttRxCount()      const { return _mqttRxCount; }

  // Publications
  void publishFullState();
  void publishZoneState(uint8_t zoneId);
  void publishCommand(const CommandContext& ctx, bool stored);
  void publishTempoState(const TempoState& state, TempoColor tomorrow);
  void publishLinkyValues(uint8_t iinst, uint8_t isousc,
                          const char* ptec, const char* demain,
                          bool overload);
  void publishLinkyIndexes(long hcjb, long hpjb,
                           long hcjw, long hpjw,
                           long hcjr, long hpjr);

  // Réglage des traces depuis un JSON {"serial":bool,"mqtt":bool,
  // "level":"WARN","mqttLevel":"INFO"} — sauvegardé en NVS.
  // Utilisé par le topic <base>/log/set et par POST /api/config/log.
  bool applyLogConfigJson(const char* json);
  void logConfigToJson(char* buf, size_t len) const;

  // Publication brute sur un topic (supervision)
  bool publishRaw(const char* topic, const char* payload, bool retain = false);

  // Logs
  void publishLog(const char* level, const char* module,
                  const char* fmt, ...) __attribute__((format(printf, 4, 5)));
  void publishCommandLog(const CommandContext& ctx, bool stored);

  // SSE / broadcast
  void sseBroadcast(const char* json);
  void wsBroadcast(const char* json);
  void wsInit() {}

  // Températures
  void updateTemperatures(const char* t1, const char* t2,
                          const char* t3, const char* t4);

  // Système
  void setSystemState(SystemState state) { _sysState = state; }
  void setFirmwareVersion(const char* ver);
  void setTimestamp(uint32_t ts) { _timestamp = ts; }

  // Identifiant client MQTT (préfixe + 3 derniers octets de la MAC)
  const char* getClientId() const { return _clientId; }

  // Getter températures (depuis Ecowitt)
  const char* getTemp(uint8_t zoneIndex) const {
    if (zoneIndex >= NUM_ZONES) return "";
    return _tempZone[zoneIndex];
  }

  // Callback MQTT statique
  static void mqttCallbackStatic(char*        topic,
                                  byte*        payload,
                                  unsigned int length);
  static Publisher* instance();

private:

  // Pattern Pimpl — tous les objets réseau dans Publisher.cpp
  struct Impl;
  Impl* _impl = nullptr;

  ZoneManager*    _zoneMgr    = nullptr;
  CommandHandler* _cmdHandler = nullptr;
  TempoManager*   _tempoMgr   = nullptr;
  LinkyReader*         _linky = nullptr;

  SystemState     _sysState   = SystemState::INIT;
  uint32_t        _timestamp  = 0;
  char            _clientId[32] = {0};
  char            _fwVersion[32]           = {0};
  char            _tempZone[NUM_ZONES][8]  = {};
  bool            _sseClientActive[SSE_MAX_CLIENTS] = {};
  unsigned long   _lastMqttAttempt = 0;
  unsigned long   _mqttRetryMs      = MQTT_RECONNECT_MS;  // backoff courant
  uint8_t         _mqttFailCount    = 0;                  // échecs consécutifs
  uint32_t        _mqttConnectCount = 0;                  // connexions depuis boot
  uint32_t        _mqttRxCount      = 0;                  // messages reçus

  // Anti-doublon heatingCmd
  uint8_t       _lastHeatCmdZone = 0;
  char          _lastHeatCmd[10] = {0};
  unsigned long _lastHeatCmdMs   = 0;

  static Publisher* _instance;

  // Privés
  bool _mqttPublish(const char* topic, const char* payload, bool retain = false);
  void _publishAndBroadcast(const char* mqttTopic, const char* json);
  void _buildZoneJson(uint8_t zoneId, char* buf, size_t bufLen) const;
  void _addHeader(char* buf, size_t bufLen) const;

  void _sseAcceptClients();
  void _sseHandshake(void* clientPtr);
  void _ssePurgeDisconnected();

  void _onMqttMessage(char* topic, byte* payload, unsigned int length);
  void _handleZoneCmd(const char* topic, const char* payload);
  void _handleHeatingCmd(const char* payload);
  void _handleSetCounters(const char* payload);
  void _handleEcowitt(const char* payload);
  void _handleOverrideValues(const char* payload);
  void _handleLogSet(const char* payload);
};

extern Publisher publisher;