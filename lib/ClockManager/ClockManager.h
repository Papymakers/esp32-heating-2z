#pragma once
// =============================================================================
// ClockManager.h — Heure locale par NTP (seule source d'heure depuis v5.1)
// ESP32 Heating Controller v5.1
// Denis Mattera - 2025
//
// - SNTP (lwIP) avec fuseau POSIX Europe/Paris : date, heure légale et jour
//   de semaine, changements été/hiver gérés par la libc.
// - Une fois synchronisée, l'horloge système continue de tourner sur le
//   quartz de l'ESP32 même si Internet disparaît ; lwIP resynchronise
//   toutes les heures. Seul un redémarrage sans Internet laisse l'heure
//   inconnue jusqu'à la première synchronisation.
// - Remplace l'abonnement MQTT utcClock des versions précédentes : les
//   modules horodatés (index Linky 23 h, compteurs Tempo, calendrier)
//   reçoivent un tick par minute générée ici, plus par message MQTT.
// - Tant que l'heure est inconnue, aucun tick n'est émis : calendrier,
//   index journaliers et comptage Tempo attendent la synchronisation. La
//   publication Linky périodique, elle, ne dépend plus de l'heure.
// =============================================================================

#include <Arduino.h>
#include <time.h>
#include "config.h"

class ClockManager {
public:
  // Callback appelé une fois par minute (heure locale valide uniquement)
  //   dow : 0 = lundi … 6 = dimanche
  using MinuteCallback = void (*)(const struct tm& t, uint8_t dow);

  // À appeler après WiFi.begin() (la synchronisation démarre dès que le
  // réseau est disponible, sans bloquer)
  void begin(MinuteCallback onMinute);

  // À appeler à chaque tour de loop() — retourne true si un tick a été émis
  bool update();

  bool     isValid()       const;          // heure plausible (synchro faite)
  bool     everSynced()    const { return _syncCount > 0; }
  uint32_t getSyncCount()  const { return _syncCount; }
  uint32_t lastSyncAgeS()  const;          // UINT32_MAX si jamais synchronisé

  // Heure locale courante (précision 1 s) ; false si l'heure est inconnue
  bool     now(struct tm& t) const;

  // Secondes depuis minuit local (champ "ts" des publications MQTT,
  // même sémantique qu'avec utcClock) ; 0 si l'heure est inconnue
  uint32_t secondsOfDay() const;

  // "JJ/MM/AAAA HH:MM:SS" ou "--" (diagnostic, /api/state)
  void     format(char* buf, size_t len) const;

private:
  MinuteCallback _onMinute     = nullptr;
  int            _lastMinute   = -1;
  int            _lastHour     = -1;
  uint32_t       _syncCount    = 0;
  uint32_t       _seenSyncs    = 0;
  bool           _started      = false;

  // Heure locale mise en cache une fois par seconde (localtime_r évalue
  // la règle de fuseau : inutile de le faire à chaque tour de loop)
  time_t         _lastEpoch    = 0;
  struct tm      _cache        = {};
  bool           _cacheValid   = false;

  static void _onSntpSync(struct timeval* tv);
};

extern ClockManager clockManager;
