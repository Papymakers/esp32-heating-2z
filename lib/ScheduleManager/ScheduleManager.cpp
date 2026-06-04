// =============================================================================
// ScheduleManager.cpp — Gestion du calendrier de chauffage par profils
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include "ScheduleManager.h"
#include "CommandHandler.h"
#include "StorageManager.h"
#include "Publisher.h"
#include <ArduinoJson.h>

// Instance globale
ScheduleManager scheduleManager;

// =============================================================================
// INITIALISATION
// =============================================================================

void ScheduleManager::begin(CommandHandler* cmdHandler,
                             StorageManager* storageMgr,
                             Publisher*      publisher) {
  _cmdHandler = cmdHandler;
  _storageMgr = storageMgr;
  _publisher  = publisher;

  // Init profils vides
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    _profiles[i] = {};
    _profiles[i].active = false;
    snprintf(_profiles[i].name, PROFILE_NAME_LEN, "Profile%d", i);
    // Tous les slots à ECO par défaut
    for (uint8_t s = 0; s < SLOTS_PER_DAY; s++) {
      _profiles[i].slots[s] = HeatingCmd::ECO;
    }
  }

  // Init associations zones → profil 0 par défaut
  for (uint8_t z = 0; z < NUM_ZONES; z++) {
    _schedules[z].enabled = false;
    for (uint8_t d = 0; d < 7; d++) {
      _schedules[z].profileIndex[d] = 0;
    }
  }

  _enabled         = false;
  _lastAppliedSlot = 255;  // Forcer 1er apply au boot
  _lastAppliedDow  = 255;

  LOG(LOG_SCHED, "ScheduleManager ready (%d profiles, %d zones)",
      MAX_PROFILES, NUM_ZONES);
}

// =============================================================================
// HORLOGE UTC
// =============================================================================

void ScheduleManager::onClock(uint8_t hh, uint8_t mm) {
  _currentHh = hh;
  _currentMm = mm;

  uint8_t slot = _toSlot(hh, mm);
  _currentSlot = slot;

  // Jour de semaine simplifié — mis à jour depuis Publisher::_handleUtcClock
  // via _currentDow (setter à ajouter si nécessaire)

  if (!_enabled) return;

  // Application uniquement si le slot a changé (toutes les 30min)
  if (_slotChanged(slot, _currentDow)) {
    LOG(LOG_SCHED, "Slot change: %02d:%02d → slot %d (dow=%d)",
        hh, mm, slot, _currentDow);
    _applySlot(_currentDow, slot);
    _lastAppliedSlot = slot;
    _lastAppliedDow  = _currentDow;
  }
}

// =============================================================================
// ACTIVATION
// =============================================================================

void ScheduleManager::setEnabled(bool enabled) {
  if (_enabled == enabled) return;
  _enabled = enabled;

  LOG(LOG_SCHED, "Schedule %s", enabled ? "ENABLED" : "DISABLED");

  if (_publisher) {
    _publisher->publishLog("INFO", "SCHED",
      "Schedule %s", enabled ? "enabled" : "disabled");
  }

  if (enabled) {
    // Application immédiate du slot courant
    _lastAppliedSlot = 255;  // Force recalcul
    applyNow();
  }
}

void ScheduleManager::setZoneEnabled(uint8_t zoneId, bool enabled) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;

  _schedules[zi].enabled = enabled;
  LOG(LOG_SCHED, "Z%d schedule %s", zoneId, enabled ? "enabled" : "disabled");

  if (_storageMgr) {
    _storageMgr->saveZoneSchedule(zoneId, _schedules[zi]);
  }
}

bool ScheduleManager::isZoneEnabled(uint8_t zoneId) const {
  if (zoneId < 1 || zoneId > NUM_ZONES) return false;
  return _schedules[zoneId - 1].enabled;
}

// =============================================================================
// GESTION DES PROFILS
// =============================================================================

bool ScheduleManager::setProfile(uint8_t                profileIndex,
                                  const ScheduleProfile& profile) {
  if (profileIndex >= MAX_PROFILES) return false;

  _profiles[profileIndex] = profile;
  _profiles[profileIndex].active = true;

  LOG(LOG_SCHED, "Profile[%d] '%s' updated",
      profileIndex, profile.name);

  if (_storageMgr) {
    _storageMgr->saveProfile(profileIndex, _profiles[profileIndex]);
  }
  return true;
}

const ScheduleProfile& ScheduleManager::getProfile(uint8_t profileIndex) const {
  if (profileIndex >= MAX_PROFILES) return _profiles[0];
  return _profiles[profileIndex];
}

uint8_t ScheduleManager::getProfileCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    if (_profiles[i].active) count++;
  }
  return count;
}

bool ScheduleManager::clearProfile(uint8_t profileIndex) {
  if (profileIndex >= MAX_PROFILES) return false;

  _profiles[profileIndex].active = false;
  _profiles[profileIndex].name[0] = '\0';
  for (uint8_t s = 0; s < SLOTS_PER_DAY; s++) {
    _profiles[profileIndex].slots[s] = HeatingCmd::ECO;
  }

  LOG(LOG_SCHED, "Profile[%d] cleared", profileIndex);

  if (_storageMgr) {
    _storageMgr->saveProfile(profileIndex, _profiles[profileIndex]);
  }
  return true;
}

bool ScheduleManager::createDefaultProfile(uint8_t     profileIndex,
                                            const char* name,
                                            HeatingCmd  dayCmd,
                                            HeatingCmd  nightCmd,
                                            uint8_t     nightStartSlot,
                                            uint8_t     nightEndSlot) {
  if (profileIndex >= MAX_PROFILES) return false;

  ScheduleProfile& p = _profiles[profileIndex];
  strncpy(p.name, name, PROFILE_NAME_LEN - 1);
  p.name[PROFILE_NAME_LEN - 1] = '\0';
  p.active = true;

  // Remplissage selon plage nuit/jour
  // nightStartSlot → nightEndSlot = nuit (peut chevaucher minuit)
  for (uint8_t s = 0; s < SLOTS_PER_DAY; s++) {
    bool isNight;

    if (nightStartSlot <= nightEndSlot) {
      // Plage nuit sans chevauchement minuit (ex: 10h→14h)
      isNight = (s >= nightStartSlot && s < nightEndSlot);
    } else {
      // Plage nuit chevauchant minuit (ex: 22h→6h = slot 44→12)
      isNight = (s >= nightStartSlot || s < nightEndSlot);
    }

    p.slots[s] = isNight ? nightCmd : dayCmd;
  }

  LOG(LOG_SCHED, "Profile[%d] '%s' created (day=%s night=%s slots %d→%d)",
      profileIndex, name,
      heatingCmdToStr(dayCmd), heatingCmdToStr(nightCmd),
      nightStartSlot, nightEndSlot);

  if (_storageMgr) {
    _storageMgr->saveProfile(profileIndex, p);
  }
  return true;
}

// =============================================================================
// ASSOCIATIONS ZONE/PROFIL
// =============================================================================

bool ScheduleManager::setZoneDayProfile(uint8_t zoneId,
                                         uint8_t dayOfWeek,
                                         uint8_t profileIndex) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return false;
  if (dayOfWeek > 6 || profileIndex >= MAX_PROFILES) return false;

  uint8_t zi = zoneId - 1;
  _schedules[zi].profileIndex[dayOfWeek] = profileIndex;

  LOG(LOG_SCHED, "Z%d day=%d → profile[%d] '%s'",
      zoneId, dayOfWeek, profileIndex, _profiles[profileIndex].name);

  if (_storageMgr) {
    _storageMgr->saveZoneSchedule(zoneId, _schedules[zi]);
  }
  return true;
}

uint8_t ScheduleManager::getZoneDayProfile(uint8_t zoneId,
                                             uint8_t dayOfWeek) const {
  if (zoneId < 1 || zoneId > NUM_ZONES || dayOfWeek > 6) return 0;
  return _schedules[zoneId - 1].profileIndex[dayOfWeek];
}

bool ScheduleManager::setZoneWeekProfile(uint8_t zoneId,
                                          uint8_t profileIndex) {
  if (zoneId < 1 || zoneId > NUM_ZONES || profileIndex >= MAX_PROFILES) return false;

  uint8_t zi = zoneId - 1;
  for (uint8_t d = 0; d < 7; d++) {
    _schedules[zi].profileIndex[d] = profileIndex;
  }
  _schedules[zi].enabled = true;

  LOG(LOG_SCHED, "Z%d all week → profile[%d] '%s'",
      zoneId, profileIndex, _profiles[profileIndex].name);

  if (_storageMgr) {
    _storageMgr->saveZoneSchedule(zoneId, _schedules[zi]);
  }
  return true;
}

bool ScheduleManager::setZoneWeekendProfile(uint8_t zoneId,
                                             uint8_t weekProfileIndex,
                                             uint8_t weekendProfileIndex) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return false;
  if (weekProfileIndex >= MAX_PROFILES || weekendProfileIndex >= MAX_PROFILES) return false;

  uint8_t zi = zoneId - 1;

  // Lun(0)→Ven(4) = semaine
  for (uint8_t d = 0; d <= 4; d++) {
    _schedules[zi].profileIndex[d] = weekProfileIndex;
  }
  // Sam(5), Dim(6) = weekend
  _schedules[zi].profileIndex[5] = weekendProfileIndex;
  _schedules[zi].profileIndex[6] = weekendProfileIndex;
  _schedules[zi].enabled = true;

  LOG(LOG_SCHED, "Z%d week→profile[%d] weekend→profile[%d]",
      zoneId, weekProfileIndex, weekendProfileIndex);

  if (_storageMgr) {
    _storageMgr->saveZoneSchedule(zoneId, _schedules[zi]);
  }
  return true;
}

// =============================================================================
// PERSISTANCE
// =============================================================================

void ScheduleManager::saveAll() {
  if (!_storageMgr) return;

  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    _storageMgr->saveProfile(i, _profiles[i]);
  }
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    _storageMgr->saveZoneSchedule(z, _schedules[z - 1]);
  }
  LOG(LOG_SCHED, "All profiles and schedules saved.");
}

void ScheduleManager::loadAll() {
  if (!_storageMgr) return;

  uint8_t profLoaded  = _storageMgr->loadAllProfiles(_profiles);
  uint8_t schedLoaded = _storageMgr->loadAllZoneSchedules(_schedules);

  LOG(LOG_SCHED, "Loaded %d profiles, %d zone schedules",
      profLoaded, schedLoaded);
}

// =============================================================================
// APPLICATION MANUELLE
// =============================================================================

void ScheduleManager::applyNow() {
  if (!_enabled) return;
  _lastAppliedSlot = 255;  // Force recalcul
  _applySlot(_currentDow, _currentSlot);
  _lastAppliedSlot = _currentSlot;
  _lastAppliedDow  = _currentDow;
}

// =============================================================================
// ACCESSEURS COMMANDES
// =============================================================================

HeatingCmd ScheduleManager::getScheduledCmd(uint8_t zoneId,
                                             uint8_t dayOfWeek,
                                             uint8_t slot) const {
  if (zoneId < 1 || zoneId > NUM_ZONES) return HeatingCmd::ECO;
  if (dayOfWeek > 6 || slot >= SLOTS_PER_DAY)  return HeatingCmd::ECO;

  uint8_t zi  = zoneId - 1;
  uint8_t pi  = _schedules[zi].profileIndex[dayOfWeek];
  if (pi >= MAX_PROFILES) return HeatingCmd::ECO;

  return _profiles[pi].slots[slot];
}

HeatingCmd ScheduleManager::getCurrentCmd(uint8_t zoneId,
                                           uint8_t dayOfWeek) const {
  return getScheduledCmd(zoneId, dayOfWeek, _currentSlot);
}

// =============================================================================
// SÉRIALISATION JSON
// =============================================================================

void ScheduleManager::profileToJson(uint8_t profileIndex,
                                     char*   buf,
                                     size_t  bufLen) const {
  if (profileIndex >= MAX_PROFILES || !buf) return;

  const ScheduleProfile& p = _profiles[profileIndex];

  JsonDocument doc;
  doc["index"]  = profileIndex;
  doc["name"]   = p.name;
  doc["active"] = p.active;

  JsonArray slots = doc["slots"].to<JsonArray>();
  for (uint8_t s = 0; s < SLOTS_PER_DAY; s++) {
    slots.add(heatingCmdToStr(p.slots[s]));
  }

  serializeJson(doc, buf, bufLen);
}

bool ScheduleManager::profileFromJson(uint8_t     profileIndex,
                                       const char* json) {
  if (profileIndex >= MAX_PROFILES || !json) return false;

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    LOG(LOG_SCHED, "profileFromJson parse error (profile %d)", profileIndex);
    return false;
  }

  ScheduleProfile& p = _profiles[profileIndex];

  const char* name = doc["name"] | "";
  strncpy(p.name, name, PROFILE_NAME_LEN - 1);
  p.name[PROFILE_NAME_LEN - 1] = '\0';
  p.active = true;

  JsonArray slots = doc["slots"];
  uint8_t   si    = 0;

  for (JsonVariant v : slots) {
    if (si >= SLOTS_PER_DAY) break;
    const char* cmdStr = v.as<const char*>();
    HeatingCmd  cmd    = CommandHandler::parseCmd(cmdStr);
    p.slots[si++] = (cmd == HeatingCmd::UNKNOWN) ? HeatingCmd::ECO : cmd;
  }

  // Compléter les slots manquants avec ECO
  for (; si < SLOTS_PER_DAY; si++) {
    p.slots[si] = HeatingCmd::ECO;
  }

  LOG(LOG_SCHED, "Profile[%d] '%s' loaded from JSON (%d slots)",
      profileIndex, p.name, si);

  if (_storageMgr) {
    _storageMgr->saveProfile(profileIndex, p);
  }
  return true;
}

void ScheduleManager::scheduleToJson(char* buf, size_t bufLen) const {
  JsonDocument doc;

  doc["enabled"]  = _enabled;
  doc["slot"]     = _currentSlot;
  doc["hh"]       = _currentHh;
  doc["mm"]       = _currentMm;
  doc["dow"]      = _currentDow;

  JsonArray zones = doc["zones"].to<JsonArray>();
  for (uint8_t z = 0; z < NUM_ZONES; z++) {
    JsonObject zObj = zones.add<JsonObject>();
    zObj["id"]      = z + 1;
    zObj["enabled"] = _schedules[z].enabled;

    JsonArray days = zObj["days"].to<JsonArray>();
    for (uint8_t d = 0; d < 7; d++) {
      days.add(_schedules[z].profileIndex[d]);
    }
  }

  // Liste des profils disponibles
  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    JsonObject pObj = profiles.add<JsonObject>();
    pObj["index"]  = i;
    pObj["name"]   = _profiles[i].name;
    pObj["active"] = _profiles[i].active;
  }

  serializeJson(doc, buf, bufLen);
}

bool ScheduleManager::scheduleFromJson(const char* json) {
  if (!json) return false;

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    LOG(LOG_SCHED, "scheduleFromJson parse error");
    return false;
  }

  // Activation globale
  if (doc["enabled"].is<bool>()) {
    setEnabled(doc["enabled"].as<bool>());
  }

  // Associations zones
  JsonArray zones = doc["zones"];
  for (JsonObject zObj : zones) {
    uint8_t zoneId  = zObj["id"] | 0;
    bool    zEnabled = zObj["enabled"] | false;

    if (zoneId < 1 || zoneId > NUM_ZONES) continue;

    setZoneEnabled(zoneId, zEnabled);

    JsonArray days = zObj["days"];
    uint8_t   d    = 0;
    for (JsonVariant v : days) {
      if (d >= 7) break;
      uint8_t pi = v.as<uint8_t>();
      if (pi < MAX_PROFILES) {
        setZoneDayProfile(zoneId, d, pi);
      }
      d++;
    }
  }

  LOG(LOG_SCHED, "Schedule loaded from JSON");
  saveAll();
  return true;
}

// =============================================================================
// LOGIQUE INTERNE
// =============================================================================

uint8_t ScheduleManager::_toSlot(uint8_t hh, uint8_t mm) {
  return hh * 2 + (mm >= 30 ? 1 : 0);
}

bool ScheduleManager::_slotChanged(uint8_t slot, uint8_t dow) const {
  return (slot != _lastAppliedSlot) || (dow != _lastAppliedDow);
}

void ScheduleManager::_applySlot(uint8_t dow, uint8_t slot) {
  if (!_enabled) return;

  LOG(LOG_SCHED, "Applying slot %d (dow=%d time=%02d:%02d)",
      slot, dow, _currentHh, _currentMm);

  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    _applyZoneSlot(z, dow, slot);
  }
}

void ScheduleManager::_applyZoneSlot(uint8_t zoneId,
                                      uint8_t dow,
                                      uint8_t slot) {
  if (!_cmdHandler) return;

  uint8_t zi = zoneId - 1;

  // Zone désactivée → on passe
  if (!_schedules[zi].enabled) return;

  // Récupère la commande planifiée
  HeatingCmd cmd = getScheduledCmd(zoneId, dow, slot);

  LOG(LOG_SCHED, "Z%d slot%d → %s (profile[%d])",
      zoneId, slot,
      heatingCmdToStr(cmd),
      _schedules[zi].profileIndex[dow]);

  // Application via CommandHandler (priorité PRIO_SCHEDULE)
  // Le CommandHandler vérifiera si Tempo/Overload/User prend la priorité
  _cmdHandler->handleCmd(zoneId,
                          cmd,
                          CommandOrigin::SYSTEM,
                          CommandSource::SRC_SCHEDULE);
}

// =============================================================================
// DEBUG
// =============================================================================

void ScheduleManager::dumpProfile(uint8_t profileIndex) const {
  if (profileIndex >= MAX_PROFILES) return;
  const ScheduleProfile& p = _profiles[profileIndex];

  Serial.printf("%s Profile[%d] '%s' active=%d\n",
                LOG_SCHED, profileIndex, p.name, p.active);

  // Affichage grille 48 slots sur 6 lignes de 8
  for (uint8_t row = 0; row < 6; row++) {
    Serial.printf("%s   ", LOG_SCHED);
    for (uint8_t col = 0; col < 8; col++) {
      uint8_t s = row * 8 + col;
      uint8_t hh = s / 2;
      uint8_t mm = (s % 2) * 30;
      Serial.printf("%02d:%02d=%-5s ", hh, mm,
                    heatingCmdToStr(p.slots[s]));
    }
    Serial.println();
  }
}

void ScheduleManager::dumpZoneSchedule(uint8_t zoneId) const {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;

  const char* days[] = {"Lun","Mar","Mer","Jeu","Ven","Sam","Dim"};

  Serial.printf("%s Z%d schedule (enabled=%d):\n",
                LOG_SCHED, zoneId, _schedules[zi].enabled);

  for (uint8_t d = 0; d < 7; d++) {
    uint8_t pi = _schedules[zi].profileIndex[d];
    Serial.printf("%s   %s → profile[%d] '%s'\n",
                  LOG_SCHED, days[d], pi, _profiles[pi].name);
  }
}

void ScheduleManager::dumpAll() const {
  Serial.printf("%s === ScheduleManager ===\n", LOG_SCHED);
  Serial.printf("%s   enabled=%d profiles=%d slot=%d dow=%d\n",
                LOG_SCHED, _enabled, getProfileCount(),
                _currentSlot, _currentDow);

  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    if (_profiles[i].active) dumpProfile(i);
  }
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    dumpZoneSchedule(z);
  }
  Serial.printf("%s =======================\n", LOG_SCHED);
}
