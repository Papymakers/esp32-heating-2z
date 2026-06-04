#pragma once
// =============================================================================
// TempoManager.h — Gestion option TEMPO EDF
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Mode historique TIC Linky :
//   PTEC = période tarifaire courante (4 chars)
//     [0] = type  : 'H' (Heures) ou autre
//     [1] = mode  : 'P' (Pleines) ou 'C' (Creuses)
//     [2] = sépar : 'J' (jour)
//     [3] = color : 'B' (Bleu) / 'W' (Blanc) / 'R' (Rouge)
//   Exemples : "HPJB" = HP Jour Bleu, "HCJR" = HC Jour Rouge
//
//   DEMAIN = couleur du lendemain : "BLEU", "BLAN", "ROUG" ou "----"
//
// Règles de chauffage TEMPO :
//   Bleu  + HC → libre
//   Bleu  + HP → libre
//   Blanc + HC → libre
//   Blanc + HP → HG forcé (économie modérée)
//   Rouge + HC → libre (optionnel selon config)
//   Rouge + HP → HG forcé (économie max)
//
// Priorité TEMPO sur calendrier et commandes user
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations
class Publisher;
class CommandHandler;

// =============================================================================
// CONFIGURATION TEMPO (ajustable)
// =============================================================================

// Comportement par couleur/période — modifiable via web UI
struct TempoConfig {
  bool forceHG_WhiteHP;   // Blanc HP → HG forcé (défaut: true)
  bool forceHG_RedHP;     // Rouge HP → HG forcé (défaut: true)
  bool forceHG_RedHC;     // Rouge HC → HG forcé (défaut: false)
  bool enabled;           // Option Tempo globalement active
};

// =============================================================================
// CLASSE TEMPOMANAGER
// =============================================================================

class TempoManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  void begin(CommandHandler* cmdHandler, Publisher* publisher);

  // ---------------------------------------------------------------------------
  // Mise à jour depuis Linky (appelé par LinkyReader)
  // ---------------------------------------------------------------------------

  // Met à jour PTEC et déclenche les règles si changement
  // ptec = string 4 chars ex: "HPJB", "HCJR"
  void onPtecChange(const char* ptec);

  // Met à jour DEMAIN
  // demain = "BLEU", "BLAN", "ROUG" ou "----"
  void onDemainChange(const char* demain);

  // ---------------------------------------------------------------------------
  // Getters état
  // ---------------------------------------------------------------------------

  const TempoState&  getState()  const { return _state; }
  const TempoConfig& getConfig() const { return _config; }

  TempoColor  getCurrentColor()  const { return _state.color;  }
  TempoPeriod getCurrentPeriod() const { return _state.period; }
  TempoColor  getTomorrowColor() const { return _tomorrowColor; }
  bool        isActive()         const { return _config.enabled; }
  bool        isForceHG()        const { return _state.forceHG; }

  // Compteurs jours Tempo saison en cours
  uint8_t getCountRed()   const { return _countRed;   }
  uint8_t getCountWhite() const { return _countWhite; }

  // ---------------------------------------------------------------------------
  // Horloge — appelé depuis Publisher::_handleUtcClock
  // ---------------------------------------------------------------------------

  void onClock(uint8_t hh, uint8_t mm, uint8_t day, uint8_t month);

  // ---------------------------------------------------------------------------
  // Configuration (depuis web UI ou MQTT)
  // ---------------------------------------------------------------------------

  void setEnabled(bool enabled);
  void setForceHG_WhiteHP(bool val);
  void setForceHG_RedHP(bool val);
  void setForceHG_RedHC(bool val);

  // Mise à jour manuelle des compteurs (via MQTT topic setCounters)
  void setCounters(uint8_t red, uint8_t white);

  // Charge/sauvegarde config depuis StorageManager
  void loadConfig();
  void saveConfig();

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dump() const;

private:

  TempoState  _state        {};
  TempoConfig _config       {};
  TempoColor  _tomorrowColor = TempoColor::UNKNOWN;

  // Compteurs jours Tempo saison (reset le 1/11)
  uint8_t       _countRed   = 0;
  uint8_t       _countWhite = 0;
  uint8_t       _lastDay    = 0;   // dernier jour comptabilisé (évite double compte)
  uint8_t       _lastMonth  = 0;

  // Derniers PTEC/DEMAIN reçus (pour détecter les changements)
  char _lastPtec[5]   = {0};
  char _lastDemain[5] = {0};

  // Dépendances
  CommandHandler* _cmdHandler = nullptr;
  Publisher*      _publisher  = nullptr;

  // ---------------------------------------------------------------------------
  // Parsing interne
  // ---------------------------------------------------------------------------

  // Parse "HPJB" → color + period
  bool _parsePtec(const char* ptec,
                  TempoColor& color,
                  TempoPeriod& period) const;

  // Parse "BLEU"/"BLAN"/"ROUG"/"----" → TempoColor
  TempoColor _parseDemain(const char* demain) const;

  // ---------------------------------------------------------------------------
  // Logique métier
  // ---------------------------------------------------------------------------

  // Recalcule forceHG selon couleur + période + config
  bool _computeForceHG(TempoColor color, TempoPeriod period) const;

  // Applique les commandes de chauffage sur toutes les zones
  void _applyTempoRules();

  // Publie l'état Tempo via MQTT + WebSocket
  void _publishState();
};

// Instance globale
extern TempoManager tempoManager;
