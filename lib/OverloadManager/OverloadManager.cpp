// =============================================================================
// OverloadManager.cpp — Gestion du délestage par surcharge courant
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// FSM :
//   IDLE
//     └─► DETECTING  (onIinstUpdate: overload=true)
//           ├─► IDLE       (surcharge disparaît avant seuil)
//           └─► OVERLOADED (seuil temporel atteint)
//                 ├─► OVERLOADED (surcharge persiste)
//                 └─► RESTORING  (surcharge disparaît OU timeout max)
//                       └─► IDLE (délai restauration écoulé)
// =============================================================================

#include "OverloadManager.h"
#include "ZoneManager.h"
#include "CommandHandler.h"
#include "Publisher.h"

// Instance globale
OverloadManager overloadManager;

// =============================================================================
// INITIALISATION
// =============================================================================

void OverloadManager::begin(ZoneManager*    zoneMgr,
                             CommandHandler* cmdHandler,
                             Publisher*      publisher) {
  _zoneMgr    = zoneMgr;
  _cmdHandler = cmdHandler;
  _publisher  = publisher;

  // Config par défaut
  _config.thresholdMs    = OVERLOAD_THRESHOLD_MS;   // 10s
  _config.restoreDelayMs = 30000UL;                 // 30s
  _config.maxOverloadMs  = FALLBACK_DURATION_MS;    // 5min
  _config.fallbackCmd    = HeatingCmd::ECO;
  _config.enabled        = true;

  // Stats
  _stats = {};

  _state = OverloadState::IDLE;

  LOG(LOG_OVLD, "OverloadManager ready (threshold=%lums restore=%lums)",
      _config.thresholdMs, _config.restoreDelayMs);
}

// =============================================================================
// CALLBACK IINST (depuis LinkyReader)
// =============================================================================

void OverloadManager::onIinstUpdate(uint8_t iinst,
                                     uint8_t isousc,
                                     bool    overload) {
  _currentIinst  = iinst;
  _currentIsousc = isousc;
  _overloadFlag  = overload;

  // Mise à jour pic
  if (iinst > _stats.peakIinst) {
    _stats.peakIinst = iinst;
  }
}

// =============================================================================
// LOOP FSM
// =============================================================================

void OverloadManager::update() {
  if (!_config.enabled) return;

  switch (_state) {
    case OverloadState::IDLE:       _handleIdle();       break;
    case OverloadState::DETECTING:  _handleDetecting();  break;
    case OverloadState::OVERLOADED: _handleOverloaded(); break;
    case OverloadState::RESTORING:  _handleRestoring();  break;
  }
}

// =============================================================================
// HANDLERS FSM
// =============================================================================

void OverloadManager::_handleIdle() {
  if (!_overloadFlag) return;

  // Début de détection
  _state       = OverloadState::DETECTING;
  _detectStart = millis();

  LOG(LOG_OVLD, "DETECTING — Iinst=%u >= Isousc=%u",
      _currentIinst, _currentIsousc);

  _publishEvent("detecting");
}

void OverloadManager::_handleDetecting() {
  // Surcharge disparue avant le seuil → retour IDLE
  if (!_overloadFlag) {
    LOG(LOG_OVLD, "Overload cleared before threshold — back to IDLE");
    _state = OverloadState::IDLE;
    _publishEvent("cleared_early");
    return;
  }

  // Seuil temporel atteint → délestage confirmé
  if (millis() - _detectStart >= _config.thresholdMs) {
    LOG(LOG_OVLD, "Threshold reached (%lums) — starting delestage",
        _config.thresholdMs);
    _startDelestage();
  }
}

void OverloadManager::_handleOverloaded() {
  // Timeout max atteint → restauration forcée (même si toujours en surcharge)
  if (millis() - _overloadStart >= _config.maxOverloadMs) {
    LOG(LOG_OVLD, "Max overload duration reached (%lums) — forced restore",
        _config.maxOverloadMs);

    if (_publisher) {
      _publisher->publishLog("WARN", "OVLD",
        "Max overload duration reached — forced restore "
        "Iinst=%u Isousc=%u",
        _currentIinst, _currentIsousc);
    }

    _endDelestage();
    return;
  }

  // Surcharge disparue → début restauration
  if (!_overloadFlag) {
    LOG(LOG_OVLD, "Overload cleared — entering RESTORING");
    _endDelestage();
  }
}

void OverloadManager::_handleRestoring() {
  // Si surcharge revient pendant restauration → retour DETECTING
  if (_overloadFlag) {
    LOG(LOG_OVLD, "Overload re-detected during RESTORING — back to DETECTING");
    _state       = OverloadState::DETECTING;
    _detectStart = millis();
    _publishEvent("re_detecting");
    return;
  }

  // Délai restauration écoulé → retour IDLE + restauration zones
  if (millis() - _restoreStart >= _config.restoreDelayMs) {
    LOG(LOG_OVLD, "Restore delay elapsed — zones restoring → IDLE");

    // Fin override dans ZoneManager (déclenche RESTORE sur chaque zone)
    if (_zoneMgr) {
      for (uint8_t z = 1; z <= NUM_ZONES; z++) {
        if (_zoneMgr->isOverrideActive(z)) {
          _zoneMgr->endOverride(z);
        }
      }
    }

    // Durée totale en surcharge
    unsigned long duration = millis() - _overloadStart;
    _stats.totalOverloadMs += duration;

    LOG(LOG_OVLD, "Overload event ended — duration=%lums total=%lums",
        duration, _stats.totalOverloadMs);

    if (_publisher) {
      _publisher->publishLog("INFO", "OVLD",
        "Overload ended — duration=%lums events=%u peak=%uA",
        duration, _stats.eventCount, _stats.peakIinst);
    }

    _state = OverloadState::IDLE;
    _publishEvent("restored");
  }
}

// =============================================================================
// ACTIONS
// =============================================================================

void OverloadManager::_startDelestage() {
  _state         = OverloadState::OVERLOADED;
  _overloadStart = millis();

  // Statistiques
  _stats.eventCount++;
  _stats.lastEventTimestamp = _overloadStart;

  LOG(LOG_OVLD, "OVERLOADED — applying %s to all zones (event #%u)",
      heatingCmdToStr(_config.fallbackCmd), _stats.eventCount);

  // Délestage via ZoneManager (startOverride sur chaque zone)
  if (_zoneMgr) {
    for (uint8_t z = 1; z <= NUM_ZONES; z++) {
      _zoneMgr->startOverride(z, _config.fallbackCmd);
    }
  }

  if (_publisher) {
    _publisher->publishLog("WARN", "OVLD",
      "Delestage started — Iinst=%u Isousc=%u cmd=%s event=#%u",
      _currentIinst, _currentIsousc,
      heatingCmdToStr(_config.fallbackCmd),
      _stats.eventCount);
  }

  _publishEvent("overloaded");
}

void OverloadManager::_endDelestage() {
  _state        = OverloadState::RESTORING;
  _restoreStart = millis();

  LOG(LOG_OVLD, "RESTORING — waiting %lums before zone restore",
      _config.restoreDelayMs);

  _publishEvent("restoring");
}

void OverloadManager::_publishEvent(const char* event) {
  if (!_publisher) return;

  // Publication état surcharge via MQTT + WebSocket
  // (format léger pour réactivité UI)
  char topic[64];
  snprintf(topic, sizeof(topic), "%s/overload", MQTT_TOPIC_BASE);

  // On passe par publishLog pour avoir le format JSON enrichi
  // Un topic dédié sera ajouté dans Publisher si besoin
  (void)topic;  // Utilisé quand Publisher aura publishOverloadState()

  // Pour l'instant : log enrichi suffisant
  _publisher->publishLog("INFO", "OVLD",
    "state=%s event=%s Iinst=%u Isousc=%u events=%u",
    stateToStr(_state), event,
    _currentIinst, _currentIsousc,
    _stats.eventCount);
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void OverloadManager::setEnabled(bool enabled) {
  _config.enabled = enabled;
  LOG(LOG_OVLD, "Delestage %s", enabled ? "ENABLED" : "DISABLED");

  if (!enabled && _state != OverloadState::IDLE) {
    // Désactivation en cours de surcharge → restauration immédiate
    LOG(LOG_OVLD, "Disabled during overload — forcing restore");
    forceRestore();
  }
}

void OverloadManager::setThresholdMs(unsigned long ms) {
  _config.thresholdMs = ms;
  LOG(LOG_OVLD, "Threshold set to %lums", ms);
}

void OverloadManager::setRestoreDelayMs(unsigned long ms) {
  _config.restoreDelayMs = ms;
  LOG(LOG_OVLD, "Restore delay set to %lums", ms);
}

void OverloadManager::setFallbackCmd(HeatingCmd cmd) {
  _config.fallbackCmd = cmd;
  LOG(LOG_OVLD, "Fallback cmd set to %s", heatingCmdToStr(cmd));
}

// =============================================================================
// FORCER RESTAURATION
// =============================================================================

void OverloadManager::forceRestore() {
  if (_state == OverloadState::IDLE) return;

  LOG(LOG_OVLD, "Force restore from state=%s", stateToStr(_state));

  if (_publisher) {
    _publisher->publishLog("WARN", "OVLD",
      "Force restore triggered from state=%s", stateToStr(_state));
  }

  // Restauration immédiate des zones
  if (_zoneMgr) {
    for (uint8_t z = 1; z <= NUM_ZONES; z++) {
      if (_zoneMgr->isOverrideActive(z)) {
        _zoneMgr->endOverride(z);
      }
    }
  }

  _state = OverloadState::IDLE;
  _publishEvent("force_restored");
}

// =============================================================================
// DEBUG
// =============================================================================

const char* OverloadManager::stateToStr(OverloadState s) {
  switch (s) {
    case OverloadState::IDLE:       return "IDLE";
    case OverloadState::DETECTING:  return "DETECTING";
    case OverloadState::OVERLOADED: return "OVERLOADED";
    case OverloadState::RESTORING:  return "RESTORING";
    default:                        return "?";
  }
}

void OverloadManager::dump() const {
  Serial.printf(
    "%s === OverloadManager ===\n"
    "%s   state        : %s\n"
    "%s   enabled      : %d\n"
    "%s   Iinst        : %u A\n"
    "%s   Isousc       : %u A\n"
    "%s   overloadFlag : %d\n"
    "%s   threshold    : %lu ms\n"
    "%s   restoreDelay : %lu ms\n"
    "%s   maxOverload  : %lu ms\n"
    "%s   fallbackCmd  : %s\n"
    "%s   events       : %u\n"
    "%s   peakIinst    : %u A\n"
    "%s   totalMs      : %lu ms\n"
    "%s =======================\n",
    LOG_OVLD,
    LOG_OVLD, stateToStr(_state),
    LOG_OVLD, _config.enabled,
    LOG_OVLD, _currentIinst,
    LOG_OVLD, _currentIsousc,
    LOG_OVLD, _overloadFlag,
    LOG_OVLD, _config.thresholdMs,
    LOG_OVLD, _config.restoreDelayMs,
    LOG_OVLD, _config.maxOverloadMs,
    LOG_OVLD, heatingCmdToStr(_config.fallbackCmd),
    LOG_OVLD, _stats.eventCount,
    LOG_OVLD, _stats.peakIinst,
    LOG_OVLD, _stats.totalOverloadMs,
    LOG_OVLD
  );
}
