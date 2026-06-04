#pragma once
// =============================================================================
// WebUI.h — Serveur HTTP + Interface web
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Responsabilités :
//   - Servir la page HTML principale (dashboard + calendrier)
//   - Gérer les routes API REST (GET/POST)
//   - Déléguer les actions aux managers concernés
//
// Routes HTTP :
//   GET  /                  → Page principale (HTML embarqué)
//   GET  /api/state         → État complet JSON
//   GET  /api/profiles      → Liste des profils JSON
//   GET  /api/schedule      → Associations zone/profil JSON
//   POST /api/profile       → Crée/modifie un profil
//   POST /api/schedule      → Modifie les associations zone/profil
//   POST /api/zone          → Commande manuelle sur une zone
//   POST /api/config/tempo  → Config Tempo (enabled, règles)
//   POST /api/config/ovld   → Config Overload (seuils)
//   POST /api/config/zones  → Nb de zones actives
//   POST /api/eeprom/erase  → Effacement EEPROM (avec confirmation)
//   GET  /api/debug         → Dump état tous modules
//
// WebSocket (port 81) géré par Publisher
// =============================================================================

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "types.h"
#include "config.h"

// Forward declarations
class ZoneManager;
class CommandHandler;
class TempoManager;
class ScheduleManager;
class OverloadManager;
class StorageManager;
class DisplayManager;
class Publisher;

// =============================================================================
// CLASSE WEBUI
// =============================================================================

class WebUI {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  void begin(ZoneManager*     zoneMgr,
             CommandHandler*  cmdHandler,
             TempoManager*    tempoMgr,
             ScheduleManager* scheduleMgr,
             OverloadManager* overloadMgr,
             StorageManager*  storageMgr,
             DisplayManager*  displayMgr,
             Publisher*       publisher);

  // ---------------------------------------------------------------------------
  // Loop — à appeler dans loop()
  // ---------------------------------------------------------------------------

  void update();

  // ---------------------------------------------------------------------------
  // Accesseurs
  // ---------------------------------------------------------------------------

  uint8_t getActiveZoneCount() const { return _activeZones; }

private:

  // ---------------------------------------------------------------------------
  // Serveur HTTP
  // ---------------------------------------------------------------------------

  WebServer _server{80};

  // ---------------------------------------------------------------------------
  // Dépendances
  // ---------------------------------------------------------------------------

  ZoneManager*     _zoneMgr     = nullptr;
  CommandHandler*  _cmdHandler  = nullptr;
  TempoManager*    _tempoMgr    = nullptr;
  ScheduleManager* _scheduleMgr = nullptr;
  OverloadManager* _overloadMgr = nullptr;
  StorageManager*  _storageMgr  = nullptr;
  DisplayManager*  _displayMgr  = nullptr;
  Publisher*       _publisher   = nullptr;

  uint8_t _activeZones = NUM_ZONES;

  // ---------------------------------------------------------------------------
  // Enregistrement des routes
  // ---------------------------------------------------------------------------

  void _registerRoutes();

  // ---------------------------------------------------------------------------
  // Handlers HTTP
  // ---------------------------------------------------------------------------

  void _handleRoot();
  void _handleApiState();
  void _handleApiProfiles();
  void _handleApiSchedule();
  void _handleApiPostProfile();
  void _handleApiPostSchedule();
  void _handleApiPostZone();
  void _handleApiConfigTempo();
  void _handleApiConfigOverload();
  void _handleApiConfigZones();
  void _handleApiEepromErase();
  void _handleApiDebug();
  void _handleNotFound();

  // ---------------------------------------------------------------------------
  // Helpers réponses
  // ---------------------------------------------------------------------------

  void _sendJson(const char* json, int code = 200);
  void _sendOk(const char* msg = "ok");
  void _sendError(const char* msg, int code = 400);

  // Vérifie que le body JSON est parseable, retourne false + envoie erreur sinon
  bool _parseBody(JsonDocument& doc);

  // ---------------------------------------------------------------------------
  // HTML embarqué
  // ---------------------------------------------------------------------------

  static const char HTML_PAGE[] PROGMEM;
};

// Instance globale
extern WebUI webUI;
