// =============================================================================
// Supervisor.cpp — Supervision et trace de la boucle coopérative
// ESP32 Heating Controller v5.0
// Denis Mattera - 2025
// =============================================================================

#include "Supervisor.h"
#include "Publisher.h"
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

Supervisor supervisor;

// -----------------------------------------------------------------------------
// Budgets (µs) : durée normale maximale d'un appel. Un dépassement n'est
// pas une erreur en soi (envoi d'une page web) mais un indicateur.
// -----------------------------------------------------------------------------
struct TaskDef { const char* name; uint32_t budgetUs; };
static const TaskDef TASK_DEFS[(uint8_t)SupTask::COUNT] = {
  { "linky",    5000  },
  { "overload", 2000  },
  { "zones",    2000  },
  { "sysfsm",   2000  },
  { "clock",    30000 },   // tick minute : publications MQTT éventuelles
  { "inputs",   25000 },   // un appui = lecture + écriture EEPROM
  { "screen",   40000 },   // OLED : 8 pages I2C (~25 ms) ; TM1637 < 5 ms
  { "web",      80000 },   // envoi de la page HTML (~34 Ko)
  { "wifi",     2000  },
  { "mqtt",     30000 },
};

// Seuils : contrôles consécutifs en échec avant DEGRADED.
// Linky/WiFi/MQTT toutes les HEALTH_CHECK_MS, EEPROM toutes les EEPROM_CHECK_MS.
struct HealthDef { const char* name; uint8_t maxFails; };
static const HealthDef HEALTH_DEFS[(uint8_t)Subsys::COUNT] = {
  { "linky",  2 },   // ~10 s après péremption des données
  { "eeprom", 2 },   // 2 contrôles ratés
  { "wifi",   6 },   // ~30 s
  { "mqtt",   6 },   // ~30 s
};

static constexpr uint32_t TRACE_RATE_MS = 5000;   // 1 événement / tâche / 5 s max

const char* supTaskName(SupTask t) {
  return (uint8_t)t < (uint8_t)SupTask::COUNT ? TASK_DEFS[(uint8_t)t].name : "?";
}

const char* subsysName(Subsys s) {
  return (uint8_t)s < (uint8_t)Subsys::COUNT ? HEALTH_DEFS[(uint8_t)s].name : "?";
}

static const char* resetReasonToStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

static const char* traceKindToStr(TraceKind k) {
  switch (k) {
    case TraceKind::BOOT:         return "boot";
    case TraceKind::TASK_FAIL:    return "fail";
    case TraceKind::TASK_OVERRUN: return "overrun";
    case TraceKind::HEALTH_DOWN:  return "degraded";
    case TraceKind::HEALTH_UP:    return "recovered";
    case TraceKind::NOTE:         return "note";
    default:                      return "?";
  }
}

// =============================================================================
// INITIALISATION
// =============================================================================

void Supervisor::begin(Publisher* publisher, HealthCallback onChange) {
  _publisher = publisher;
  _onChange  = onChange;

  for (uint8_t i = 0; i < (uint8_t)SupTask::COUNT; i++) {
    _tasks[i] = {};
    _tasks[i].name     = TASK_DEFS[i].name;
    _tasks[i].budgetUs = TASK_DEFS[i].budgetUs;
  }
  for (uint8_t i = 0; i < (uint8_t)Subsys::COUNT; i++) {
    _health[i] = {};
    _health[i].name     = HEALTH_DEFS[i].name;
    _health[i].maxFails = HEALTH_DEFS[i].maxFails;
    _health[i].state    = HealthState::OK;
  }

  esp_reset_reason_t rr = esp_reset_reason();
  _resetReason   = resetReasonToStr(rr);
  _stackFreeMin  = uxTaskGetStackHighWaterMark(NULL);
  _heapFree      = ESP.getFreeHeap();
  _heapMin       = ESP.getMinFreeHeap();
  _windowStartMs = millis();

  trace(TraceKind::BOOT, 0, (uint32_t)rr);
  LOG(LOG_SUP, "Supervisor ready (reset reason: %s)", _resetReason);
}

// =============================================================================
// TÂCHES
// =============================================================================

void Supervisor::_account(SupTask t, uint32_t us, TaskResult r) {
  TaskStat& st = _tasks[(uint8_t)t];
  unsigned long now = millis();

  st.calls++;       st.win.calls++;
  st.totalUs += us; st.win.totalUs += us;
  st.lastUs = us;
  if (us > st.maxUs)     st.maxUs = us;
  if (us > st.win.maxUs) st.win.maxUs = us;

  if (r == TaskResult::DONE) { st.done++;  st.win.done++; }

  bool traced = false;
  if (r == TaskResult::FAIL) {
    st.fails++; st.win.fails++;
    if (now - st.lastTraceMs >= TRACE_RATE_MS) {
      trace(TraceKind::TASK_FAIL, (uint8_t)t, us);
      traced = true;
    }
    LOG_D(LOG_SUP, "%s FAIL (%lu us)", st.name, (unsigned long)us);
  }

  if (us > st.budgetUs) {
    st.overruns++;
    if (!traced && now - st.lastTraceMs >= TRACE_RATE_MS) {
      trace(TraceKind::TASK_OVERRUN, (uint8_t)t, us);
      traced = true;
    }
    if (us >= st.maxUs) {
      LOG_W(LOG_SUP, "%s: new max %lu us (budget %lu us, overruns %lu)",
            st.name, (unsigned long)us, (unsigned long)st.budgetUs,
            (unsigned long)st.overruns);
    }
  }
  if (traced) st.lastTraceMs = now;

  // Blocage sévère : remonté en MQTT, au plus une fois par minute
  if (us > TASK_BLOCK_ALERT_US && _publisher &&
      now - _lastBlockAlert > 60000UL) {
    _lastBlockAlert = now;
    _publisher->publishLog("WARN", "SUP", "Task %s blocked %lu ms",
                           st.name, (unsigned long)(us / 1000));
  }
}

void Supervisor::loopEnd() {
  uint32_t us = micros() - _loopT0;
  if (us > _loopMaxUs)     _loopMaxUs = us;
  if (us > _loopMaxEverUs) _loopMaxEverUs = us;
  _loopCount++;
}

// =============================================================================
// SANTÉ
// =============================================================================

void Supervisor::report(Subsys s, bool ok) {
  SubsysHealth& h = _health[(uint8_t)s];

  if (ok) {
    h.fails = 0;
    if (h.state != HealthState::OK) {
      h.state     = HealthState::OK;
      h.changedMs = millis();
      trace(TraceKind::HEALTH_UP, (uint8_t)s, 0);
      LOG(LOG_SUP, "HEALTH %s → OK", h.name);
      if (_onChange) _onChange(s, h.state);
    }
    return;
  }

  if (h.fails < 255) h.fails++;
  if (h.state == HealthState::OK && h.fails >= h.maxFails) {
    h.state     = HealthState::DEGRADED;
    h.changedMs = millis();
    h.degradedCount++;
    trace(TraceKind::HEALTH_DOWN, (uint8_t)s, h.fails);
    LOG_W(LOG_SUP, "HEALTH %s → DEGRADED (%u fails)", h.name, h.fails);
    if (_onChange) _onChange(s, h.state);
  }
}

// =============================================================================
// JOURNAL
// =============================================================================

void Supervisor::trace(TraceKind kind, uint8_t id, uint32_t value) {
  TraceEvent& e = _trace[_traceHead];
  e.ms    = millis();
  e.kind  = kind;
  e.id    = id;
  e.value = value;
  _traceHead = (_traceHead + 1) % TRACE_SIZE;
  if (_traceCount < TRACE_SIZE) _traceCount++;
  _traceTotal++;
}

// =============================================================================
// PÉRIODIQUE
// =============================================================================

void Supervisor::update() {
  unsigned long now = millis();

  // Échantillonnage pile/heap toutes les secondes (appel peu coûteux)
  if (now - _lastSampleMs >= 1000UL) {
    _lastSampleMs = now;
    uint32_t sf = uxTaskGetStackHighWaterMark(NULL);   // octets sur ESP-IDF
    if (sf < _stackFreeMin) _stackFreeMin = sf;
    _heapFree = ESP.getFreeHeap();
    _heapMin  = ESP.getMinFreeHeap();
  }

  if (now - _windowStartMs < HEALTH_PUBLISH_MS) return;

  // --- Clôture de la fenêtre ---
  _lastWindowMs = now - _windowStartMs;
  for (uint8_t i = 0; i < (uint8_t)SupTask::COUNT; i++) {
    _tasks[i].last = _tasks[i].win;
  }

  _printWindow();

  if (_publisher && _publisher->isMqttConnected()) {
    char buf[1600];
    toJson(buf, sizeof(buf));
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/health", MQTT_TOPIC_BASE);
    _publisher->publishRaw(topic, buf);
  }

  _resetWindow();
}

void Supervisor::_resetWindow() {
  for (uint8_t i = 0; i < (uint8_t)SupTask::COUNT; i++) _tasks[i].win = {};
  _loopMaxUs     = 0;
  _loopCount     = 0;
  _windowStartMs = millis();
}

void Supervisor::_printWindow() const {
  if (!logSerialOn(LogLevel::DEBUG)) return;

  uint64_t winUs = (uint64_t)_lastWindowMs * 1000ULL;
  LOG_D(LOG_SUP, "---- tâches sur %lus : %lu tours, boucle max %lu us ----",
        (unsigned long)(_lastWindowMs / 1000), (unsigned long)_loopCount,
        (unsigned long)_loopMaxUs);
  LOG_D(LOG_SUP, "%-9s %8s %6s %5s %8s %8s %6s", "tâche", "appels", "trav",
        "échec", "moy µs", "max µs", "‰ CPU");
  for (uint8_t i = 0; i < (uint8_t)SupTask::COUNT; i++) {
    const TaskStat& t = _tasks[i];
    const TaskWindow& w = t.last;
    uint32_t avg  = w.calls ? (uint32_t)(w.totalUs / w.calls) : 0;
    uint32_t load = winUs ? (uint32_t)(w.totalUs * 1000ULL / winUs) : 0;
    LOG_D(LOG_SUP, "%-9s %8lu %6lu %5lu %8lu %8lu %6lu", t.name,
          (unsigned long)w.calls, (unsigned long)w.done, (unsigned long)w.fails,
          (unsigned long)avg, (unsigned long)w.maxUs, (unsigned long)load);
  }
  LOG_D(LOG_SUP, "heap %lu (min %lu)  pile libre min %lu o",
        (unsigned long)_heapFree, (unsigned long)_heapMin,
        (unsigned long)_stackFreeMin);
}

// =============================================================================
// SÉRIALISATION
// =============================================================================

size_t Supervisor::toJson(char* buf, size_t len) const {
  uint64_t winUs = (uint64_t)_lastWindowMs * 1000ULL;

  size_t n = snprintf(buf, len,
    "{\"uptime\":%lu,\"reset\":\"%s\",\"heap\":%lu,\"heapMin\":%lu,"
    "\"stackFree\":%lu,\"window\":%lu,\"loops\":%lu,\"loopMaxUs\":%lu,"
    "\"loopMaxEverUs\":%lu,\"events\":%lu,\"health\":{",
    (unsigned long)(millis() / 1000), _resetReason,
    (unsigned long)_heapFree, (unsigned long)_heapMin,
    (unsigned long)_stackFreeMin, (unsigned long)(_lastWindowMs / 1000),
    (unsigned long)_loopCount, (unsigned long)_loopMaxUs,
    (unsigned long)_loopMaxEverUs, (unsigned long)_traceTotal);

  for (uint8_t i = 0; i < (uint8_t)Subsys::COUNT && n < len; i++) {
    const SubsysHealth& h = _health[i];
    n += snprintf(buf + n, len - n,
                  "%s\"%s\":{\"ok\":%s,\"fails\":%u,\"degraded\":%lu}",
                  i ? "," : "", h.name,
                  h.state == HealthState::OK ? "true" : "false",
                  h.fails, (unsigned long)h.degradedCount);
  }
  if (n < len) n += snprintf(buf + n, len - n, "},\"tasks\":{");

  // Par tâche, sur la dernière fenêtre close :
  //   n appels, ok travaux, ko échecs, avg/max µs, load ‰ CPU,
  //   ovr dépassements de budget depuis le boot
  for (uint8_t i = 0; i < (uint8_t)SupTask::COUNT && n < len; i++) {
    const TaskStat& t = _tasks[i];
    const TaskWindow& w = t.last;
    uint32_t avg  = w.calls ? (uint32_t)(w.totalUs / w.calls) : 0;
    uint32_t load = winUs ? (uint32_t)(w.totalUs * 1000ULL / winUs) : 0;
    n += snprintf(buf + n, len - n,
                  "%s\"%s\":{\"n\":%lu,\"ok\":%lu,\"ko\":%lu,\"avg\":%lu,"
                  "\"max\":%lu,\"load\":%lu,\"ovr\":%lu}",
                  i ? "," : "", t.name,
                  (unsigned long)w.calls, (unsigned long)w.done,
                  (unsigned long)w.fails, (unsigned long)avg,
                  (unsigned long)w.maxUs, (unsigned long)load,
                  (unsigned long)t.overruns);
  }
  if (n < len) n += snprintf(buf + n, len - n, "}}");

  if (n >= len) {               // tronqué : JSON valide minimal
    snprintf(buf, len, "{\"error\":\"health json truncated\"}");
    n = strlen(buf);
  }
  return n;
}

void Supervisor::traceToJson(String& out) const {
  out.reserve(4096);
  char line[160];

  snprintf(line, sizeof(line),
           "{\"uptime\":%lu,\"reset\":\"%s\",\"total\":%lu,\"events\":[",
           (unsigned long)(millis() / 1000), _resetReason,
           (unsigned long)_traceTotal);
  out += line;

  // Du plus ancien au plus récent
  uint8_t start = (_traceCount < TRACE_SIZE) ? 0 : _traceHead;
  for (uint16_t i = 0; i < _traceCount; i++) {
    const TraceEvent& e = _trace[(start + i) % TRACE_SIZE];
    const char* who = "";
    switch (e.kind) {
      case TraceKind::TASK_FAIL:
      case TraceKind::TASK_OVERRUN: who = supTaskName((SupTask)e.id); break;
      case TraceKind::HEALTH_DOWN:
      case TraceKind::HEALTH_UP:    who = subsysName((Subsys)e.id);   break;
      default: break;
    }
    snprintf(line, sizeof(line),
             "%s{\"t\":%lu,\"kind\":\"%s\",\"id\":%u,\"name\":\"%s\",\"value\":%lu}",
             i ? "," : "", (unsigned long)e.ms, traceKindToStr(e.kind),
             e.id, who, (unsigned long)e.value);
    out += line;
  }

  // Cumuls par tâche depuis le boot
  out += "],\"tasks\":{";
  for (uint8_t i = 0; i < (uint8_t)SupTask::COUNT; i++) {
    const TaskStat& t = _tasks[i];
    uint32_t avg = t.calls ? (uint32_t)(t.totalUs / t.calls) : 0;
    snprintf(line, sizeof(line),
             "%s\"%s\":{\"calls\":%lu,\"done\":%lu,\"fails\":%lu,"
             "\"ms\":%lu,\"avgUs\":%lu,\"maxUs\":%lu,\"overruns\":%lu}",
             i ? "," : "", t.name,
             (unsigned long)t.calls, (unsigned long)t.done,
             (unsigned long)t.fails, (unsigned long)(t.totalUs / 1000ULL),
             (unsigned long)avg, (unsigned long)t.maxUs,
             (unsigned long)t.overruns);
    out += line;
  }
  out += "}}";
}
