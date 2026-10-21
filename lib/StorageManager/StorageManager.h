#pragma once
// =============================================================================
// StorageManager.h — Persistance EEPROM I2C 24C32…24C512 + NVS Flash
// ESP32 Heating Controller v5.1
// Denis Mattera - 2025
//
// 1. EEPROM 24C32 à 24C512 (adressage 16 bits, page déclarée 32 o) :
//    24C32 / 24LC32A / AT24C32 … 24CS512, 24LC512, AT24C512
//    (taille fixe, pas de détection ; n° de série et ID fabricant facultatifs)
//    Données utilisateur, survie aux coupures. Carte mémoire dans config.h :
//      0x0000  en-tête (magic "NAUC" + version de layout)
//      0x0010  commandes zones (16 o / zone)
//      0x0050  flags système, 0x0054-55 compteurs Tempo
//      0x0100  profils calendrier (64 o / profil)
//      0x0300  associations zone/jour (16 o / zone)
//    Au démarrage : en-tête absent (puce vierge) ou étranger → formatage
//    de la zone utilisée (0x0000-0x03FF) et écriture de l'en-tête.
//
//    Registre de sécurité 24CSxxx uniquement (adresse I2C 0x58) :
//      0x0800-0x080F  n° de série 128 bits, unique, lecture seule
//      0x0880-0x08FF  page ID 128 o, programmable puis verrouillable
//    Le n° de série sert à identifier le module (journal, /api/state,
//    MQTT au boot) et à détecter un changement de module EEPROM (n° de
//    série comparé à celui mémorisé en NVS côté ESP32). Sans n° de série
//    (24LC512, AT24C512), le changement de module n'est pas détectable.
//
// 2. NVS Flash ESP32 — configuration (Tempo, délestage, traces, n° série)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>    // NVS Flash ESP32
#include "types.h"
#include "config.h"

// Commandes zones : chaîne de 10 octets max dans un enregistrement de 16
constexpr uint16_t EE_ZONE_SIZE       = 10;

// Flags système (1 octet)
constexpr uint8_t  EE_FLAG_TEMPO      = 0x01;
constexpr uint8_t  EE_FLAG_SCHEDULE   = 0x02;
constexpr uint8_t  EE_FLAG_OVERLOAD   = 0x04;

// Profil : 12 o nom + 48 o slots = 60 o dans un enregistrement de 64
constexpr uint16_t EE_PROFILE_SIZE    = PROFILE_NAME_LEN + SLOTS_PER_DAY;
static_assert(EE_PROFILE_SIZE <= EE_PROFILE_STRIDE, "profil > stride");

// Association zone/jour : 7 o + 1 flag = 8 o dans un enregistrement de 16
constexpr uint16_t EE_ZONE_SCHED_SIZE = 8;

// =============================================================================
// NAMESPACES NVS
// =============================================================================

constexpr char NVS_NS_TEMPO[]    = "tempo";
constexpr char NVS_NS_OVERLOAD[] = "overload";
constexpr char NVS_NS_SYSTEM[]   = "system";

// =============================================================================
// CLASSE STORAGEMANAGER
// =============================================================================

class StorageManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  // Retourne true si l'EEPROM répond sur le bus I2C
  bool begin();

  bool isEepromPresent() const { return _eepromOk; }

  // ---------------------------------------------------------------------------
  // Identification de la puce (facultative : 24CSxxx seulement)
  // ---------------------------------------------------------------------------
  bool        hasSerial()       const { return _serialOk; }
  const char* getSerialHex()    const { return _serialHex; }   // 32 car. hex
  uint32_t    getMfgId()        const { return _mfgId; }       // 0 = non lu
  const char* chipName()        const;  // "24CS512", "EEPROM (sans ID)"…
  bool        serialChanged()   const { return _serialChanged; } // module remplacé
  bool        wasFormatted()    const { return _formatted; }     // en-tête écrit au boot

  // Lecture de la page ID utilisateur (128 o, 0x0880) — réservée à un usage
  // futur (révision carte, date de fabrication…). Écriture/verrouillage non
  // implémentés volontairement : le verrouillage est irréversible.
  bool readIdPage(uint8_t* buf, uint8_t len);

  // Supervision : re-sonde l'EEPROM (détecte perte ET retour du composant)
  bool recheck();
  uint32_t getErrorCount() const { return _errorCount; }

  // Calendrier persistant : disponible dès la 24C32 (carte mémoire = 1 Ko).
  static constexpr bool hasCalendarStorage() { return EEPROM_ADDR_16; }

  // ---------------------------------------------------------------------------
  // EEPROM — Commandes zones
  // ---------------------------------------------------------------------------

  // Sauvegarde la commande active d'une zone (zoneId 1-4)
  bool saveZoneCmd(uint8_t zoneId, const char* cmdStr);

  // Charge la commande d'une zone (retourne false si invalide/absente)
  bool loadZoneCmd(uint8_t zoneId, char* buf, size_t bufLen);

  // Charge toutes les zones (retourne le nb de zones chargées)
  uint8_t loadAllZoneCmds(char cmds[NUM_ZONES][10]);

  // ---------------------------------------------------------------------------
  // EEPROM — Flags système
  // ---------------------------------------------------------------------------

  bool saveSysFlags(bool tempoEnabled,
                    bool scheduleEnabled,
                    bool overloadEnabled);

  bool loadSysFlags(bool& tempoEnabled,
                    bool& scheduleEnabled,
                    bool& overloadEnabled);

  // Mémorise le seul bit « calendrier actif » (lecture-modification-
  // écriture, écriture évitée si inchangé). Tempo et délestage ont leur
  // propre configuration persistante ; leurs bits restent neutres.
  bool saveScheduleFlag(bool enabled);

  // ---------------------------------------------------------------------------
  // EEPROM — Profils calendrier
  // ---------------------------------------------------------------------------

  // Sauvegarde un profil (index 0-7)
  bool saveProfile(uint8_t profileIndex, const ScheduleProfile& profile);

  // Charge un profil
  bool loadProfile(uint8_t profileIndex, ScheduleProfile& profile);

  // Charge tous les profils
  uint8_t loadAllProfiles(ScheduleProfile profiles[MAX_PROFILES]);

  // ---------------------------------------------------------------------------
  // EEPROM — Associations zone/jour
  // ---------------------------------------------------------------------------

  bool saveZoneSchedule(uint8_t zoneId, const ZoneSchedule& sched);
  bool loadZoneSchedule(uint8_t zoneId, ZoneSchedule& sched);
  uint8_t loadAllZoneSchedules(ZoneSchedule schedules[NUM_ZONES]);

  // ---------------------------------------------------------------------------
  // EEPROM — Utilitaires
  // ---------------------------------------------------------------------------

  // Efface la zone utilisée (0x0000–EE_USED_END) puis réécrit l'en-tête
  bool eraseAll();

  // Vérifie la présence de l'EEPROM sur le bus I2C
  bool probe();

  // Dump hexadécimal d'une plage EEPROM (debug)
  void dumpHex(uint16_t addr, uint16_t len) const;

  // ---------------------------------------------------------------------------
  // NVS Flash — Config Tempo
  // ---------------------------------------------------------------------------

  bool saveTempoConfig(bool enabled,
                       bool forceHG_WhiteHP,
                       bool forceHG_RedHP,
                       bool forceHG_RedHC);

  bool loadTempoConfig(bool& enabled,
                       bool& forceHG_WhiteHP,
                       bool& forceHG_RedHP,
                       bool& forceHG_RedHC);

  // Compteurs jours Tempo saison
  bool saveTempoCounters(uint8_t red, uint8_t white);
  bool loadTempoCounters(uint8_t& red, uint8_t& white);

  // ---------------------------------------------------------------------------
  // NVS Flash — Config Overload
  // ---------------------------------------------------------------------------

  bool saveOverloadConfig(bool          enabled,
                          unsigned long thresholdMs,
                          unsigned long restoreDelayMs,
                          HeatingCmd    fallbackCmd);

  bool loadOverloadConfig(bool&          enabled,
                          unsigned long& thresholdMs,
                          unsigned long& restoreDelayMs,
                          HeatingCmd&    fallbackCmd);

  // ---------------------------------------------------------------------------
  // NVS Flash — Utilitaires
  // ---------------------------------------------------------------------------

  void clearNvs(const char* ns);

  // ---------------------------------------------------------------------------
  // NVS Flash — Réglages des traces (voir Logger.h / log_config.h)
  // ---------------------------------------------------------------------------

  bool saveLogConfig();   // sauvegarde l'état courant de logState()
  bool loadLogConfig();   // applique la config NVS si elle existe

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dump() const;

private:

  bool        _eepromOk = false;
  mutable uint32_t _errorCount = 0;   // erreurs I2C depuis le boot

  // Identification
  bool        _serialOk      = false;
  bool        _serialChanged = false;
  bool        _formatted     = false;
  uint8_t     _serial[16]    = {0};
  char        _serialHex[33] = {0};
  uint32_t    _mfgId         = 0;

  bool _readSerial();
  bool _readMfgId();
  void _checkSerialChange();
  bool _checkHeader();          // formate si vierge / étrangère
  bool _writeHeader();
  bool _fill(uint16_t from, uint16_t to, uint8_t val);
  Preferences _prefs;

  // ---------------------------------------------------------------------------
  // Primitives EEPROM bas niveau
  // ---------------------------------------------------------------------------

  bool _write(uint16_t addr, const uint8_t* data, uint16_t len);
  bool _read (uint16_t addr, uint8_t* data, uint16_t len) const;

  // Écriture d'un seul octet
  bool _writeByte(uint16_t addr, uint8_t val);

  // Attend la fin du cycle d'écriture interne (ACK polling, max ~10 ms)
  bool _waitReady() const;
  bool _readByte (uint16_t addr, uint8_t& val) const;

  // Adresse EEPROM d'une zone (0-indexed)
  static constexpr uint16_t _zoneAddr(uint8_t zi) {
    return EE_ZONE_BASE + zi * EE_ZONE_STRIDE;
  }

  // Adresse EEPROM d'un profil (0-indexed)
  static constexpr uint16_t _profileAddr(uint8_t pi) {
    return EE_PROFILES_BASE + pi * EE_PROFILE_STRIDE;
  }

  // Adresse EEPROM d'une association zone/jour (0-indexed)
  static constexpr uint16_t _schedAddr(uint8_t zi) {
    return EE_SCHEDULE_BASE + zi * EE_SCHED_STRIDE;
  }

  // Validation d'une commande string
  static bool _isValidCmdStr(const char* s);
};

// Instance globale
extern StorageManager storageManager;