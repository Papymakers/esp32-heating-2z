#pragma once
// =============================================================================
// OverloadManager.h — Gestion du délestage par surcharge courant
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Responsabilités :
//   - Détecter une surcharge (Iinst >= Isousc) sur durée seuil
//   - Délester les zones selon une stratégie configurable
//   - Gérer le fallback et la restauration automatique
//   - Publier les événements de délestage
//
// Stratégie de délestage :
//   1. Surcharge détectée → on attend OVERLOAD_THRESHOLD_MS
//   2. Toujours en surcharge → délestage zone(s) : ECO ou HG selon config
//   3. Fin surcharge → on attend RESTORE_DELAY_MS puis restauration
//
// Zones délestées selon priorité configurable :
//   - Par défaut : toutes les zones passent en ECO
//   - Configurable : zone 1 en HG, zones 2-4 en ECO (ex: pièce principale protégée)
//
// FSM OverloadManager :
//   IDLE → DETECTING → OVERLOADED → RESTORING → IDLE
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations
class ZoneManager;
class Publisher;
class CommandHandler;

// =============================================================================
// ÉTATS FSM OVERLOAD
// =============================================================================

enum class OverloadState : uint8_t {
  IDLE,        // Pas de surcharge
  DETECTING,   // Surcharge détectée, en attente confirmation (seuil temporel)
  OVERLOADED,  // Surcharge confirmée, zones délestées
  RESTORING    // Fin surcharge, délai avant restauration
};

// =============================================================================
// CONFIGURATION DÉLESTAGE
// =============================================================================

struct OverloadConfig {
  unsigned long thresholdMs;    // Durée avant délestage (défaut: 10s)
  unsigned long restoreDelayMs; // Délai avant restauration (défaut: 30s)
  unsigned long maxOverloadMs;  // Durée max en délestage avant restauration forcée (défaut: 5min)
  HeatingCmd    fallbackCmd;    // Commande appliquée lors du délestage (défaut: ECO)
  bool          enabled;        // Délestage actif
};

// =============================================================================
// STATISTIQUES SURCHARGE
// =============================================================================

struct OverloadStats {
  uint32_t eventCount;          // Nombre de surcharges depuis boot
  uint32_t totalOverloadMs;     // Durée cumulée en surcharge
  uint8_t  peakIinst;           // Pic Iinst mesuré
  uint32_t lastEventTimestamp;  // Timestamp dernière surcharge
};

// =============================================================================
// CLASSE OVERLOADMANAGER
// =============================================================================

class OverloadManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  void begin(ZoneManager*    zoneMgr,
             CommandHandler* cmdHandler,
             Publisher*      publisher);

  // ---------------------------------------------------------------------------
  // Callback depuis LinkyReader — appelé à chaque mesure Iinst
  // ---------------------------------------------------------------------------

  void onIinstUpdate(uint8_t iinst, uint8_t isousc, bool overload);

  // ---------------------------------------------------------------------------
  // Loop FSM — à appeler dans loop()
  // ---------------------------------------------------------------------------

  void update();

  // ---------------------------------------------------------------------------
  // Accesseurs état
  // ---------------------------------------------------------------------------

  OverloadState    getState()       const { return _state; }
  bool             isOverloaded()   const { return _state == OverloadState::OVERLOADED; }
  bool             isDetecting()    const { return _state == OverloadState::DETECTING; }
  const OverloadStats& getStats()   const { return _stats; }
  uint8_t          getCurrentIinst()  const { return _currentIinst; }
  uint8_t          getCurrentIsousc() const { return _currentIsousc; }

  // ---------------------------------------------------------------------------
  // Configuration (depuis web UI ou MQTT)
  // ---------------------------------------------------------------------------

  void setEnabled(bool enabled);
  void setThresholdMs(unsigned long ms);
  void setRestoreDelayMs(unsigned long ms);
  void setFallbackCmd(HeatingCmd cmd);

  const OverloadConfig& getConfig() const { return _config; }

  // ---------------------------------------------------------------------------
  // Forcer fin de délestage (depuis UI)
  // ---------------------------------------------------------------------------

  void forceRestore();

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dump() const;

  static const char* stateToStr(OverloadState s);

private:

  // ---------------------------------------------------------------------------
  // FSM interne
  // ---------------------------------------------------------------------------

  void _handleIdle();
  void _handleDetecting();
  void _handleOverloaded();
  void _handleRestoring();

  // ---------------------------------------------------------------------------
  // Actions
  // ---------------------------------------------------------------------------

  void _startDelestage();
  void _endDelestage();
  void _publishEvent(const char* event);

  // ---------------------------------------------------------------------------
  // Membres privés
  // ---------------------------------------------------------------------------

  OverloadState  _state        = OverloadState::IDLE;
  OverloadConfig _config       {};
  OverloadStats  _stats        {};

  uint8_t        _currentIinst  = 0;
  uint8_t        _currentIsousc = 0;
  bool           _overloadFlag  = false;  // Dernière valeur reçue

  unsigned long  _detectStart   = 0;  // Début de détection (DETECTING)
  unsigned long  _overloadStart = 0;  // Début de surcharge confirmée
  unsigned long  _restoreStart  = 0;  // Début de restauration

  // Dépendances
  ZoneManager*    _zoneMgr    = nullptr;
  CommandHandler* _cmdHandler = nullptr;
  Publisher*      _publisher  = nullptr;
};

// Instance globale
extern OverloadManager overloadManager;
