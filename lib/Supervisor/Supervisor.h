#pragma once
// =============================================================================
// Supervisor.h — Supervision et trace de la boucle coopérative
// ESP32 Heating Controller v5.0
// Denis Mattera - 2025
//
// 1. TÂCHES — chaque appel de loop() passe par run(). La tâche renvoie :
//      TaskResult::IDLE  rien à faire à ce tour
//      TaskResult::DONE  travail effectué (trame lue, commande appliquée…)
//      TaskResult::FAIL  échec (reconnexion ratée, checksum, EEPROM…)
//    Pour chaque tâche on trace : appels, travaux, échecs, temps consommé
//    (cumul, moyenne, max, charge CPU), dépassements de budget.
//    Deux jeux de compteurs : cumul depuis le boot + fenêtre courante
//    (HEALTH_PUBLISH_MS), remise à zéro à chaque publication.
//
// 2. SANTÉ — report(subsys, ok) : DEGRADED après maxFails échecs
//    consécutifs, OK au premier succès ; callback de repli à chaque
//    transition.
//
// 3. JOURNAL — tampon circulaire des TRACE_SIZE derniers événements
//    (démarrage, échecs, dépassements, transitions de santé, notes).
//
// Sorties :
//   MQTT  <MQTT_TOPIC_BASE>/health   toutes les HEALTH_PUBLISH_MS
//   HTTP  GET /api/health            même contenu
//   HTTP  GET /api/trace             journal d'événements
//   Série tableau des tâches à chaque fenêtre (niveau DEBUG)
//
// Filet de sécurité : Task Watchdog sur loopTask (enableLoopWDT, 5 s).
// =============================================================================

#include <Arduino.h>
#include <type_traits>
#include "config.h"

class Publisher;

// -----------------------------------------------------------------------------
// Tâches de loop(), par ordre de priorité décroissante
// (SCREEN et non DISPLAY : DISPLAY est une macro du SDK)
// -----------------------------------------------------------------------------
enum class SupTask : uint8_t {
  LINKY,      // lecture TIC
  OVERLOAD,   // FSM délestage
  ZONES,      // FSM zones + CM2
  SYSFSM,     // FSM système
  CLOCK,      // heure NTP + tick minute (index, Tempo, calendrier)
  INPUTS,     // interrupteurs + bouton BOOT
  SCREEN,     // afficheur OLED ou TM1637
  WEB,        // serveur HTTP
  WIFI,       // surveillance WiFi
  MQTT,       // client MQTT (reconnexion + réception)
  COUNT
};

enum class TaskResult : uint8_t { IDLE, DONE, FAIL };

// -----------------------------------------------------------------------------
// Sous-systèmes surveillés
// -----------------------------------------------------------------------------
enum class Subsys : uint8_t {
  LINKY,      // trames TIC reçues récemment
  EEPROM,     // EEPROM I2C répond, pas d'erreur d'accès
  WIFI,       // association WiFi
  MQTT,       // connexion broker
  COUNT
};

enum class HealthState : uint8_t { OK, DEGRADED };

// -----------------------------------------------------------------------------
// Journal
// -----------------------------------------------------------------------------
enum class TraceKind : uint8_t {
  BOOT,          // value = esp_reset_reason()
  TASK_FAIL,     // id = SupTask, value = durée µs
  TASK_OVERRUN,  // id = SupTask, value = durée µs
  HEALTH_DOWN,   // id = Subsys,  value = échecs consécutifs
  HEALTH_UP,     // id = Subsys
  NOTE           // id = code libre, value = valeur libre (voir TraceNote)
};

enum class TraceNote : uint8_t {
  EEPROM_SERIAL_CHANGED = 1,   // module EEPROM différent du précédent
  EEPROM_FORMATTED      = 2,   // EEPROM vierge ou étrangère → formatée
  MQTT_CONNECTED        = 3    // value = nb de connexions
};

struct TraceEvent {
  uint32_t  ms;       // millis() de l'événement
  uint32_t  value;
  TraceKind kind;
  uint8_t   id;
};

constexpr uint8_t TRACE_SIZE = 64;

// -----------------------------------------------------------------------------
// Statistiques
// -----------------------------------------------------------------------------
struct TaskWindow {
  uint32_t calls;
  uint32_t done;
  uint32_t fails;
  uint64_t totalUs;
  uint32_t maxUs;
};

struct TaskStat {
  const char* name;
  uint32_t    budgetUs;     // durée normale maximale d'un appel
  // cumul depuis le boot
  uint32_t    calls;
  uint32_t    done;
  uint32_t    fails;
  uint32_t    overruns;
  uint64_t    totalUs;
  uint32_t    maxUs;
  uint32_t    lastUs;
  // fenêtre courante (en cours) et dernière fenêtre close (publiée)
  TaskWindow  win;
  TaskWindow  last;
  // anti-saturation du journal
  uint32_t    lastTraceMs;
};

struct SubsysHealth {
  const char*   name;
  HealthState   state;
  uint8_t       fails;        // échecs consécutifs
  uint8_t       maxFails;     // seuil de passage en DEGRADED
  uint32_t      degradedCount;
  unsigned long changedMs;
};

// =============================================================================
// CLASSE SUPERVISOR
// =============================================================================

class Supervisor {
public:
  using HealthCallback = void (*)(Subsys, HealthState);

  void begin(Publisher* publisher, HealthCallback onChange);

  // ---------------------------------------------------------------------------
  // Exécution mesurée d'une tâche (fn renvoie TaskResult, bool ou void)
  // ---------------------------------------------------------------------------
  template <typename F>
  void run(SupTask t, F&& fn) {
    using R = std::invoke_result_t<F>;
    uint32_t t0 = micros();
    TaskResult r;
    if constexpr (std::is_same_v<R, TaskResult>) {
      r = fn();
    } else if constexpr (std::is_same_v<R, bool>) {
      r = fn() ? TaskResult::DONE : TaskResult::FAIL;
    } else {
      fn();
      r = TaskResult::IDLE;
    }
    _account(t, micros() - t0, r);
  }

  // Encadrement de loop() complète
  void loopStart() { _loopT0 = micros(); }
  void loopEnd();

  // ---------------------------------------------------------------------------
  // Santé
  // ---------------------------------------------------------------------------
  void        report(Subsys s, bool ok);
  HealthState state(Subsys s) const { return _health[(uint8_t)s].state; }
  bool        isOk(Subsys s)  const { return state(s) == HealthState::OK; }

  // ---------------------------------------------------------------------------
  // Journal
  // ---------------------------------------------------------------------------
  void trace(TraceKind kind, uint8_t id, uint32_t value);
  void note(TraceNote n, uint32_t value = 0) { trace(TraceKind::NOTE, (uint8_t)n, value); }

  // ---------------------------------------------------------------------------
  // Périodique : échantillonne pile/heap ; à chaque fenêtre publie l'état
  // (MQTT), imprime le tableau des tâches (série DEBUG) et remet la
  // fenêtre à zéro
  // ---------------------------------------------------------------------------
  void update();

  // Sérialisations
  size_t toJson(char* buf, size_t len) const;   // santé + tâches (fenêtre)
  void   traceToJson(String& out) const;        // journal + cumuls tâches

  const char* resetReason() const { return _resetReason; }
  const TaskStat& task(SupTask t) const { return _tasks[(uint8_t)t]; }

private:
  void _account(SupTask t, uint32_t us, TaskResult r);
  void _printWindow() const;
  void _resetWindow();

  Publisher*     _publisher = nullptr;
  HealthCallback _onChange  = nullptr;

  TaskStat     _tasks[(uint8_t)SupTask::COUNT];
  SubsysHealth _health[(uint8_t)Subsys::COUNT];

  TraceEvent   _trace[TRACE_SIZE] = {};
  uint8_t      _traceHead  = 0;     // prochain emplacement
  uint16_t     _traceCount = 0;     // nb total (saturé à TRACE_SIZE pour la lecture)
  uint32_t     _traceTotal = 0;     // nb d'événements depuis le boot

  uint32_t      _loopT0          = 0;
  uint32_t      _loopMaxUs       = 0;   // max sur la fenêtre
  uint32_t      _loopMaxEverUs   = 0;
  uint32_t      _loopCount       = 0;   // tours de loop sur la fenêtre
  uint32_t      _stackFreeMin    = 0;   // octets libres minimum (loopTask)
  uint32_t      _heapFree        = 0;
  uint32_t      _heapMin         = 0;
  unsigned long _windowStartMs   = 0;
  unsigned long _lastWindowMs    = 0;   // durée de la dernière fenêtre close
  unsigned long _lastSampleMs    = 0;
  unsigned long _lastBlockAlert  = 0;
  const char*   _resetReason     = "?";
};

extern Supervisor supervisor;

const char* supTaskName(SupTask t);
const char* subsysName(Subsys s);
