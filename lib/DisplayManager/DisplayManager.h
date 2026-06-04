#pragma once
// =============================================================================
// DisplayManager.h — Affichage TM1637 via ErriezTM1637
// ESP32 Heating Controller v4.0-2Z
// Denis Mattera - 2025
//
// Layout TM1637 :
//   Digit 0 (GRID1) : Zone 1 — commande bitmask
//   Digit 1 (GRID2) : Zone 2 — commande bitmask
//   Digit 2 (GRID3) : réservé (0x00)
//   Digit 3 (GRID4) : réservé (0x00)
//   Digit 4 (GRID5) : Status WiFi + Linky (bitmask combiné)
//
// Bitmask commandes :
//   CMD_OFF   = 0x00  — éteint
//   CMD_STOP  = 0x01  — STOP
//   CMD_HG    = 0x02  — Hors Gel
//   CMD_ECO   = 0x04  — ECO
//   CMD_CONF  = 0x08  — CONFORT
//   CMD_CM2   = 0x10  — Confort -2°C
//   CMD_WIFI  = 0x20  — WiFi connecté (digit 4)
//   CMD_LINK  = 0x40  — Linky connecté (digit 4)
//   CMD_RESET = 0xFF  — reset/erreur
//
// Veille : appui switch → réveil, timeout DISPLAY_TIMEOUT_SEC → extinction
// =============================================================================

#include <Arduino.h>
#include <ErriezTM1637.h>
#include "types.h"
#include "config.h"
#include "OverloadManager.h"

// =============================================================================
// BITMASK AFFICHAGE (repris de l'ancien projet)
// =============================================================================

enum displayCommand : uint8_t {
  DISP_OFF   = 0b00000000,
  DISP_STOP  = 0b00000001,
  DISP_HG    = 0b00000010,
  DISP_ECO   = 0b00000100,
  DISP_CONF  = 0b00001000,
  DISP_CM2   = 0b00010000,
  DISP_WIFI  = 0b00100000,
  DISP_LINK  = 0b01000000,
  DISP_RESET = 0b11111111
};

// Conversion HeatingCmd → displayCommand
inline uint8_t heatingCmdToDisp(HeatingCmd cmd) {
  switch (cmd) {
    case HeatingCmd::STOP: return DISP_STOP;
    case HeatingCmd::HG:   return DISP_HG;
    case HeatingCmd::ECO:  return DISP_ECO;
    case HeatingCmd::CONF: return DISP_CONF;
    case HeatingCmd::CM2:  return DISP_CM2;
    default:               return DISP_OFF;
  }
}

// =============================================================================
// CLASSE DISPLAYMANAGER
// =============================================================================

class DisplayManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  bool begin();

  // ---------------------------------------------------------------------------
  // Loop — à appeler dans loop()
  // ---------------------------------------------------------------------------

  void update();

  // ---------------------------------------------------------------------------
  // Réveil écran
  // ---------------------------------------------------------------------------

  void wake();

  // ---------------------------------------------------------------------------
  // Setters
  // ---------------------------------------------------------------------------

  void setActiveZoneCount(uint8_t count) { _activeZones = count; }
  void setZoneCmd(uint8_t zoneId, HeatingCmd cmd, ZoneState state);
  void setSelectedZone(uint8_t zoneId);
  void setWifiStatus(bool connected, const char* ip = nullptr);
  void setMqttStatus(bool connected)  { (void)connected; }
  void setLinkyStatus(bool connected);
  void setTempoState(TempoColor color, TempoPeriod period, bool forceHG);
  void setOverloadState(bool active, OverloadState state);
  void setScheduleInfo(bool enabled, uint8_t slot, const char* name)
    { (void)enabled; (void)slot; (void)name; }

  // ---------------------------------------------------------------------------
  // Accesseurs
  // ---------------------------------------------------------------------------

  bool isOn()      const { return _screenOn; }
  bool isPresent() const { return _present;  }

  // ---------------------------------------------------------------------------
  // Écrans spéciaux
  // ---------------------------------------------------------------------------

  void forceRefresh();
  void showBootScreen(const char* version);
  void showFaultScreen(const char* reason);

private:

  TM1637      _tm1637{TM1637_CLK_PIN, TM1637_DIO_PIN};
  bool        _present    = false;
  bool        _screenOn   = false;

  // Cache état
  uint8_t     _activeZones = NUM_ZONES;
  uint8_t     _selectedZone = 1;
  HeatingCmd  _zoneCmd[NUM_ZONES]   = {};
  ZoneState   _zoneState[NUM_ZONES] = {};

  bool        _wifiConnected  = false;
  bool        _linkyConnected = false;
  bool        _tempoForceHG   = false;
  bool        _overloadActive = false;

  unsigned long _lastActivity = 0;
  bool          _dirty        = true;
  unsigned long _lastStatusMs = 0;
  bool          _lastWifiState = false;

  // ---------------------------------------------------------------------------
  // Rendu interne
  // ---------------------------------------------------------------------------

  void _render();
  void _writeZone(uint8_t zoneId, HeatingCmd cmd);
  void _writeStatus();
  void _clearAll();
};

extern DisplayManager displayManager;
