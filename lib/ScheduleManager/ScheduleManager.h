#pragma once
// =============================================================================
// ScheduleManager.h — Gestion du calendrier de chauffage par profils
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Concept :
//   - Jusqu'à MAX_PROFILES (8) profils réutilisables
//   - Chaque profil contient 48 slots de 30 min (24h)
//   - Chaque slot contient une HeatingCmd (STOP/HG/ECO/CONF/CM2)
//   - Chaque zone associe un profil par jour de semaine (0=Lun … 6=Dim)
//   - Le calendrier est activable/désactivable par zone et globalement
//
// Priorité :
//   - TEMPO > OVERLOAD > USER > SCHEDULE > DEFAULT
//   - Le ScheduleManager n'applique que si aucune commande de priorité
//     supérieure n'est active sur la zone
//
// Horloge :
//   - onClock(hh, mm) déclenché par Publisher depuis MQTT utcClock
//   - Vérification toutes les 30s (SCHEDULE_CHECK_MS)
//   - Application au changement de slot (toutes les 30min)
//
// Slots :
//   slot = hh * 2 + (mm >= 30 ? 1 : 0)
//   slot 0 = 00h00-00h29, slot 1 = 00h30-00h59, ..., slot 47 = 23h30-23h59
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations
class CommandHandler;
class StorageManager;
class Publisher;

// =============================================================================
// CLASSE SCHEDULEMANAGER
// =============================================================================

class ScheduleManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  void begin(CommandHandler* cmdHandler,
             StorageManager* storageMgr,
             Publisher*      publisher);

  // ---------------------------------------------------------------------------
  // Horloge UTC — appelé par Publisher depuis MQTT utcClock
  // ---------------------------------------------------------------------------

  void onClock(uint8_t hh, uint8_t mm);

  // ---------------------------------------------------------------------------
  // Activation globale
  // ---------------------------------------------------------------------------

  void     setEnabled(bool enabled);
  bool     isEnabled() const { return _enabled; }

  // ---------------------------------------------------------------------------
  // Activation par zone
  // ---------------------------------------------------------------------------

  void     setZoneEnabled(uint8_t zoneId, bool enabled);
  bool     isZoneEnabled(uint8_t zoneId) const;

  // ---------------------------------------------------------------------------
  // Gestion des profils
  // ---------------------------------------------------------------------------

  // Crée ou met à jour un profil
  bool setProfile(uint8_t profileIndex, const ScheduleProfile& profile);

  // Récupère un profil (const ref)
  const ScheduleProfile& getProfile(uint8_t profileIndex) const;

  // Retourne le nombre de profils actifs
  uint8_t getProfileCount() const;

  // Supprime un profil (slots → ECO, nom → vide)
  bool clearProfile(uint8_t profileIndex);

  // Crée un profil prédéfini (helper)
  bool createDefaultProfile(uint8_t profileIndex,
                             const char* name,
                             HeatingCmd  dayCmd,
                             HeatingCmd  nightCmd,
                             uint8_t     nightStartSlot,  // ex: 44 = 22h00
                             uint8_t     nightEndSlot);   // ex: 12 = 06h00

  // ---------------------------------------------------------------------------
  // Associations zone/profil
  // ---------------------------------------------------------------------------

  // Associe un profil à une zone pour un jour donné (0=Lun … 6=Dim)
  bool setZoneDayProfile(uint8_t zoneId,
                         uint8_t dayOfWeek,
                         uint8_t profileIndex);

  // Récupère l'index de profil associé à une zone/jour
  uint8_t getZoneDayProfile(uint8_t zoneId, uint8_t dayOfWeek) const;

  // Associe le même profil à toute la semaine pour une zone
  bool setZoneWeekProfile(uint8_t zoneId, uint8_t profileIndex);

  // Associe profil semaine + profil weekend pour une zone
  bool setZoneWeekendProfile(uint8_t zoneId,
                              uint8_t weekProfileIndex,
                              uint8_t weekendProfileIndex);

  // ---------------------------------------------------------------------------
  // Persistance
  // ---------------------------------------------------------------------------

  void saveAll();
  void loadAll();

  // ---------------------------------------------------------------------------
  // Application manuelle (force recalcul hors tick horloge)
  // ---------------------------------------------------------------------------

  void applyNow();

  // ---------------------------------------------------------------------------
  // Accesseurs état courant
  // ---------------------------------------------------------------------------

  uint8_t  getCurrentSlot()   const { return _currentSlot; }
  uint8_t  getCurrentHour()   const { return _currentHh;   }
  uint8_t  getCurrentMinute() const { return _currentMm;   }

  // Retourne la commande prévue pour une zone à un slot donné
  HeatingCmd getScheduledCmd(uint8_t zoneId,
                              uint8_t dayOfWeek,
                              uint8_t slot) const;

  // Retourne la commande active pour une zone à l'heure courante
  HeatingCmd getCurrentCmd(uint8_t zoneId, uint8_t dayOfWeek) const;

  // ---------------------------------------------------------------------------
  // Sérialisation JSON (pour WebUI)
  // ---------------------------------------------------------------------------

  // Génère le JSON d'un profil (pour l'éditeur web)
  void profileToJson(uint8_t profileIndex, char* buf, size_t bufLen) const;

  // Parse un profil depuis JSON (depuis l'UI web)
  bool profileFromJson(uint8_t profileIndex, const char* json);

  // Génère le JSON des associations zone/profil
  void scheduleToJson(char* buf, size_t bufLen) const;

  // Parse les associations depuis JSON
  bool scheduleFromJson(const char* json);

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dumpProfile(uint8_t profileIndex) const;
  void dumpZoneSchedule(uint8_t zoneId) const;
  void dumpAll() const;

private:

  // ---------------------------------------------------------------------------
  // Données
  // ---------------------------------------------------------------------------

  ScheduleProfile _profiles[MAX_PROFILES]    = {};
  ZoneSchedule    _schedules[NUM_ZONES]       = {};

  bool            _enabled                    = false;
  uint8_t         _currentSlot               = 0;
  uint8_t         _currentHh                 = 0;
  uint8_t         _currentMm                 = 0;
  uint8_t         _lastAppliedSlot           = 255;  // Forcer 1er apply
  uint8_t         _lastAppliedDow            = 255;

  // ---------------------------------------------------------------------------
  // Dépendances
  // ---------------------------------------------------------------------------

  CommandHandler* _cmdHandler  = nullptr;
  StorageManager* _storageMgr  = nullptr;
  Publisher*      _publisher   = nullptr;

  // ---------------------------------------------------------------------------
  // Logique interne
  // ---------------------------------------------------------------------------

  // Calcule le slot depuis hh:mm
  static uint8_t  _toSlot(uint8_t hh, uint8_t mm);

  // Calcule le jour de semaine (0=Lun…6=Dim) depuis day/month
  // Simplifié : stocké à chaque onClock via _currentDow
  uint8_t         _currentDow  = 0;

  // Applique le slot courant sur toutes les zones actives
  void _applySlot(uint8_t dow, uint8_t slot);

  // Applique le slot sur une zone
  void _applyZoneSlot(uint8_t zoneId, uint8_t dow, uint8_t slot);

  // Vérifie si le slot a changé depuis le dernier apply
  bool _slotChanged(uint8_t slot, uint8_t dow) const;
};

// Instance globale
extern ScheduleManager scheduleManager;
