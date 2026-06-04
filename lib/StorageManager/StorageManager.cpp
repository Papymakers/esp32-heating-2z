// =============================================================================
// StorageManager.cpp — Persistance EEPROM 24C32 (I2C) + NVS Flash
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include "StorageManager.h"

// Instance globale
StorageManager storageManager;

// =============================================================================
// INITIALISATION
// =============================================================================

bool StorageManager::begin() {
  // Détection EEPROM 24C32 sur le bus I2C
  _eepromOk = probe();

  if (_eepromOk) {
    LOG(LOG_STORE, "EEPROM 24C32 detected at 0x%02X (4KB)", EEPROM_ADDR);
  } else {
    LOG(LOG_STORE, "EEPROM not found at 0x%02X — storage degraded", EEPROM_ADDR);
  }

  return _eepromOk;
}

bool StorageManager::probe() {
  Wire.beginTransmission(EEPROM_ADDR);
  uint8_t err = Wire.endTransmission();
  return (err == 0);
}

// =============================================================================
// EEPROM — COMMANDES ZONES
// =============================================================================

bool StorageManager::saveZoneCmd(uint8_t zoneId, const char* cmdStr) {
  if (!_eepromOk) {
    LOG(LOG_STORE, "Z%d saveZoneCmd skipped — EEPROM absent", zoneId);
    return false;
  }
  if (zoneId < 1 || zoneId > NUM_ZONES || !cmdStr) return false;

  uint8_t zi = zoneId - 1;
  uint16_t addr = _zoneAddr(zi);

  // Buffer fixe de EE_ZONE_SIZE octets, rempli de 0
  uint8_t buf[EE_ZONE_SIZE] = {0};
  size_t len = strlen(cmdStr);
  if (len >= EE_ZONE_SIZE) len = EE_ZONE_SIZE - 1;
  memcpy(buf, cmdStr, len);

  bool ok = _write(addr, buf, EE_ZONE_SIZE);

  if (ok) {
    LOG(LOG_STORE, "Z%d saved cmd='%s' @ 0x%03X", zoneId, cmdStr, addr);
  } else {
    LOG(LOG_STORE, "Z%d save FAILED @ 0x%03X", zoneId, addr);
  }
  return ok;
}

bool StorageManager::loadZoneCmd(uint8_t zoneId,
                                  char*   buf,
                                  size_t  bufLen) {
  if (!_eepromOk || zoneId < 1 || zoneId > NUM_ZONES || !buf) return false;

  uint8_t  zi   = zoneId - 1;
  uint16_t addr = _zoneAddr(zi);

  uint8_t raw[EE_ZONE_SIZE] = {0};
  if (!_read(addr, raw, EE_ZONE_SIZE)) {
    LOG(LOG_STORE, "Z%d load FAILED @ 0x%03X", zoneId, addr);
    return false;
  }

  raw[EE_ZONE_SIZE - 1] = '\0';  // Sécurité null-termination

  // Vérifie que le contenu est une commande valide
  if (!_isValidCmdStr((char*)raw)) {
    LOG(LOG_STORE, "Z%d invalid EEPROM content '%s'", zoneId, raw);
    return false;
  }

  size_t len = strlen((char*)raw);
  if (len >= bufLen) len = bufLen - 1;
  memcpy(buf, raw, len);
  buf[len] = '\0';

  LOG(LOG_STORE, "Z%d loaded cmd='%s' @ 0x%03X", zoneId, buf, addr);
  return true;
}

uint8_t StorageManager::loadAllZoneCmds(char cmds[NUM_ZONES][10]) {
  uint8_t count = 0;
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    if (loadZoneCmd(z, cmds[z - 1], 10)) count++;
  }
  LOG(LOG_STORE, "Loaded %d/%d zone commands", count, NUM_ZONES);
  return count;
}

// =============================================================================
// EEPROM — FLAGS SYSTÈME
// =============================================================================

bool StorageManager::saveSysFlags(bool tempoEnabled,
                                   bool scheduleEnabled,
                                   bool overloadEnabled) {
  if (!_eepromOk) return false;

  uint8_t flags = 0;
  if (tempoEnabled)    flags |= EE_FLAG_TEMPO;
  if (scheduleEnabled) flags |= EE_FLAG_SCHEDULE;
  if (overloadEnabled) flags |= EE_FLAG_OVERLOAD;

  bool ok = _writeByte(EE_SYS_FLAGS, flags);
  LOG(LOG_STORE, "SysFlags saved: 0x%02X (tempo=%d sched=%d ovld=%d)",
      flags, tempoEnabled, scheduleEnabled, overloadEnabled);
  return ok;
}

bool StorageManager::loadSysFlags(bool& tempoEnabled,
                                   bool& scheduleEnabled,
                                   bool& overloadEnabled) {
  if (!_eepromOk) return false;

  uint8_t flags = 0;
  if (!_readByte(EE_SYS_FLAGS, flags)) return false;

  // Valeur 0xFF = EEPROM vierge → valeurs par défaut
  if (flags == 0xFF) {
    tempoEnabled    = false;
    scheduleEnabled = false;
    overloadEnabled = true;
    LOG(LOG_STORE, "SysFlags: EEPROM virgin → defaults");
    return true;
  }

  tempoEnabled    = (flags & EE_FLAG_TEMPO)    != 0;
  scheduleEnabled = (flags & EE_FLAG_SCHEDULE) != 0;
  overloadEnabled = (flags & EE_FLAG_OVERLOAD) != 0;

  LOG(LOG_STORE, "SysFlags loaded: 0x%02X (tempo=%d sched=%d ovld=%d)",
      flags, tempoEnabled, scheduleEnabled, overloadEnabled);
  return true;
}

// =============================================================================
// EEPROM — PROFILS CALENDRIER
// =============================================================================

bool StorageManager::saveProfile(uint8_t               profileIndex,
                                  const ScheduleProfile& profile) {
  if (!_eepromOk || profileIndex >= MAX_PROFILES) return false;

  uint16_t addr = _profileAddr(profileIndex);

  // Écriture nom (PROFILE_NAME_LEN octets)
  if (!_write(addr,
              (const uint8_t*)profile.name,
              PROFILE_NAME_LEN)) return false;
  addr += PROFILE_NAME_LEN;

  // Écriture slots (SLOTS_PER_DAY octets — 1 octet par slot = HeatingCmd)
  uint8_t slotsBuf[SLOTS_PER_DAY];
  for (uint8_t i = 0; i < SLOTS_PER_DAY; i++) {
    slotsBuf[i] = (uint8_t)profile.slots[i];
  }
  if (!_write(addr, slotsBuf, SLOTS_PER_DAY)) return false;

  LOG(LOG_STORE, "Profile[%d] '%s' saved @ 0x%03X",
      profileIndex, profile.name, _profileAddr(profileIndex));
  return true;
}

bool StorageManager::loadProfile(uint8_t          profileIndex,
                                  ScheduleProfile& profile) {
  if (!_eepromOk || profileIndex >= MAX_PROFILES) return false;

  uint16_t addr = _profileAddr(profileIndex);

  // Lecture nom
  if (!_read(addr, (uint8_t*)profile.name, PROFILE_NAME_LEN)) return false;
  profile.name[PROFILE_NAME_LEN - 1] = '\0';
  addr += PROFILE_NAME_LEN;

  // EEPROM vierge → profil invalide
  if (profile.name[0] == '\xFF' || profile.name[0] == '\0') {
    profile.active = false;
    return false;
  }

  // Lecture slots
  uint8_t slotsBuf[SLOTS_PER_DAY] = {0};
  if (!_read(addr, slotsBuf, SLOTS_PER_DAY)) return false;

  for (uint8_t i = 0; i < SLOTS_PER_DAY; i++) {
    HeatingCmd cmd = (HeatingCmd)slotsBuf[i];
    // Validation : commande inconnue → ECO par défaut
    if (cmd > HeatingCmd::CM2) cmd = HeatingCmd::ECO;
    profile.slots[i] = cmd;
  }

  profile.active = true;

  LOG(LOG_STORE, "Profile[%d] '%s' loaded @ 0x%03X",
      profileIndex, profile.name, _profileAddr(profileIndex));
  return true;
}

uint8_t StorageManager::loadAllProfiles(ScheduleProfile profiles[MAX_PROFILES]) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    if (loadProfile(i, profiles[i])) count++;
  }
  LOG(LOG_STORE, "Loaded %d/%d profiles", count, MAX_PROFILES);
  return count;
}

// =============================================================================
// EEPROM — ASSOCIATIONS ZONE/JOUR
// =============================================================================

bool StorageManager::saveZoneSchedule(uint8_t            zoneId,
                                       const ZoneSchedule& sched) {
  if (!_eepromOk || zoneId < 1 || zoneId > NUM_ZONES) return false;

  uint8_t  zi   = zoneId - 1;
  uint16_t addr = _schedAddr(zi);

  // 7 octets profil index + 1 octet flag enabled
  uint8_t buf[EE_ZONE_SCHED_SIZE] = {0};
  for (uint8_t d = 0; d < 7; d++) {
    buf[d] = sched.profileIndex[d];
  }
  buf[7] = sched.enabled ? 0x01 : 0x00;

  bool ok = _write(addr, buf, EE_ZONE_SCHED_SIZE);
  LOG(LOG_STORE, "Z%d schedule saved @ 0x%03X (enabled=%d)",
      zoneId, addr, sched.enabled);
  return ok;
}

bool StorageManager::loadZoneSchedule(uint8_t       zoneId,
                                       ZoneSchedule& sched) {
  if (!_eepromOk || zoneId < 1 || zoneId > NUM_ZONES) return false;

  uint8_t  zi   = zoneId - 1;
  uint16_t addr = _schedAddr(zi);

  uint8_t buf[EE_ZONE_SCHED_SIZE] = {0};
  if (!_read(addr, buf, EE_ZONE_SCHED_SIZE)) return false;

  // EEPROM vierge
  if (buf[0] == 0xFF) {
    sched.enabled = false;
    for (uint8_t d = 0; d < 7; d++) sched.profileIndex[d] = 0;
    return false;
  }

  for (uint8_t d = 0; d < 7; d++) {
    sched.profileIndex[d] = buf[d] < MAX_PROFILES ? buf[d] : 0;
  }
  sched.enabled = (buf[7] == 0x01);

  LOG(LOG_STORE, "Z%d schedule loaded @ 0x%03X (enabled=%d)",
      zoneId, addr, sched.enabled);
  return true;
}

uint8_t StorageManager::loadAllZoneSchedules(ZoneSchedule schedules[NUM_ZONES]) {
  uint8_t count = 0;
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    if (loadZoneSchedule(z, schedules[z - 1])) count++;
  }
  LOG(LOG_STORE, "Loaded %d/%d zone schedules", count, NUM_ZONES);
  return count;
}

// =============================================================================
// EEPROM — UTILITAIRES
// =============================================================================

bool StorageManager::eraseAll() {
  if (!_eepromOk) return false;

  LOG(LOG_STORE, "Erasing EEPROM (%u bytes)...", EEPROM_SIZE);

  // Écriture par pages de 32 octets (limite 24C32 page write)
  constexpr uint8_t PAGE_SIZE = EEPROM_ADDR_16 ? 32 : 8;
  uint8_t blank[PAGE_SIZE];
  memset(blank, 0xFF, PAGE_SIZE);

  for (uint16_t addr = 0; addr < EEPROM_SIZE; addr += PAGE_SIZE) {
    uint16_t chunk = ((addr + PAGE_SIZE) <= EEPROM_SIZE)
                     ? PAGE_SIZE
                     : (EEPROM_SIZE - addr);
    if (!_write(addr, blank, chunk)) {
      LOG(LOG_STORE, "Erase FAILED @ 0x%03X", addr);
      return false;
    }
    delay(6);  // 24C32 write cycle : max 5ms
  }

  LOG(LOG_STORE, "EEPROM erased.");
  return true;
}

void StorageManager::dumpHex(uint16_t addr, uint16_t len) const {
  if (!_eepromOk) {
    Serial.printf("%s EEPROM not present\n", LOG_STORE);
    return;
  }

  Serial.printf("%s EEPROM dump 0x%03X–0x%03X:\n", LOG_STORE, addr, addr + len - 1);

  for (uint16_t i = 0; i < len; i += 16) {
    Serial.printf("  %03X: ", addr + i);
    for (uint16_t j = 0; j < 16 && (i + j) < len; j++) {
      uint8_t b = 0;
      _readByte(addr + i + j, const_cast<uint8_t&>(b));
      Serial.printf("%02X ", b);
    }
    Serial.println();
  }
}

// =============================================================================
// NVS FLASH — CONFIG TEMPO
// =============================================================================

bool StorageManager::saveTempoConfig(bool enabled,
                                      bool forceHG_WhiteHP,
                                      bool forceHG_RedHP,
                                      bool forceHG_RedHC) {
  if (!_prefs.begin(NVS_NS_TEMPO, false)) {
    LOG(LOG_STORE, "NVS open '%s' FAILED", NVS_NS_TEMPO);
    return false;
  }

  _prefs.putBool("enabled",     enabled);
  _prefs.putBool("whiteHP",     forceHG_WhiteHP);
  _prefs.putBool("redHP",       forceHG_RedHP);
  _prefs.putBool("redHC",       forceHG_RedHC);
  _prefs.end();

  LOG(LOG_STORE, "Tempo config saved (enabled=%d whiteHP=%d redHP=%d redHC=%d)",
      enabled, forceHG_WhiteHP, forceHG_RedHP, forceHG_RedHC);
  return true;
}

bool StorageManager::loadTempoConfig(bool& enabled,
                                      bool& forceHG_WhiteHP,
                                      bool& forceHG_RedHP,
                                      bool& forceHG_RedHC) {
  if (!_prefs.begin(NVS_NS_TEMPO, true)) {
    LOG(LOG_STORE, "NVS open '%s' FAILED — using defaults", NVS_NS_TEMPO);
    enabled = false; forceHG_WhiteHP = true;
    forceHG_RedHP = true; forceHG_RedHC = false;
    return false;
  }

  enabled         = _prefs.getBool("enabled",  false);
  forceHG_WhiteHP = _prefs.getBool("whiteHP",  true);
  forceHG_RedHP   = _prefs.getBool("redHP",    true);
  forceHG_RedHC   = _prefs.getBool("redHC",    false);
  _prefs.end();

  LOG(LOG_STORE, "Tempo config loaded (enabled=%d whiteHP=%d redHP=%d redHC=%d)",
      enabled, forceHG_WhiteHP, forceHG_RedHP, forceHG_RedHC);
  return true;
}

// =============================================================================
// EEPROM — COMPTEURS JOURS TEMPO
// =============================================================================

bool StorageManager::saveTempoCounters(uint8_t red, uint8_t white) {
  if (!_eepromOk) return false;
  bool ok = _writeByte(EE_TEMPO_RED,   red) &&
            _writeByte(EE_TEMPO_WHITE, white);
  if (ok) {
    LOG(LOG_STORE, "Tempo counters saved (red=%d white=%d)", red, white);
  }
  return ok;
}

bool StorageManager::loadTempoCounters(uint8_t& red, uint8_t& white) {
  if (!_eepromOk) { red = 0; white = 0; return false; }

  uint8_t r = 0, w = 0;
  bool ok = _readByte(EE_TEMPO_RED,   r) &&
            _readByte(EE_TEMPO_WHITE, w);

  if (!ok || r > 22 || w > 43) {
    // Valeurs invalides — reset
    red = 0; white = 0;
    LOG(LOG_STORE, "Tempo counters invalid — reset (r=%d w=%d)", r, w);
    return false;
  }

  red   = r;
  white = w;
  LOG(LOG_STORE, "Tempo counters loaded (red=%d white=%d)", red, white);
  return true;
}

// =============================================================================
// NVS FLASH — CONFIG OVERLOAD
// =============================================================================

bool StorageManager::saveOverloadConfig(bool          enabled,
                                         unsigned long thresholdMs,
                                         unsigned long restoreDelayMs,
                                         HeatingCmd    fallbackCmd) {
  if (!_prefs.begin(NVS_NS_OVERLOAD, false)) {
    LOG(LOG_STORE, "NVS open '%s' FAILED", NVS_NS_OVERLOAD);
    return false;
  }

  _prefs.putBool("enabled",    enabled);
  _prefs.putULong("threshold", thresholdMs);
  _prefs.putULong("restore",   restoreDelayMs);
  _prefs.putUChar("fallback",  (uint8_t)fallbackCmd);
  _prefs.end();

  LOG(LOG_STORE, "Overload config saved (en=%d thr=%lu res=%lu fb=%s)",
      enabled, thresholdMs, restoreDelayMs, heatingCmdToStr(fallbackCmd));
  return true;
}

bool StorageManager::loadOverloadConfig(bool&          enabled,
                                         unsigned long& thresholdMs,
                                         unsigned long& restoreDelayMs,
                                         HeatingCmd&    fallbackCmd) {
  if (!_prefs.begin(NVS_NS_OVERLOAD, true)) {
    LOG(LOG_STORE, "NVS open '%s' FAILED — using defaults", NVS_NS_OVERLOAD);
    enabled       = true;
    thresholdMs   = OVERLOAD_THRESHOLD_MS;
    restoreDelayMs = 30000UL;
    fallbackCmd   = HeatingCmd::ECO;
    return false;
  }

  enabled        = _prefs.getBool("enabled",   true);
  thresholdMs    = _prefs.getULong("threshold", OVERLOAD_THRESHOLD_MS);
  restoreDelayMs = _prefs.getULong("restore",   30000UL);
  fallbackCmd    = (HeatingCmd)_prefs.getUChar("fallback", (uint8_t)HeatingCmd::ECO);
  _prefs.end();

  // Validation fallbackCmd
  if (fallbackCmd > HeatingCmd::CM2) fallbackCmd = HeatingCmd::ECO;

  LOG(LOG_STORE, "Overload config loaded (en=%d thr=%lu res=%lu fb=%s)",
      enabled, thresholdMs, restoreDelayMs, heatingCmdToStr(fallbackCmd));
  return true;
}

// =============================================================================
// NVS FLASH — UTILITAIRES
// =============================================================================

void StorageManager::clearNvs(const char* ns) {
  if (_prefs.begin(ns, false)) {
    _prefs.clear();
    _prefs.end();
    LOG(LOG_STORE, "NVS namespace '%s' cleared", ns);
  }
}

// =============================================================================
// PRIMITIVES EEPROM BAS NIVEAU
// =============================================================================

bool StorageManager::_write(uint16_t       addr,
                              const uint8_t* data,
                              uint16_t       len) {
  constexpr uint8_t PAGE = EEPROM_ADDR_16 ? 32 : 8; // 24C02 = pages 8 octets
  uint16_t written = 0;

  while (written < len) {
    uint16_t pageOffset = (addr + written) % PAGE;
    uint16_t chunkMax   = PAGE - pageOffset;
    uint16_t chunk      = (len - written) < chunkMax
                          ? (len - written) : chunkMax;

    Wire.beginTransmission(EEPROM_ADDR);
#ifdef EEPROM_TYPE_24C32
    Wire.write((uint8_t)((addr + written) >> 8));
#endif
    Wire.write((uint8_t)((addr + written) & 0xFF));
    Wire.write(data + written, chunk);
    if (Wire.endTransmission(true) != 0) return false;

    delay(6);
    written += chunk;
  }
  return true;
}

bool StorageManager::_read(uint16_t addr,
                             uint8_t* data,
                             uint16_t len) const {
  // Transaction 1 : positionne le pointeur interne
  Wire.beginTransmission(EEPROM_ADDR);
#ifdef EEPROM_TYPE_24C32
  Wire.write((uint8_t)(addr >> 8));
#endif
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission(true) != 0) return false;

  delay(1);

  // Transaction 2 : lecture séquentielle
  uint16_t received = 0;
  while (received < len) {
    uint16_t chunk = (len - received) > 32 ? 32 : (len - received);
    uint8_t  got   = Wire.requestFrom((uint8_t)EEPROM_ADDR,
                                      (uint8_t)chunk,
                                      (uint8_t)true);
    if (got == 0) return false;
    for (uint8_t i = 0; i < got && Wire.available(); i++) {
      data[received++] = Wire.read();
    }
  }
  return true;
}

bool StorageManager::_writeByte(uint16_t addr, uint8_t val) {
  return _write(addr, &val, 1);
}

bool StorageManager::_readByte(uint16_t addr, uint8_t& val) const {
  return _read(addr, &val, 1);
}

// =============================================================================
// VALIDATION
// =============================================================================

bool StorageManager::_isValidCmdStr(const char* s) {
  if (!s || s[0] == '\0' || s[0] == '\xFF') return false;

  // Liste des commandes valides
  const char* valid[] = { "STOP", "HG", "ECO", "CONF", "CM2" };
  for (const char* v : valid) {
    if (strcasecmp(s, v) == 0) return true;
  }
  return false;
}

// =============================================================================
// DEBUG
// =============================================================================

void StorageManager::dump() const {
  Serial.printf(
    "%s === StorageManager ===\n"
    "%s   EEPROM 24C32 : %s (addr=0x%02X, size=%u)\n"
    "%s   Layout:\n"
    "%s     Zones      : 0x%03X–0x%03X (%u bytes)\n"
    "%s     SysFlags   : 0x%03X\n"
    "%s     Profiles   : 0x%03X–0x%03X (%u bytes)\n"
    "%s     Schedules  : 0x%03X–0x%03X (%u bytes)\n"
    "%s ======================\n",
    LOG_STORE,
    LOG_STORE, _eepromOk ? "OK" : "ABSENT", EEPROM_ADDR, EEPROM_SIZE,
    LOG_STORE,
    LOG_STORE, EE_ZONE_BASE, EE_ZONE_BASE + NUM_ZONES * EE_ZONE_SIZE - 1,
               NUM_ZONES * EE_ZONE_SIZE,
    LOG_STORE, EE_SYS_FLAGS,
    LOG_STORE, EE_PROFILES_BASE,
               EE_PROFILES_BASE + MAX_PROFILES * EE_PROFILE_SIZE - 1,
               MAX_PROFILES * EE_PROFILE_SIZE,
    LOG_STORE, EE_SCHEDULE_BASE,
               EE_SCHEDULE_BASE + NUM_ZONES * EE_ZONE_SCHED_SIZE - 1,
               NUM_ZONES * EE_ZONE_SCHED_SIZE,
    LOG_STORE
  );
}
