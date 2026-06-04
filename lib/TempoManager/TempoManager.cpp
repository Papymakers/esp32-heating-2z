// =============================================================================
// TempoManager.cpp — Gestion option TEMPO EDF
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include "TempoManager.h"
#include "CommandHandler.h"
#include "Publisher.h"
#include "ZoneManager.h"
#include "StorageManager.h"

// Instance globale
TempoManager tempoManager;

// =============================================================================
// INITIALISATION
// =============================================================================

void TempoManager::begin(CommandHandler* cmdHandler, Publisher* publisher) {
  _cmdHandler = cmdHandler;
  _publisher  = publisher;

  // Config par défaut
  _config.enabled        = false;  // Désactivé par défaut, activable via UI
  _config.forceHG_WhiteHP = true;
  _config.forceHG_RedHP   = true;
  _config.forceHG_RedHC   = false;

  // État initial inconnu
  _state.color    = TempoColor::UNKNOWN;
  _state.period   = TempoPeriod::UNKNOWN;
  _state.active   = false;
  _state.forceHG  = false;
  _tomorrowColor  = TempoColor::UNKNOWN;

  LOG(LOG_TEMPO, "TempoManager ready (enabled=%d)", _config.enabled);

  // Charge les compteurs depuis EEPROM
  storageManager.loadTempoCounters(_countRed, _countWhite);
  LOG(LOG_TEMPO, "Tempo counters: red=%d/22 white=%d/43", _countRed, _countWhite);
}

// =============================================================================
// MISE À JOUR DEPUIS LINKY
// =============================================================================

void TempoManager::onPtecChange(const char* ptec) {
  if (!ptec || ptec[0] == '\0') return;

  // Pas de changement → on ignore
  if (strncmp(_lastPtec, ptec, sizeof(_lastPtec) - 1) == 0) return;

  strncpy(_lastPtec, ptec, sizeof(_lastPtec) - 1);
  _lastPtec[sizeof(_lastPtec) - 1] = '\0';

  TempoColor  newColor;
  TempoPeriod newPeriod;

  if (!_parsePtec(ptec, newColor, newPeriod)) {
    LOG(LOG_TEMPO, "Invalid PTEC: '%s'", ptec);
    if (_publisher) {
      _publisher->publishLog("WARN", "TEMPO",
        "Invalid PTEC received: %s", ptec);
    }
    return;
  }

  bool colorChanged  = (newColor  != _state.color);
  bool periodChanged = (newPeriod != _state.period);

  _state.color  = newColor;
  _state.period = newPeriod;
  _state.active = _config.enabled;

  // Recalcule forceHG
  bool newForceHG = _computeForceHG(newColor, newPeriod);
  bool forceHGChanged = (newForceHG != _state.forceHG);
  _state.forceHG = newForceHG;

  LOG(LOG_TEMPO, "PTEC='%s' color=%s period=%s forceHG=%d",
      ptec,
      tempoColorToStr(_state.color),
      (_state.period == TempoPeriod::HP) ? "HP" : "HC",
      _state.forceHG);

  // Applique les règles seulement si Tempo actif ET changement détecté
  if (_config.enabled && (forceHGChanged || colorChanged || periodChanged)) {
    _applyTempoRules();
    _publishState();
  } else if (colorChanged || periodChanged) {
    // Publie quand même l'état pour mettre à jour le WebUI
    _publishState();
  }
}

void TempoManager::onDemainChange(const char* demain) {
  if (!demain || demain[0] == '\0') return;

  if (strncmp(_lastDemain, demain, sizeof(_lastDemain) - 1) == 0) return;

  strncpy(_lastDemain, demain, sizeof(_lastDemain) - 1);
  _lastDemain[sizeof(_lastDemain) - 1] = '\0';

  TempoColor newTomorrow = _parseDemain(demain);

  if (newTomorrow != _tomorrowColor) {
    _tomorrowColor = newTomorrow;
    LOG(LOG_TEMPO, "DEMAIN='%s' → %s",
        demain, tempoColorToStr(_tomorrowColor));
    _publishState();  // Met à jour l'UI avec la couleur de demain
  }
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void TempoManager::setEnabled(bool enabled) {
  if (_config.enabled == enabled) return;

  _config.enabled = enabled;
  _state.active   = enabled;

  LOG(LOG_TEMPO, "Tempo %s", enabled ? "ENABLED" : "DISABLED");

  if (!enabled) {
    // Désactivation Tempo → restaure les zones depuis leur commande sauvegardée
    _state.forceHG = false;
    LOG(LOG_TEMPO, "Tempo disabled — restoring zones");

    if (_cmdHandler) {
      // Remet la priorité des zones à DEFAULT pour permettre la recovery
      for (uint8_t z = 1; z <= NUM_ZONES; z++) {
        zoneManager.getZoneMutable(z).activePriority = CommandPriority::PRIO_DEFAULT;
        zoneManager.getZoneMutable(z).state          = ZoneState::NORMAL;
      }
      // Restaure depuis EEPROM
      _cmdHandler->handleRecovery();
    }
  } else {
    // Réactivation : on recalcule et applique les règles immédiatement
    _state.forceHG = _computeForceHG(_state.color, _state.period);
    _applyTempoRules();
  }

  _publishState();
  saveConfig();
}

void TempoManager::setForceHG_WhiteHP(bool val) {
  _config.forceHG_WhiteHP = val;
  _state.forceHG = _computeForceHG(_state.color, _state.period);
  _applyTempoRules();
  saveConfig();
}

void TempoManager::setForceHG_RedHP(bool val) {
  _config.forceHG_RedHP = val;
  _state.forceHG = _computeForceHG(_state.color, _state.period);
  _applyTempoRules();
  saveConfig();
}

// =============================================================================
// HORLOGE — comptage jours Tempo
// =============================================================================

void TempoManager::onClock(uint8_t hh, uint8_t mm,
                            uint8_t day, uint8_t month) {
  // Réinitialisation saison au 1er novembre
  if (day == 1 && month == 11 && _lastMonth != 11) {
    _countRed   = 0;
    _countWhite = 0;
    _lastDay    = 0;
    LOG(LOG_TEMPO, "Saison Tempo reset (1/11)");
    storageManager.saveTempoCounters(0, 0);
  }
  _lastMonth = month;

  // Comptage à minuit — une seule fois par jour
  if (hh == 0 && mm == 0 && day != _lastDay) {
    _lastDay = day;

    // La couleur du jour courant est donnée par le PTEC
    if (_state.color == TempoColor::TEMPO_RED) {
      _countRed++;
      _countRed = min(_countRed, (uint8_t)22);
      LOG(LOG_TEMPO, "Jour ROUGE #%d/22", _countRed);
      storageManager.saveTempoCounters(_countRed, _countWhite);
      if (_publisher) _publisher->publishLog("INFO","TEMPO",
        "Jour ROUGE #%d/22 · BLANC %d/43", _countRed, _countWhite);
    }
    else if (_state.color == TempoColor::TEMPO_WHITE) {
      _countWhite++;
      _countWhite = min(_countWhite, (uint8_t)43);
      LOG(LOG_TEMPO, "Jour BLANC #%d/43", _countWhite);
      storageManager.saveTempoCounters(_countRed, _countWhite);
      if (_publisher) _publisher->publishLog("INFO","TEMPO",
        "Jour BLANC #%d/43 · ROUGE %d/22", _countWhite, _countRed);
    }
  }
}

void TempoManager::setCounters(uint8_t red, uint8_t white) {
  _countRed   = min(red,   (uint8_t)22);
  _countWhite = min(white, (uint8_t)43);
  storageManager.saveTempoCounters(_countRed, _countWhite);
  LOG(LOG_TEMPO, "Counters set manually: red=%d white=%d", _countRed, _countWhite);
  if (_publisher) _publisher->publishLog("INFO","TEMPO",
    "Counters updated manually: ROUGE=%d/22 BLANC=%d/43",
    _countRed, _countWhite);
}

void TempoManager::setForceHG_RedHC(bool val) {
  _config.forceHG_RedHC = val;
  _state.forceHG = _computeForceHG(_state.color, _state.period);
  _applyTempoRules();
  saveConfig();
}

void TempoManager::loadConfig() {
  // Sera implémenté avec StorageManager
  LOG(LOG_TEMPO, "loadConfig() — TODO StorageManager");
}

void TempoManager::saveConfig() {
  // Sera implémenté avec StorageManager
  LOG(LOG_TEMPO, "saveConfig() — TODO StorageManager");
}

// =============================================================================
// PARSING INTERNE
// =============================================================================

bool TempoManager::_parsePtec(const char*  ptec,
                               TempoColor&  color,
                               TempoPeriod& period) const {
  // Format mode historique : 4 chars ex "HPJB"
  //   [0] = 'H' (Heure)
  //   [1] = 'P' (Pleines) ou 'C' (Creuses)
  //   [2] = 'J' (Jour)
  //   [3] = 'B' (Bleu) / 'W' (Blanc) / 'R' (Rouge)

  if (!ptec || strlen(ptec) < 4) return false;

  // Validation minimale
  if (ptec[0] != 'H') return false;
  if (ptec[1] != 'P' && ptec[1] != 'C') return false;
  if (ptec[2] != 'J') return false;

  // Période
  period = (ptec[1] == 'P') ? TempoPeriod::HP : TempoPeriod::HC;

  // Couleur
  switch (ptec[3]) {
    case 'B': color = TempoColor::TEMPO_BLUE;  break;
    case 'W': color = TempoColor::TEMPO_WHITE; break;
    case 'R': color = TempoColor::TEMPO_RED;   break;
    default:
      color = TempoColor::UNKNOWN;
      return false;
  }

  return true;
}

TempoColor TempoManager::_parseDemain(const char* demain) const {
  // Format mode historique Linky
  if (!demain || demain[0] == '\0') return TempoColor::UNKNOWN;
  if (strcmp(demain, "BLEU") == 0)  return TempoColor::TEMPO_BLUE;
  if (strcmp(demain, "BLAN") == 0)  return TempoColor::TEMPO_WHITE;
  if (strcmp(demain, "ROUG") == 0)  return TempoColor::TEMPO_RED;
  return TempoColor::UNKNOWN;  // "----" ou inconnu
}

// =============================================================================
// LOGIQUE MÉTIER
// =============================================================================

bool TempoManager::_computeForceHG(TempoColor  color,
                                    TempoPeriod period) const {
  if (!_config.enabled) return false;
  if (color == TempoColor::UNKNOWN) return false;

  // Blanc + HP
  if (color == TempoColor::TEMPO_WHITE &&
      period == TempoPeriod::HP &&
      _config.forceHG_WhiteHP) {
    return true;
  }

  // Rouge + HP
  if (color == TempoColor::TEMPO_RED &&
      period == TempoPeriod::HP &&
      _config.forceHG_RedHP) {
    return true;
  }

  // Rouge + HC (optionnel)
  if (color == TempoColor::TEMPO_RED &&
      period == TempoPeriod::HC &&
      _config.forceHG_RedHC) {
    return true;
  }

  return false;
}

void TempoManager::_applyTempoRules() {
  if (!_cmdHandler) return;

  if (_state.forceHG && _config.enabled) {
    LOG(LOG_TEMPO, "Applying HG on zones (color=%s period=%s)",
        tempoColorToStr(_state.color),
        (_state.period == TempoPeriod::HP) ? "HP" : "HC");

    for (uint8_t z = 1; z <= NUM_ZONES; z++) {
      // Ne pas forcer HG si la zone est en STOP
      // Vérifie à la fois la commande courante ET la commande sauvegardée
      HeatingCmd current = zoneManager.getCurrentCmd(z);
      if (current == HeatingCmd::STOP) {
        LOG(LOG_TEMPO, "Z%d STOP — skipping Tempo HG", z);
        continue;
      }

      // Vérifie aussi la commande EEPROM (cas recovery → STOP puis Tempo)
      char savedCmd[10] = {0};
      if (storageManager.loadZoneCmd(z, savedCmd, sizeof(savedCmd))) {
        HeatingCmd saved = CommandHandler::parseCmd(savedCmd);
        if (saved == HeatingCmd::STOP) {
          LOG(LOG_TEMPO, "Z%d EEPROM=STOP — skipping Tempo HG", z);
          continue;
        }
      }

      _cmdHandler->handleCmd(z,
                              HeatingCmd::HG,
                              CommandOrigin::SYSTEM,
                              CommandSource::SRC_TEMPO);
    }

    if (_publisher) {
      _publisher->publishLog("INFO", "TEMPO",
        "HG forced on eligible zones — %s %s",
        tempoColorToStr(_state.color),
        (_state.period == TempoPeriod::HP) ? "HP" : "HC");
    }

  } else {
    // Fin restriction HP → restaure les commandes sauvegardées (HC ou jour bleu)
    if (_state.forceHG) {
      LOG(LOG_TEMPO, "Restriction lifted — restoring zones from EEPROM");
      if (_cmdHandler) _cmdHandler->handleRecovery();
    }

    LOG(LOG_TEMPO, "No restriction (color=%s period=%s forceHG=0)",
        tempoColorToStr(_state.color),
        (_state.period == TempoPeriod::HP) ? "HP" : "HC");

    if (_publisher) {
      _publisher->publishLog("INFO", "TEMPO",
        "Restriction lifted — %s %s",
        tempoColorToStr(_state.color),
        (_state.period == TempoPeriod::HP) ? "HP" : "HC");
    }
  }
}

void TempoManager::_publishState() {
  if (!_publisher) return;
  _publisher->publishTempoState(_state, _tomorrowColor);
}

// =============================================================================
// DEBUG
// =============================================================================

void TempoManager::dump() const {
  Serial.printf(
    "%s === Tempo State ===\n"
    "%s   enabled    : %d\n"
    "%s   color      : %s\n"
    "%s   period     : %s\n"
    "%s   forceHG    : %d\n"
    "%s   tomorrow   : %s\n"
    "%s   cfg WhiteHP: %d  RedHP: %d  RedHC: %d\n"
    "%s ==================\n",
    LOG_TEMPO,
    LOG_TEMPO, _config.enabled,
    LOG_TEMPO, tempoColorToStr(_state.color),
    LOG_TEMPO, (_state.period == TempoPeriod::HP) ? "HP" : "HC",
    LOG_TEMPO, _state.forceHG,
    LOG_TEMPO, tempoColorToStr(_tomorrowColor),
    LOG_TEMPO, _config.forceHG_WhiteHP,
              _config.forceHG_RedHP,
              _config.forceHG_RedHC,
    LOG_TEMPO
  );
}
