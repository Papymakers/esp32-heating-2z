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

// =============================================================================
// CONFIGURATION SSE
// =============================================================================

constexpr uint8_t  SSE_MAX_CLIENTS = 4;
constexpr uint16_t SSE_PORT        = 81;

// =============================================================================
// CLASSE PUBLISHER
// =============================================================================

class Publisher {
public:

  void begin(ZoneManager*    zoneMgr,
             CommandHandler* cmdHandler,
             TempoManager*   tempoMgr);

  void update();

  // Connexion
  bool isConnected();
  bool isMqttConnected();
  void reconnectMQTT();

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

  SystemState     _sysState   = SystemState::INIT;
  uint32_t        _timestamp  = 0;
  char            _fwVersion[20]           = {0};
  char            _tempZone[NUM_ZONES][8]  = {};
  bool            _sseClientActive[SSE_MAX_CLIENTS] = {};
  unsigned long   _lastMqttAttempt = 0;

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
  void _handleUtcClock(const char* payload);
  void _handleEcowitt(const char* payload);
  void _handleOverrideValues(const char* payload);
};

extern Publisher publisher;
