#pragma once
// =============================================================================
// DisplayManager.h — Afficheur détecté au démarrage : OLED SH1106 ou TM1637
// ESP32 Heating Controller v5.1-2Z
// Denis Mattera - 2025
//
// Détection (begin) :
//   1. OLED SH1106 1,3" 128×64 sur le bus I2C de l'EEPROM (SDA 18 / SCL 19),
//      adresse 0x3C puis 0x3D
//   2. sinon TM1637 (CLK 23 / DIO 22) : acquittement du 9e coup d'horloge
//   3. sinon fonctionnement sans afficheur (toutes les méthodes sont inertes)
//   La détection n'a lieu qu'au démarrage : brancher un afficheur à chaud
//   demande un redémarrage.
//
// OLED (U8g2, tampon d'une page de 128×8, rendu page par page) :
//   L1 : WiFi / MQTT + heure NTP (--:-- tant qu'elle est inconnue)
//   L2 : Z1 commande réellement appliquée + repères
//   L3 : Z2 commande réellement appliquée + repères
//          T = HG forcé par Tempo, D = délestage, ~ = Confort -2 (CM2)
//   L4 : couleur Tempo + période + Iinst ;
//        en délestage : « DELEST xxA » en vidéo inverse clignotante
//   Veille après OLED_SLEEP_SEC ; réveil par un switch (le premier appui
//   ne fait que réveiller l'écran) ou au début d'un délestage.
//
// TM1637 (carte LED, inchangé depuis la v4) :
//   Digit 0/1 : commande Z1/Z2 (une LED par commande)
//   Digit 4   : WiFi + Linky
//   Veille : TM1637_SLEEP_SEC (0 = jamais)
// =============================================================================

#include <Arduino.h>
#include <ErriezTM1637.h>
#include "types.h"
#include "config.h"
#include "OverloadManager.h"

// =============================================================================
// BITMASK AFFICHAGE TM1637 (repris de l'ancien projet)
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

enum class DisplayType : uint8_t { NONE, OLED, TM1637 };

inline const char* displayTypeToStr(DisplayType t) {
  switch (t) {
    case DisplayType::OLED:   return "OLED";
    case DisplayType::TM1637: return "TM1637";
    default:                  return "NONE";
  }
}

// =============================================================================
// CLASSE DISPLAYMANAGER
// =============================================================================

class DisplayManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation — détection du type d'afficheur (Wire déjà initialisé)
  // ---------------------------------------------------------------------------

  bool begin();

  // ---------------------------------------------------------------------------
  // Loop — à appeler dans loop()
  // ---------------------------------------------------------------------------

  void update();

  // ---------------------------------------------------------------------------
  // Veille / réveil
  // ---------------------------------------------------------------------------

  void wake();                       // relance aussi le délai de veille
  bool isAsleep() const { return _present && !_screenOn; }

  // ---------------------------------------------------------------------------
  // Setters
  // ---------------------------------------------------------------------------

  void setActiveZoneCount(uint8_t count) { _activeZones = count; }
  void setZoneCmd(uint8_t zoneId, HeatingCmd cmd, ZoneState state,
                  CommandPriority prio = CommandPriority::PRIO_DEFAULT);
  void setSelectedZone(uint8_t zoneId);
  void setWifiStatus(bool connected, const char* ip = nullptr);
  void setMqttStatus(bool connected);
  void setLinkyStatus(bool connected);
  void setLinkyData(uint8_t iinst, bool fresh);
  void setClock(bool valid, uint8_t hh, uint8_t mm);
  void setTempoState(TempoColor color, TempoPeriod period, bool forceHG);
  void setOverloadState(bool active, OverloadState state);
  void setScheduleInfo(bool enabled, uint8_t slot, const char* name)
    { (void)enabled; (void)slot; (void)name; }

  // ---------------------------------------------------------------------------
  // Accesseurs
  // ---------------------------------------------------------------------------

  bool        isOn()      const { return _screenOn; }
  bool        isPresent() const { return _present;  }
  DisplayType getType()   const { return _type; }
  const char* getTypeStr() const { return displayTypeToStr(_type); }
  uint8_t     getOledAddr() const { return _oledAddr; }
  uint32_t    getRenderCount() const { return _renders; }

  // ---------------------------------------------------------------------------
  // Écrans spéciaux
  // ---------------------------------------------------------------------------

  void forceRefresh();
  void showBootScreen(const char* version);
  void showFaultScreen(const char* reason);

private:

  DisplayType _type       = DisplayType::NONE;
  uint8_t     _oledAddr   = 0;
  TM1637      _tm1637{TM1637_CLK_PIN, TM1637_DIO_PIN};
  bool        _present    = false;
  bool        _screenOn   = false;
  bool        _faultShown = false;

  // Cache état
  uint8_t         _activeZones = NUM_ZONES;
  uint8_t         _selectedZone = 1;
  HeatingCmd      _zoneCmd[NUM_ZONES]   = {};
  ZoneState       _zoneState[NUM_ZONES] = {};
  CommandPriority _zonePrio[NUM_ZONES]  = {};

  bool          _wifiConnected  = false;
  bool          _mqttConnected  = false;
  bool          _linkyConnected = false;
  bool          _linkyFresh     = false;
  uint8_t       _iinst          = 0;
  bool          _clockValid     = false;
  uint8_t       _hh = 0, _mm = 0;
  TempoColor    _tempoColor     = TempoColor::UNKNOWN;
  TempoPeriod   _tempoPeriod    = TempoPeriod::UNKNOWN;
  bool          _tempoForceHG   = false;
  bool          _overloadActive = false;
  bool          _blinkOn        = true;

  unsigned long _lastActivity    = 0;
  unsigned long _lastBlink       = 0;
  unsigned long _lastRender      = 0;
  bool          _dirty           = true;
  bool          _lastLinkyState  = false;
  bool          _lastWifiState   = false;
  uint32_t      _renders         = 0;

  // ---------------------------------------------------------------------------
  // Détection
  // ---------------------------------------------------------------------------

  static bool _probeI2c(uint8_t addr);
  static bool _probeTm1637(uint8_t clk, uint8_t dio);

  // ---------------------------------------------------------------------------
  // Rendu interne
  // ---------------------------------------------------------------------------

  uint16_t _sleepSec() const;
  void _sleep();
  void _render();

  // OLED
  void _oledRender();
  void _oledZoneLine(uint8_t zoneId, int baseline);
  void _oledMessage(const char* l1, const char* l2);

  // TM1637
  void _tmRender();
  void _tmWriteZone(uint8_t zoneId, HeatingCmd cmd);
  void _tmWriteStatus();
  void _tmClearAll();
};

extern DisplayManager displayManager;
