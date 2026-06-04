#pragma once
// =============================================================================
// StorageManager.h — Persistance EEPROM 24C32 (I2C) + NVS Flash
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Deux couches de stockage :
//
// 1. EEPROM 24C32 (I2C, 4KB) — données critiques survie au reboot
//    Accès lent (~5ms/écriture), endurance 1M cycles
//    Contenu :
//      [0x000-0x027] Commandes zones    (4 × 10 octets)
//      [0x028-0x02F] Flags système      (8 octets)
//      [0x030-0x22F] Profils calendrier (8 profils × nom + 48 slots)
//      [0x230-0x25F] Associations zone/jour (4 zones × 7 jours + flags)
//
// 2. NVS Flash ESP32 — configuration système (grande capacité, key/value)
//    Accès rapide, endurance 10k cycles sur même secteur
//    Contenu :
//      Config Tempo (enabled, règles)
//      Config Overload (seuils, fallback)
//      Config réseau (si modifiable)
//      Config Overload personnalisée
//
// Stratégie :
//   - Commandes zones → EEPROM (critique, survie coupure secteur)
//   - Profils calendrier → EEPROM (données volumineuses)
//   - Config modules → NVS Flash (petites config, accès fréquent)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>    // NVS Flash ESP32
#include "types.h"
#include "config.h"

// =============================================================================
// LAYOUT EEPROM 24C32 (4096 octets)
// =============================================================================

// Zones : 4 × 10 octets = 40 octets [0x000-0x027]
constexpr uint16_t EE_ZONE_SIZE       = 10;    // Octets par zone
// (défini dans config.h : EE_ZONE_BASE = 0x000)

// Flags système : 8 octets [0x028-0x02F]
// Byte 0 : bit0=tempoEnabled, bit1=scheduleEnabled, bit2=overloadEnabled
// (défini dans config.h : EE_SYS_FLAGS = 0x028)
constexpr uint8_t  EE_FLAG_TEMPO      = 0x01;
constexpr uint8_t  EE_FLAG_SCHEDULE   = 0x02;
constexpr uint8_t  EE_FLAG_OVERLOAD   = 0x04;

// Profils calendrier [0x030-0x22F]
// 8 profils × (12 octets nom + 48 octets slots) = 8 × 60 = 480 octets
// (défini dans config.h : EE_PROFILES_BASE = 0x030)
constexpr uint16_t EE_PROFILE_SIZE    = PROFILE_NAME_LEN + SLOTS_PER_DAY;  // 60 octets
// Total profils : 8 × 60 = 480 octets → [0x030-0x21F]

// Associations zone/jour [0x230-0x25F]
// 4 zones × (7 jours × 1 octet + 1 flag) = 4 × 8 = 32 octets
// (défini dans config.h : EE_SCHEDULE_BASE = 0x230)
constexpr uint16_t EE_ZONE_SCHED_SIZE = 8;   // 7 jours + 1 flag enabled

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

  // Retourne true si EEPROM 24C32 détectée sur le bus I2C
  bool begin();

  bool isEepromPresent() const { return _eepromOk; }

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

  // Efface toute l'EEPROM (0xFF)
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
  // Debug
  // ---------------------------------------------------------------------------

  void dump() const;

private:

  bool        _eepromOk = false;
  Preferences _prefs;

  // ---------------------------------------------------------------------------
  // Primitives EEPROM bas niveau
  // ---------------------------------------------------------------------------

  bool _write(uint16_t addr, const uint8_t* data, uint16_t len);
  bool _read (uint16_t addr, uint8_t* data, uint16_t len) const;

  // Écriture d'un seul octet
  bool _writeByte(uint16_t addr, uint8_t val);
  bool _readByte (uint16_t addr, uint8_t& val) const;

  // Adresse EEPROM d'une zone (0-indexed)
  static constexpr uint16_t _zoneAddr(uint8_t zi) {
    return EE_ZONE_BASE + zi * EE_ZONE_SIZE;
  }

  // Adresse EEPROM d'un profil (0-indexed)
  static constexpr uint16_t _profileAddr(uint8_t pi) {
    return EE_PROFILES_BASE + pi * EE_PROFILE_SIZE;
  }

  // Adresse EEPROM d'une association zone/jour (0-indexed)
  static constexpr uint16_t _schedAddr(uint8_t zi) {
    return EE_SCHEDULE_BASE + zi * EE_ZONE_SCHED_SIZE;
  }

  // Validation d'une commande string
  static bool _isValidCmdStr(const char* s);
};

// Instance globale
extern StorageManager storageManager;
