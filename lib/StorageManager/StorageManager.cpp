// =============================================================================
// StorageManager.cpp — Persistance EEPROM I2C 24C32…24C512 + NVS Flash
// ESP32 Heating Controller v5.1
// Denis Mattera - 2025
// =============================================================================

#include "StorageManager.h"

// Instance globale
StorageManager storageManager;

// =============================================================================
// INITIALISATION
// =============================================================================

bool StorageManager::begin() {
  _eepromOk = probe();

  if (!_eepromOk) {
    LOG_E(LOG_STORE, "EEPROM not found at 0x%02X — storage degraded", EEPROM_ADDR);
    return false;
  }

  // Identification facultative : ID fabricant et n° de série n'existent
  // que sur les 24CSxxx. Sur une 24LC512 ou une AT24C512, les adresses
  // 0x7C et 0x58 ne répondent pas : ce n'est pas une erreur.
  _readMfgId();
  if (_readSerial()) _checkSerialChange();
  LOG(LOG_STORE, "EEPROM 0x%02X: %s, >= %lu bytes, mfgId=%s0x%06lX serial=%s",
      EEPROM_ADDR, chipName(), (unsigned long)EEPROM_SIZE,
      _mfgId ? "" : "(none) ", (unsigned long)_mfgId,
      _serialOk ? _serialHex : "-");

  _checkHeader();
  return _eepromOk;
}

// -----------------------------------------------------------------------------
// Identification 24CSxxx
// -----------------------------------------------------------------------------

const char* StorageManager::chipName() const {
  if (_mfgId == EEPROM_MFG_ID_CS512) return "24CS512";
  if (_serialOk)                     return "24CSxxx";
  if (_mfgId != 0)                   return "EEPROM (ID inconnu)";
  return "EEPROM (sans ID)";
}

bool StorageManager::_readSerial() {
  // Lecture aléatoire dans le registre de sécurité : adresse I2C 0x58,
  // mot 0x0800, avec START répété (pas de STOP entre adresse et lecture).
  // Pas de compteur d'erreur ici : l'absence de registre de sécurité est
  // normale sur une 24LC512 / AT24C512.
  Wire.beginTransmission(EEPROM_SEC_ADDR);
  Wire.write((uint8_t)(EE_SEC_SERIAL >> 8));
  Wire.write((uint8_t)(EE_SEC_SERIAL & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((uint8_t)EEPROM_SEC_ADDR, (uint8_t)16) != 16) return false;
  for (uint8_t i = 0; i < 16; i++) _serial[i] = Wire.read();

  // Un registre jamais programmé ne peut pas exister (programmé en usine) :
  // 16 × 0xFF ou 16 × 0x00 = lecture invalide
  bool allFF = true, all00 = true;
  for (uint8_t i = 0; i < 16; i++) {
    if (_serial[i] != 0xFF) allFF = false;
    if (_serial[i] != 0x00) all00 = false;
  }
  if (allFF || all00) return false;

  for (uint8_t i = 0; i < 16; i++) {
    snprintf(_serialHex + 2 * i, 3, "%02X", _serial[i]);
  }
  _serialOk = true;
  return true;
}

bool StorageManager::_readMfgId() {
  // Séquence ID fabricant I2C : START, F8h, adresse matrice, START répété,
  // F9h, 3 octets. Avec Wire : adresse 7 bits 0x7C en écriture puis lecture.
  Wire.beginTransmission(EEPROM_MFG_CODE);
  Wire.write((uint8_t)(EEPROM_ADDR << 1));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)EEPROM_MFG_CODE, (uint8_t)3) != 3) return false;
  uint32_t id = 0;
  for (uint8_t i = 0; i < 3; i++) id = (id << 8) | Wire.read();
  _mfgId = id;
  return true;
}

bool StorageManager::readIdPage(uint8_t* buf, uint8_t len) {
  if (!_eepromOk || !_serialOk || !buf || len == 0) return false;
  if (len > 128) len = 128;
  Wire.beginTransmission(EEPROM_SEC_ADDR);
  Wire.write((uint8_t)(EE_SEC_IDPAGE >> 8));
  Wire.write((uint8_t)(EE_SEC_IDPAGE & 0xFF));
  if (Wire.endTransmission(false) != 0) { _errorCount++; return false; }
  if (Wire.requestFrom((uint8_t)EEPROM_SEC_ADDR, len) != len) {
    _errorCount++;
    return false;
  }
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

void StorageManager::_checkSerialChange() {
  // Le n° de série du module est mémorisé côté ESP32 (NVS). S'il change,
  // le module EEPROM a été remplacé : les données qu'il contient viennent
  // d'ailleurs (ou d'une puce neuve). On le signale sans rien effacer.
  if (!_prefs.begin(NVS_NS_SYSTEM, false)) return;
  uint8_t known[16] = {0};
  size_t  n = _prefs.getBytes("eeSerial", known, sizeof(known));
  if (n != sizeof(known)) {
    _prefs.putBytes("eeSerial", _serial, sizeof(_serial));
    LOG(LOG_STORE, "EEPROM serial registered");
  } else if (memcmp(known, _serial, sizeof(known)) != 0) {
    _serialChanged = true;
    _prefs.putBytes("eeSerial", _serial, sizeof(_serial));
    LOG_W(LOG_STORE, "EEPROM module CHANGED (new serial %s)", _serialHex);
  }
  _prefs.end();
}

// -----------------------------------------------------------------------------
// En-tête de format
// -----------------------------------------------------------------------------

bool StorageManager::_writeHeader() {
  uint8_t hdr[16];
  memset(hdr, 0xFF, sizeof(hdr));
  memcpy(hdr, EE_MAGIC, 4);
  hdr[4] = EE_LAYOUT_VERSION;
  return _write(EE_HEADER, hdr, sizeof(hdr));
}

bool StorageManager::_checkHeader() {
  uint8_t hdr[16];
  if (!_read(EE_HEADER, hdr, sizeof(hdr))) {
    LOG_E(LOG_STORE, "EEPROM header read FAILED — no format attempted");
    return false;
  }

  if (memcmp(hdr, EE_MAGIC, 4) == 0 && hdr[4] == EE_LAYOUT_VERSION) {
    LOG_D(LOG_STORE, "EEPROM header OK (layout v%u)", hdr[4]);
    return true;
  }

  bool virgin = true;
  for (uint8_t b : hdr) if (b != 0xFF) { virgin = false; break; }

  if (memcmp(hdr, EE_MAGIC, 4) == 0) {
    // Même magic, autre version : migration à écrire le jour où le layout
    // évolue. En v1 il n'existe pas de version antérieure.
    LOG_W(LOG_STORE, "EEPROM layout v%u != v%u — reformat", hdr[4], EE_LAYOUT_VERSION);
  } else {
    LOG_W(LOG_STORE, "EEPROM %s — formatting 0x0000-0x%04X",
          virgin ? "virgin" : "foreign content", EE_USED_END - 1);
  }

  if (!_fill(0, EE_USED_END, 0xFF) || !_writeHeader()) {
    LOG_E(LOG_STORE, "EEPROM format FAILED");
    return false;
  }
  _formatted = true;
  return true;
}

bool StorageManager::_fill(uint16_t from, uint16_t to, uint8_t val) {
  uint8_t blank[EE_WRITE_CHUNK];
  memset(blank, val, sizeof(blank));
  for (uint32_t addr = from; addr < to; addr += EE_WRITE_CHUNK) {
    uint16_t len = (to - addr) < EE_WRITE_CHUNK ? (to - addr) : EE_WRITE_CHUNK;
    if (!_write((uint16_t)addr, blank, len)) return false;
  }
  return true;
}

bool StorageManager::probe() {
  Wire.beginTransmission(EEPROM_ADDR);
  uint8_t err = Wire.endTransmission();
  return (err == 0);
}

bool StorageManager::recheck() {
  bool ok = probe();
  if (ok != _eepromOk) {
    LOG_E(LOG_STORE, "EEPROM %s", ok ? "back online" : "LOST");
    _eepromOk = ok;
  }
  return ok;
}

bool StorageManager::_waitReady() const {
  // Pendant son cycle d'écriture, l'EEPROM ne répond pas (NACK).
  // 5 ms sur 24LC32A / 24CS512, jusqu'à 20 ms sur d'anciennes AT24C32 :
  // on attend au plus ~20 ms (sortie dès que la puce répond).
  for (uint8_t i = 0; i < 40; i++) {
    Wire.beginTransmission(EEPROM_ADDR);
    if (Wire.endTransmission() == 0) return true;
    delayMicroseconds(500);
  }
  return false;
}

// =============================================================================
// EEPROM — COMMANDES ZONES
// =============================================================================

bool StorageManager::saveZoneCmd(uint8_t zoneId, const char* cmdStr) {
  if (!_eepromOk) {
    LOG_W(LOG_STORE, "Z%d saveZoneCmd skipped — EEPROM absent", zoneId);
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
    LOG_D(LOG_STORE, "Z%d saved cmd='%s' @ 0x%03X", zoneId, cmdStr, addr);
  } else {
    LOG_E(LOG_STORE, "Z%d save FAILED @ 0x%03X", zoneId, addr);
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
    LOG_E(LOG_STORE, "Z%d load FAILED @ 0x%03X", zoneId, addr);
    return false;
  }

  raw[EE_ZONE_SIZE - 1] = '\0';  // Sécurité null-termination

  // Vérifie que le contenu est une commande valide
  if (!_isValidCmdStr((char*)raw)) {
    LOG_W(LOG_STORE, "Z%d invalid EEPROM content '%s'", zoneId, raw);
    return false;
  }

  size_t len = strlen((char*)raw);
  if (len >= bufLen) len = bufLen - 1;
  memcpy(buf, raw, len);
  buf[len] = '\0';

  LOG_D(LOG_STORE, "Z%d loaded cmd='%s' @ 0x%03X", zoneId, buf, addr);
  return true;
}

uint8_t StorageManager::loadAllZoneCmds(char cmds[NUM_ZONES][10]) {
  uint8_t count = 0;
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    if (loadZoneCmd(z, cmds[z - 1], 10)) count++;
  }
  LOG_D(LOG_STORE, "Loaded %d/%d zone commands", count, NUM_ZONES);
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
  LOG_D(LOG_STORE, "SysFlags saved: 0x%02X (tempo=%d sched=%d ovld=%d)",
      flags, tempoEnabled, scheduleEnabled, overloadEnabled);
  return ok;
}

bool StorageManager::saveScheduleFlag(bool enabled) {
  if (!_eepromOk) return false;

  uint8_t flags = 0xFF;
  if (!_readByte(EE_SYS_FLAGS, flags)) return false;
  // Octet vierge : valeurs neutres (Tempo off, délestage on)
  if (flags == 0xFF) flags = EE_FLAG_OVERLOAD;

  uint8_t updated = enabled ? (uint8_t)(flags |  EE_FLAG_SCHEDULE)
                            : (uint8_t)(flags & ~EE_FLAG_SCHEDULE);
  if (updated == flags) return true;          // déjà à jour : pas d'écriture

  bool ok = _writeByte(EE_SYS_FLAGS, updated);
  LOG_D(LOG_STORE, "Schedule flag saved: %d (flags 0x%02X)", enabled, updated);
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
    LOG_D(LOG_STORE, "SysFlags: EEPROM virgin → defaults");
    return true;
  }

  tempoEnabled    = (flags & EE_FLAG_TEMPO)    != 0;
  scheduleEnabled = (flags & EE_FLAG_SCHEDULE) != 0;
  overloadEnabled = (flags & EE_FLAG_OVERLOAD) != 0;

  LOG_D(LOG_STORE, "SysFlags loaded: 0x%02X (tempo=%d sched=%d ovld=%d)",
      flags, tempoEnabled, scheduleEnabled, overloadEnabled);
  return true;
}

// =============================================================================
// EEPROM — PROFILS CALENDRIER
// =============================================================================

bool StorageManager::saveProfile(uint8_t               profileIndex,
                                  const ScheduleProfile& profile) {
  if (!hasCalendarStorage()) return false;   // 24C02 : pas de calendrier
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

  LOG_D(LOG_STORE, "Profile[%d] '%s' saved @ 0x%03X",
      profileIndex, profile.name, _profileAddr(profileIndex));
  return true;
}

bool StorageManager::loadProfile(uint8_t          profileIndex,
                                  ScheduleProfile& profile) {
  if (!hasCalendarStorage()) return false;   // 24C02 : pas de calendrier
  if (!_eepromOk || profileIndex >= MAX_PROFILES) return false;

  uint16_t addr = _profileAddr(profileIndex);

  // Lecture du nom dans un tampon local : le profil passé en paramètre
  // (nom par défaut « ProfileN » posé par ScheduleManager) n'est modifié
  // que si l'emplacement contient un profil valide.
  // (v5.1.1 et antérieures : le nom était lu directement dans le profil ;
  //  sur une EEPROM formatée, il valait alors 0xFF…0xFF, que la WebUI
  //  affichait comme une suite de caractères parasites.)
  char name[PROFILE_NAME_LEN];
  if (!_read(addr, (uint8_t*)name, PROFILE_NAME_LEN)) return false;
  name[PROFILE_NAME_LEN - 1] = '\0';
  addr += PROFILE_NAME_LEN;

  // Emplacement vide (0xFF), effacé (0x00) ou contenu étranger : un nom
  // valide ne contient ni caractère de contrôle ni octet 0xFF (absent de
  // tout texte UTF-8 : les accents saisis dans la WebUI restent acceptés)
  if (name[0] == '\0') { profile.active = false; return false; }
  for (uint8_t i = 0; i < PROFILE_NAME_LEN && name[i]; i++) {
    if ((uint8_t)name[i] < 0x20 || (uint8_t)name[i] == 0xFF) {
      profile.active = false;
      return false;
    }
  }
  memcpy(profile.name, name, PROFILE_NAME_LEN);

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

  LOG_D(LOG_STORE, "Profile[%d] '%s' loaded @ 0x%03X",
      profileIndex, profile.name, _profileAddr(profileIndex));
  return true;
}

uint8_t StorageManager::loadAllProfiles(ScheduleProfile profiles[MAX_PROFILES]) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    if (loadProfile(i, profiles[i])) count++;
  }
  LOG_D(LOG_STORE, "Loaded %d/%d profiles", count, MAX_PROFILES);
  return count;
}

// =============================================================================
// EEPROM — ASSOCIATIONS ZONE/JOUR
// =============================================================================

bool StorageManager::saveZoneSchedule(uint8_t            zoneId,
                                       const ZoneSchedule& sched) {
  if (!hasCalendarStorage()) return false;   // 24C02 : pas de calendrier
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
  LOG_D(LOG_STORE, "Z%d schedule saved @ 0x%03X (enabled=%d)",
      zoneId, addr, sched.enabled);
  return ok;
}

bool StorageManager::loadZoneSchedule(uint8_t       zoneId,
                                       ZoneSchedule& sched) {
  if (!hasCalendarStorage()) return false;   // 24C02 : pas de calendrier
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

  LOG_D(LOG_STORE, "Z%d schedule loaded @ 0x%03X (enabled=%d)",
      zoneId, addr, sched.enabled);
  return true;
}

uint8_t StorageManager::loadAllZoneSchedules(ZoneSchedule schedules[NUM_ZONES]) {
  uint8_t count = 0;
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    if (loadZoneSchedule(z, schedules[z - 1])) count++;
  }
  LOG_D(LOG_STORE, "Loaded %d/%d zone schedules", count, NUM_ZONES);
  return count;
}

// =============================================================================
// EEPROM — UTILITAIRES
// =============================================================================

bool StorageManager::eraseAll() {
  if (!_eepromOk) return false;

  // Seule la zone utilisée est effacée : ~80 ms au lieu de ~2,5 s pour
  // 64 Ko (et sans risque de dépasser le Task Watchdog). L'en-tête est
  // réécrit pour que la puce reste reconnue au prochain démarrage.
  LOG_W(LOG_STORE, "Erasing EEPROM 0x0000-0x%04X...", EE_USED_END - 1);
  if (!_fill(0, EE_USED_END, 0xFF) || !_writeHeader()) {
    LOG_E(LOG_STORE, "Erase FAILED");
    return false;
  }
  LOG(LOG_STORE, "EEPROM erased.");
  return true;
}

void StorageManager::dumpHex(uint16_t addr, uint16_t len) const {
  if (!LOG_RAW_ENABLED()) return;   // dump série = niveau DEBUG
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
    LOG_E(LOG_STORE, "NVS open '%s' FAILED", NVS_NS_TEMPO);
    return false;
  }

  _prefs.putBool("enabled",     enabled);
  _prefs.putBool("whiteHP",     forceHG_WhiteHP);
  _prefs.putBool("redHP",       forceHG_RedHP);
  _prefs.putBool("redHC",       forceHG_RedHC);
  _prefs.end();

  LOG_D(LOG_STORE, "Tempo config saved (enabled=%d whiteHP=%d redHP=%d redHC=%d)",
      enabled, forceHG_WhiteHP, forceHG_RedHP, forceHG_RedHC);
  return true;
}

bool StorageManager::loadTempoConfig(bool& enabled,
                                      bool& forceHG_WhiteHP,
                                      bool& forceHG_RedHP,
                                      bool& forceHG_RedHC) {
  if (!_prefs.begin(NVS_NS_TEMPO, true)) {
    LOG_E(LOG_STORE, "NVS open '%s' FAILED — using defaults", NVS_NS_TEMPO);
    enabled = false; forceHG_WhiteHP = true;
    forceHG_RedHP = true; forceHG_RedHC = false;
    return false;
  }

  enabled         = _prefs.getBool("enabled",  false);
  forceHG_WhiteHP = _prefs.getBool("whiteHP",  true);
  forceHG_RedHP   = _prefs.getBool("redHP",    true);
  forceHG_RedHC   = _prefs.getBool("redHC",    false);
  _prefs.end();

  LOG_D(LOG_STORE, "Tempo config loaded (enabled=%d whiteHP=%d redHP=%d redHC=%d)",
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
    LOG_D(LOG_STORE, "Tempo counters saved (red=%d white=%d)", red, white);
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
    LOG_W(LOG_STORE, "Tempo counters invalid — reset (r=%d w=%d)", r, w);
    return false;
  }

  red   = r;
  white = w;
  LOG_D(LOG_STORE, "Tempo counters loaded (red=%d white=%d)", red, white);
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
    LOG_E(LOG_STORE, "NVS open '%s' FAILED", NVS_NS_OVERLOAD);
    return false;
  }

  _prefs.putBool("enabled",    enabled);
  _prefs.putULong("threshold", thresholdMs);
  _prefs.putULong("restore",   restoreDelayMs);
  _prefs.putUChar("fallback",  (uint8_t)fallbackCmd);
  _prefs.end();

  LOG_D(LOG_STORE, "Overload config saved (en=%d thr=%lu res=%lu fb=%s)",
      enabled, thresholdMs, restoreDelayMs, heatingCmdToStr(fallbackCmd));
  return true;
}

bool StorageManager::loadOverloadConfig(bool&          enabled,
                                         unsigned long& thresholdMs,
                                         unsigned long& restoreDelayMs,
                                         HeatingCmd&    fallbackCmd) {
  if (!_prefs.begin(NVS_NS_OVERLOAD, true)) {
    LOG_E(LOG_STORE, "NVS open '%s' FAILED — using defaults", NVS_NS_OVERLOAD);
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

  LOG_D(LOG_STORE, "Overload config loaded (en=%d thr=%lu res=%lu fb=%s)",
      enabled, thresholdMs, restoreDelayMs, heatingCmdToStr(fallbackCmd));
  return true;
}

// =============================================================================
// NVS FLASH — UTILITAIRES
// =============================================================================

bool StorageManager::saveLogConfig() {
  if (!_prefs.begin(NVS_NS_SYSTEM, false)) return false;
  const LogState& ls = logState();
  _prefs.putBool ("logSer",  ls.serial);
  _prefs.putBool ("logMqtt", ls.mqtt);
  _prefs.putUChar("logSLvl", (uint8_t)ls.serialLevel);
  _prefs.putUChar("logMLvl", (uint8_t)ls.mqttLevel);
  _prefs.end();
  return true;
}

bool StorageManager::loadLogConfig() {
  if (!_prefs.begin(NVS_NS_SYSTEM, true)) return false;
  bool present = _prefs.isKey("logSer");
  if (present) {
    LogState& ls = logState();
    ls.serial      = _prefs.getBool("logSer",  ls.serial);
    ls.mqtt        = _prefs.getBool("logMqtt", ls.mqtt);
    uint8_t sl     = _prefs.getUChar("logSLvl", (uint8_t)ls.serialLevel);
    uint8_t ml     = _prefs.getUChar("logMLvl", (uint8_t)ls.mqttLevel);
    if (sl <= (uint8_t)LogLevel::DEBUG) ls.serialLevel = (LogLevel)sl;
    if (ml <= (uint8_t)LogLevel::DEBUG) ls.mqttLevel   = (LogLevel)ml;
  }
  _prefs.end();
  return present;
}

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
  // Découpage : jamais à cheval sur une page physique (sinon rebouclage
  // dans la page) et jamais plus que le buffer Wire ne peut contenir.
  uint16_t written = 0;

  while (written < len) {
    uint16_t a          = addr + written;
    uint16_t pageRemain = EEPROM_PAGE - (a % EEPROM_PAGE);
    uint16_t chunk      = len - written;
    if (chunk > pageRemain)     chunk = pageRemain;
    if (chunk > EE_WRITE_CHUNK) chunk = EE_WRITE_CHUNK;

    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(a >> 8));
    Wire.write((uint8_t)(a & 0xFF));
    Wire.write(data + written, chunk);
    if (Wire.endTransmission(true) != 0) { _errorCount++; return false; }

    if (!_waitReady()) { _errorCount++; return false; }
    written += chunk;
  }
  return true;
}

bool StorageManager::_read(uint16_t addr,
                             uint8_t* data,
                             uint16_t len) const {
  // Lecture aléatoire (START répété) puis lecture séquentielle par blocs
  Wire.beginTransmission(EEPROM_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission(false) != 0) { _errorCount++; return false; }

  uint16_t received = 0;
  while (received < len) {
    uint16_t chunk = (len - received) > 64 ? 64 : (len - received);
    uint8_t  got   = Wire.requestFrom((uint8_t)EEPROM_ADDR,
                                      (uint8_t)chunk,
                                      (uint8_t)true);
    if (got != chunk) { _errorCount++; return false; }
    for (uint8_t i = 0; i < got; i++) {
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
  if (!LOG_RAW_ENABLED()) return;   // dump série = niveau DEBUG
  Serial.printf(
    "%s === StorageManager ===\n"
    "%s   EEPROM       : %s (addr=0x%02X, size=%lu, calendar=%d)\n"
    "%s   Chip         : mfgId=0x%06lX serial=%s changed=%d formatted=%d\n"
    "%s   Layout:\n"
    "%s     Zones      : 0x%03X–0x%03X (%u bytes)\n"
    "%s     SysFlags   : 0x%03X\n"
    "%s     Profiles   : 0x%03X–0x%03X (%u bytes)\n"
    "%s     Schedules  : 0x%03X–0x%03X (%u bytes)\n"
    "%s ======================\n",
    LOG_STORE,
    LOG_STORE, _eepromOk ? "OK" : "ABSENT", EEPROM_ADDR,
               (unsigned long)EEPROM_SIZE, hasCalendarStorage(),
    LOG_STORE, (unsigned long)_mfgId, _serialOk ? _serialHex : "-",
               _serialChanged, _formatted,
    LOG_STORE,
    LOG_STORE, EE_ZONE_BASE, EE_ZONE_BASE + NUM_ZONES * EE_ZONE_STRIDE - 1,
               NUM_ZONES * EE_ZONE_STRIDE,
    LOG_STORE, EE_SYS_FLAGS,
    LOG_STORE, EE_PROFILES_BASE,
               EE_PROFILES_BASE + MAX_PROFILES * EE_PROFILE_STRIDE - 1,
               MAX_PROFILES * EE_PROFILE_STRIDE,
    LOG_STORE, EE_SCHEDULE_BASE,
               EE_SCHEDULE_BASE + NUM_ZONES * EE_SCHED_STRIDE - 1,
               NUM_ZONES * EE_SCHED_STRIDE,
    LOG_STORE
  );
}