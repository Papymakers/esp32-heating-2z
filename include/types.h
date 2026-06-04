#pragma once
// =============================================================================
// types.h — Enums, structs et events partagés entre tous les modules
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include <Arduino.h>

// =============================================================================
// SYSTEM FSM
// =============================================================================

enum class SystemState : uint8_t {
  INIT,       // Démarrage, initialisation hardware
  RUNNING,    // Fonctionnement normal
  SAFE,       // Mode sécurité (surcharge détectée)
  FAULT       // Erreur critique, redémarrage imminent
};

// =============================================================================
// ZONE FSM
// =============================================================================

enum class ZoneState : uint8_t {
  IDLE,       // Zone non initialisée
  NORMAL,     // Fonctionnement normal (user / calendrier / tempo)
  OVERRIDE,   // Surcharge : commande système temporaire
  RESTORE,    // Restauration après surcharge (timer en cours)
  LOCKED      // Verrouillée (FAULT système)
};

// =============================================================================
// EVENTS SYSTÈME
// =============================================================================

enum class SystemEvent : uint8_t {
  EVT_NONE,
  EVT_WIFI_UP,
  EVT_WIFI_DOWN,
  EVT_MQTT_UP,
  EVT_MQTT_DOWN,
  EVT_OVERLOAD_START,
  EVT_OVERLOAD_END,
  EVT_TEMPO_CHANGE,
  EVT_SCHEDULE_TRIGGER,
  EVT_USER_CMD,
  EVT_BOOT_LONG_PRESS,
  EVT_FAULT
};

// =============================================================================
// COMMANDES DE CHAUFFAGE
// Fil pilote : 2 fils (pos/neg), tension 230V via MOC3041 (actif bas)
//   STOP  : pos=LOW,  neg=HIGH  → demi-onde négative
//   HG    : pos=HIGH, neg=LOW   → demi-onde positive
//   ECO   : pos=LOW,  neg=LOW   → pas de signal
//   CONF  : pos=HIGH, neg=HIGH  → signal complet
//   CM2   : alternance 7s/293s (CONF-2°C)
// =============================================================================

enum class HeatingCmd : uint8_t {
  STOP = 0,
  HG,
  ECO,
  CONF,
  CM2,        // Confort -2°C (timer alterné)
  UNKNOWN
};

// =============================================================================
// ORIGINES ET SOURCES DE COMMANDE
// Utilisées pour la priorité et la logique EEPROM
// =============================================================================

enum class CommandOrigin : uint8_t {
  USER,       // Commande manuelle (switch physique, WebSocket, MQTT)
  SYSTEM      // Commande automatique (tempo, overload, calendrier, boot)
};

enum class CommandSource : uint8_t {
  // --- USER
  SRC_SW,         // Switch physique
  SRC_WS,         // WebSocket (interface web)
  SRC_MQTT,       // MQTT externe

  // --- SYSTEM
  SRC_TEMPO,      // Tempo EDF (rouge/blanc → HG forcé)
  SRC_OVERLOAD,   // Délestage surcharge
  SRC_SCHEDULE,   // Calendrier programmé
  SRC_DEFAULT,    // Valeur par défaut au boot
  SRC_RECOVERY,   // Restauration depuis EEPROM
  SRC_TIMER       // Timer CM2 (alternance 7s/293s)
};

// =============================================================================
// PRIORITÉS DE COMMANDE
// Plus la valeur est haute, plus la priorité est haute
// =============================================================================

enum class CommandPriority : uint8_t {
  PRIO_DEFAULT   = 0,   // Boot default
  PRIO_SCHEDULE  = 1,   // Calendrier
  PRIO_USER      = 2,   // Commande manuelle
  PRIO_OVERLOAD  = 3,   // Délestage
  PRIO_TEMPO     = 4,   // Tempo EDF
  PRIO_FAULT     = 5    // Erreur critique
};

// =============================================================================
// CONTEXTE D'UNE COMMANDE
// Transporté à travers tout le pipeline de traitement
// =============================================================================

struct CommandContext {
  HeatingCmd    cmd;          // Commande finale résolue
  HeatingCmd    inputCmd;     // Commande d'entrée (avant résolution Tempo)
  CommandOrigin origin;
  CommandSource source;
  CommandPriority priority;
  bool          isPersistent; // Sauvegarde EEPROM autorisée
  bool          isTemporary;  // Commande temporaire (pas de sauvegarde)
  bool          isStop;       // Est-ce une commande d'arrêt ?
  uint8_t       zone;         // Zone concernée (1-4)
};

// =============================================================================
// TEMPO EDF
// =============================================================================

enum class TempoColor : uint8_t {
  UNKNOWN = 0,
  TEMPO_BLUE,         // Jour bleu  → pas de restriction
  TEMPO_WHITE,        // Jour blanc → restriction modérée → HG forcé
  TEMPO_RED           // Jour rouge → restriction max     → HG forcé
};

enum class TempoPeriod : uint8_t {
  UNKNOWN = 0,
  HC,           // Heures Creuses
  HP            // Heures Pleines
};

struct TempoState {
  TempoColor  color;
  TempoPeriod period;
  bool        active;         // Option Tempo activée par l'utilisateur
  bool        forceHG;        // true si WHITE+HP ou RED (→ HG forcé)
};

// =============================================================================
// CALENDRIER — PROFILS
// =============================================================================

constexpr uint8_t SLOTS_PER_DAY  = 48;   // 30 min × 48 = 24h
constexpr uint8_t MAX_PROFILES   = 8;
constexpr uint8_t PROFILE_NAME_LEN = 12;

struct ScheduleProfile {
  char      name[PROFILE_NAME_LEN]; // Ex: "Nuit", "Jour", "Absent"
  HeatingCmd slots[SLOTS_PER_DAY];  // Commande pour chaque slot de 30min
  bool      active;
};

// Association zone/jour → profil
// 7 jours (0=lundi … 6=dimanche) × 4 zones → index de profil
struct ZoneSchedule {
  uint8_t profileIndex[7]; // index dans scheduleProfiles[]
  bool    enabled;          // Calendrier actif pour cette zone
};

// =============================================================================
// ÉTAT D'UNE ZONE
// =============================================================================

enum class Cm2Mode : uint8_t {
  MODE_7S,      // Phase courte CONF (7s)
  MODE_293S     // Phase longue ECO  (293s)
};

struct ZoneData {
  uint8_t       id;                  // 1-4
  ZoneState     state;
  HeatingCmd    currentCmd;          // Commande active
  HeatingCmd    savedCmd;            // Commande sauvegardée avant override
  CommandPriority activePriority;    // Priorité de la commande active
  bool          posState;            // État sortie MOC pos
  bool          negState;            // État sortie MOC neg
  bool          timerActive;         // Timer CM2 actif
  Cm2Mode       cm2Mode;             // Phase CM2 courante
  unsigned long timerStart;          // Timestamp départ timer
  unsigned long overloadStart;       // Timestamp début surcharge
  unsigned long restoreTimer;        // Timestamp début restauration
};

// =============================================================================
// SWITCHES PHYSIQUES
// =============================================================================

struct PhysicalSwitch {
  volatile bool pressed;
  uint8_t       clickCount;
  unsigned long lastDebounce;
  int           zone;
  bool          glitch;   // true = ISR déclenchée mais pin encore HIGH (parasite)
};

struct BootButton {
  volatile bool   pressed;
  volatile uint32_t pressStartTime;
  bool            erased;
};

// =============================================================================
// STATUTS SYSTÈME (runtime)
// =============================================================================

struct SystemStatus {
  SystemState state;
  bool        wifiConnected;
  bool        mqttConnected;
  bool        linkyConnected;
  bool        eepromPresent;
  bool        scheduleEnabled;  // Calendrier globalement actif
  bool        tempoEnabled;     // Option Tempo globalement active
  uint8_t     selectedZone;     // Zone sélectionnée par switch (1-4)
  uint8_t     wifiFailCount;
  unsigned long lastWifiCheck;
};

// =============================================================================
// FLAGS DE COMMANDES (pour CommandHandler)
// =============================================================================

enum CommandFlags : uint8_t {
  FLAG_NONE       = 0,
  FLAG_STOP       = 1 << 0,
  FLAG_TEMPORARY  = 1 << 1,
  FLAG_PERSISTENT = 1 << 2
};

// =============================================================================
// HELPERS — Conversions string (debug / MQTT)
// =============================================================================

inline const char* heatingCmdToStr(HeatingCmd cmd) {
  switch (cmd) {
    case HeatingCmd::STOP: return "STOP";
    case HeatingCmd::HG:   return "HG";
    case HeatingCmd::ECO:  return "ECO";
    case HeatingCmd::CONF: return "CONF";
    case HeatingCmd::CM2:  return "CM2";
    default:               return "UNKNOWN";
  }
}

inline const char* zoneStateToStr(ZoneState s) {
  switch (s) {
    case ZoneState::IDLE:     return "IDLE";
    case ZoneState::NORMAL:   return "NORMAL";
    case ZoneState::OVERRIDE: return "OVERRIDE";
    case ZoneState::RESTORE:  return "RESTORE";
    case ZoneState::LOCKED:   return "LOCKED";
    default:                  return "?";
  }
}

inline const char* systemStateToStr(SystemState s) {
  switch (s) {
    case SystemState::INIT:    return "INIT";
    case SystemState::RUNNING: return "RUNNING";
    case SystemState::SAFE:    return "SAFE";
    case SystemState::FAULT:   return "FAULT";
    default:                   return "?";
  }
}

inline const char* tempoColorToStr(TempoColor c) {
  switch (c) {
    case TempoColor::TEMPO_BLUE:  return "BLUE";
    case TempoColor::TEMPO_WHITE: return "WHITE";
    case TempoColor::TEMPO_RED:   return "RED";
    default:                return "UNKNOWN";
  }
}

inline const char* originToStr(CommandOrigin o) {
  return (o == CommandOrigin::USER) ? "USER" : "SYSTEM";
}

inline const char* sourceToStr(CommandSource s) {
  switch (s) {
    case CommandSource::SRC_SW:       return "SW";
    case CommandSource::SRC_WS:       return "WS";
    case CommandSource::SRC_MQTT:     return "MQTT";
    case CommandSource::SRC_TEMPO:    return "TEMPO";
    case CommandSource::SRC_OVERLOAD: return "OVERLOAD";
    case CommandSource::SRC_SCHEDULE: return "SCHEDULE";
    case CommandSource::SRC_DEFAULT:  return "DEFAULT";
    case CommandSource::SRC_RECOVERY: return "RECOVERY";
    case CommandSource::SRC_TIMER:    return "TIMER";
    default:                          return "?";
  }
}
