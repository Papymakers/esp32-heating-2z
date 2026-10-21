// =============================================================================
// ClockManager.cpp — Heure locale par NTP
// ESP32 Heating Controller v5.1
// Denis Mattera - 2025
// =============================================================================

#include "ClockManager.h"
#include <esp_sntp.h>

ClockManager clockManager;

// Écrits depuis la tâche lwIP (callback SNTP), lus dans loop()
static volatile uint32_t s_syncCount  = 0;
static volatile uint32_t s_lastSyncMs = 0;

void ClockManager::_onSntpSync(struct timeval* tv) {
  (void)tv;
  s_syncCount  = s_syncCount + 1;
  s_lastSyncMs = millis();
}

// =============================================================================
// INITIALISATION
// =============================================================================

void ClockManager::begin(MinuteCallback onMinute) {
  _onMinute = onMinute;
  sntp_set_time_sync_notification_cb(_onSntpSync);
  // configTzTime : positionne TZ, démarre SNTP en mode poll (lwIP gère
  // les nouvelles tentatives et la resynchronisation horaire)
  configTzTime(NTP_TZ, NTP_SERVER1, NTP_SERVER2);
  _started = true;
  LOG(LOG_CLOCK, "NTP started (%s, %s) TZ=%s", NTP_SERVER1, NTP_SERVER2, NTP_TZ);
}

// =============================================================================
// LOOP
// =============================================================================

bool ClockManager::update() {
  if (!_started) return false;

  time_t n = time(nullptr);
  if (n <= NTP_VALID_EPOCH) { _cacheValid = false; return false; }

  bool newSecond = (n != _lastEpoch) || !_cacheValid;
  if (newSecond) {
    _lastEpoch = n;
    localtime_r(&n, &_cache);
    _cacheValid = true;
  }

  // Journalisation des synchronisations (hors callback lwIP)
  uint32_t sc = s_syncCount;
  if (sc != _seenSyncs) {
    _seenSyncs = sc;
    _syncCount = sc;
    char buf[24];
    format(buf, sizeof(buf));
    if (sc == 1) LOG(LOG_CLOCK, "NTP first sync: %s", buf);
    else         LOG_D(LOG_CLOCK, "NTP resync #%lu: %s", (unsigned long)sc, buf);
  }

  if (!newSecond) return false;

  // Un tick par changement de minute (y compris après un saut d'heure :
  // première synchronisation, passage heure d'été / d'hiver)
  if (_cache.tm_min == _lastMinute && _cache.tm_hour == _lastHour) return false;
  _lastMinute = _cache.tm_min;
  _lastHour   = _cache.tm_hour;

  uint8_t dow = (uint8_t)((_cache.tm_wday + 6) % 7);   // 0 = lundi
  if (_onMinute) _onMinute(_cache, dow);
  return true;
}

// =============================================================================
// ACCESSEURS
// =============================================================================

bool ClockManager::isValid() const {
  return _cacheValid;
}

bool ClockManager::now(struct tm& t) const {
  if (!_cacheValid) return false;
  t = _cache;
  return true;
}

uint32_t ClockManager::lastSyncAgeS() const {
  if (s_syncCount == 0) return UINT32_MAX;
  return (millis() - s_lastSyncMs) / 1000UL;
}

uint32_t ClockManager::secondsOfDay() const {
  struct tm t;
  if (!now(t)) return 0;
  return (uint32_t)(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
}

void ClockManager::format(char* buf, size_t len) const {
  struct tm t;
  if (!buf || len == 0) return;
  if (!now(t)) { snprintf(buf, len, "--"); return; }
  snprintf(buf, len, "%02d/%02d/%04d %02d:%02d:%02d",
           t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
           t.tm_hour, t.tm_min, t.tm_sec);
}
