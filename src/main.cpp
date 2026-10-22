// =============================================================================
// main.cpp — Orchestrateur principal + System FSM + supervision
// ESP32 Heating Controller v5.1-2Z — Naucrate 2 zones
// (afficheur OLED SH1106 ou TM1637 détecté au démarrage, heure NTP)
// Denis Mattera - 2025
//
// Boucle coopérative ordonnée par priorité décroissante :
//   1. mesure et sécurité électrique (Linky, délestage, zones, FSM)
//   2. entrées utilisateur (interrupteurs, BOOT)
//   3. interface locale (afficheur, HTTP)
//   4. réseau (WiFi, MQTT) — peut être lent, passe en dernier
// Chaque tâche est chronométrée par le Supervisor et renvoie IDLE (rien à
// faire), DONE (travail effectué) ou FAIL ; la santé des sous-systèmes est
// contrôlée périodiquement avec mode de repli. Traces : log_config.h.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp32-hal-rgb-led.h>

#include "ZoneManager.h"
#include "CommandHandler.h"
#include "TempoManager.h"
#include "OverloadManager.h"
#include "LinkyReader.h"
#include "ScheduleManager.h"
#include "StorageManager.h"
#include "DisplayManager.h"
#include "ClockManager.h"
#include "Publisher.h"
#include "WebUI.h"
#include "Supervisor.h"
#include "config.h"
#include "types.h"

// Pile de loopTask : 12 Ko au lieu de 8 Ko (JSON ArduinoJson + WebServer
// + snprintf imbriqués). La marge réelle est publiée dans /health.
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

// =============================================================================
// ÉTAT SYSTÈME
// =============================================================================

static SystemState  sysState  = SystemState::INIT;
static SystemStatus sysStatus = {};
static char         fwVersion[32] = {0};

// =============================================================================
// SWITCHES PHYSIQUES
// =============================================================================

static PhysicalSwitch swZ1    = {};
static PhysicalSwitch swZ2    = {};
static BootButton     bootBtn = {};

static unsigned long swZ1LastPress = 0;
static unsigned long swZ2LastPress = 0;
static bool          swZ1Pending   = false;
static bool          swZ2Pending   = false;

// =============================================================================
// ISR
// =============================================================================

void IRAM_ATTR isrSwitchZ1() {
  unsigned long now = millis();
  if (now - swZ1.lastDebounce > DEBOUNCE_MS) {
    swZ1.pressed      = true;     // front descendant détecté
    swZ1.lastDebounce = now;      // horodatage pour fenêtre de confirmation
  }
}

void IRAM_ATTR isrSwitchZ2() {
  unsigned long now = millis();
  if (now - swZ2.lastDebounce > DEBOUNCE_MS) {
    swZ2.pressed      = true;
    swZ2.lastDebounce = now;
  }
}

void IRAM_ATTR isrBootButton() {
  unsigned long now = millis();
  if (!bootBtn.pressed) {
    bootBtn.pressed        = true;
    bootBtn.pressStartTime = now;
  }
}

// =============================================================================
// WIFI
// =============================================================================

static unsigned long lastWifiCheck = 0;
static uint8_t       wifiFailCount = 0;

void setupWifi() {
  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS)) {
    LOG_E(LOG_MAIN, "Static IP config failed");
  }
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiFailCount = 0;
  LOG(LOG_MAIN, "WiFi connecting to '%s'...", WIFI_SSID);
}

TaskResult wifiWatchdog() {
  if (millis() - lastWifiCheck < WIFI_CHECK_INTERVAL_MS) return TaskResult::IDLE;
  lastWifiCheck = millis();

  bool connected = WiFi.isConnected();

  if (connected != sysStatus.wifiConnected) {
    sysStatus.wifiConnected = connected;
    displayManager.setWifiStatus(connected);

    if (connected) {
      LOG(LOG_MAIN, "WiFi connected: %s", WiFi.localIP().toString().c_str());
      wifiFailCount = 0;
    } else {
      LOG_W(LOG_MAIN, "WiFi disconnected");
    }
  }

  if (!connected) {
    wifiFailCount++;
    if (wifiFailCount >= WIFI_MAX_FAIL_COUNT) {
      LOG_W(LOG_MAIN, "WiFi: max retries — reconnecting");
      setupWifi();
      wifiFailCount = 0;
    }
    return TaskResult::FAIL;
  }
  return TaskResult::DONE;
}

// =============================================================================
// SYSTEM FSM
// =============================================================================

void transitionTo(SystemState newState) {
  if (newState == sysState) return;
  LOG(LOG_MAIN, "SysState: %s → %s",
      systemStateToStr(sysState), systemStateToStr(newState));
  sysState = newState;
  sysStatus.state = newState;
  publisher.setSystemState(newState);

  switch (newState) {
    case SystemState::RUNNING:
      displayManager.forceRefresh();
      publisher.publishLog("INFO","MAIN","System RUNNING fw=%s", fwVersion);
      break;
    case SystemState::SAFE:
      publisher.publishLog("WARN","MAIN","System SAFE — overload active");
      break;
    case SystemState::FAULT:
      zoneManager.lockAllZones();
      displayManager.showFaultScreen("Fault");
      publisher.publishLog("ERROR","MAIN","System FAULT — zones locked");
      break;
    default: break;
  }
}

void updateSystemFSM() {
  switch (sysState) {
    case SystemState::INIT:
      transitionTo(SystemState::RUNNING);
      break;
    case SystemState::RUNNING:
      if (overloadManager.isOverloaded())
        transitionTo(SystemState::SAFE);
      break;
    case SystemState::SAFE:
      if (!overloadManager.isOverloaded() && !overloadManager.isDetecting())
        transitionTo(SystemState::RUNNING);
      break;
    case SystemState::FAULT:
      break;
  }
}

// =============================================================================
// SWITCHES PHYSIQUES Z1 et Z2
// Logique identique — cycle commandes sur la zone associée
// =============================================================================

static void handleSwitch(uint8_t          zone,
                          PhysicalSwitch&  sw,
                          unsigned long&   lastPress,
                          bool&            pending,
                          uint8_t          pin) {

  // ───────────────────────────────────────────────────────────────────────
  // Étape 1 : front descendant signalé par l'ISR → confirmation du niveau
  // Fenêtre non-bloquante : on attend SWITCH_CONFIRM_MS puis on relit le pin
  // ───────────────────────────────────────────────────────────────────────
  if (sw.pressed) {
    // Attendre la fenêtre de confirmation après le front (non-bloquant)
    if (millis() - sw.lastDebounce < SWITCH_CONFIRM_MS) {
      return;  // pas encore — on repassera au prochain tour de loop()
    }

    sw.pressed = false;

    // Relecture du niveau réel — un vrai appui maintient le pin LOW.
    // Une impulsion EMI (basculement HC/HP) est déjà retombée → pin HIGH.
    bool pinNow = (REG_READ(GPIO_IN_REG) >> pin) & 1;
    if (pinNow != 0) {
      LOG_W(LOG_MAIN, "SW%d parasite ignored (pin HIGH after %lums)",
          zone, SWITCH_CONFIRM_MS);
      publisher.publishLog("WARN", "SW",
        "Z%d SW parasite ignored (EMI?)", zone);
      return;
    }

    // Écran en veille : le premier appui ne fait que le réveiller, pour
    // lire l'état avant de modifier quoi que ce soit
    if (displayManager.isAsleep()) {
      displayManager.wake();
      LOG(LOG_MAIN, "SW%d: display wake only", zone);
      return;
    }

    // Appui confirmé → enregistre un clic
    displayManager.wake();
    sw.clickCount++;
    lastPress = millis();
    pending   = true;

    publisher.publishLog("INFO", "SW",
      "Z%d SW pressed GPIO%d click#%d", zone, pin, sw.clickCount);
    return;
  }

  // ───────────────────────────────────────────────────────────────────────
  // Étape 2 : fin de la fenêtre multi-clic → application de la commande
  // ───────────────────────────────────────────────────────────────────────
  if (pending && millis() - lastPress > SWITCH_MULTI_CLICK_MS) {
    pending = false;
    uint8_t clicks = sw.clickCount;
    sw.clickCount = 0;

    static const HeatingCmd cmdCycle[] = {
      HeatingCmd::HG, HeatingCmd::ECO,
      HeatingCmd::CONF, HeatingCmd::CM2, HeatingCmd::STOP
    };
    constexpr uint8_t LEN = sizeof(cmdCycle) / sizeof(cmdCycle[0]);

    // Base du cycle depuis EEPROM (ignore HG forcé par Tempo)
    HeatingCmd current = HeatingCmd::ECO;
    char savedStr[10] = {0};
    if (storageManager.loadZoneCmd(zone, savedStr, sizeof(savedStr))) {
      HeatingCmd saved = CommandHandler::parseCmd(savedStr);
      if (saved != HeatingCmd::UNKNOWN) current = saved;
    }

    uint8_t idx = 0;
    for (uint8_t i = 0; i < LEN; i++) {
      if (cmdCycle[i] == current) { idx = i; break; }
    }
    HeatingCmd newCmd = cmdCycle[(idx + clicks) % LEN];

    LOG(LOG_MAIN, "SW%d Z%d: %s → %s",
        zone, zone, heatingCmdToStr(current), heatingCmdToStr(newCmd));
    publisher.publishLog("INFO", "SW",
      "Z%d SW cmd %s → %s", zone, heatingCmdToStr(current), heatingCmdToStr(newCmd));
    commandHandler.handleCmd(zone, newCmd, CommandOrigin::USER, CommandSource::SRC_SW);
    displayManager.setZoneCmd(zone, zoneManager.getCurrentCmd(zone),
                              zoneManager.getZoneState(zone),
                              zoneManager.getZone(zone).activePriority);
    displayManager.wake();
  }
}

void handleSwitchZ1() {
  handleSwitch(1, swZ1, swZ1LastPress, swZ1Pending, PIN_SWITCH_Z1);
}

void handleSwitchZ2() {
  handleSwitch(2, swZ2, swZ2LastPress, swZ2Pending, PIN_SWITCH_Z2);
}

// =============================================================================
// BOOT BUTTON — appui long = effacement EEPROM
// =============================================================================

void handleBootButton() {
  if (!bootBtn.pressed || bootBtn.erased) return;

  if (digitalRead(PIN_BOOT_BTN) != LOW) {
    bootBtn.pressed        = false;
    bootBtn.pressStartTime = 0;
    return;
  }

  if (millis() - bootBtn.pressStartTime < BOOT_LONG_PRESS_MS) return;

  LOG_W(LOG_MAIN, "BOOT held → EEPROM ERASE");
  bootBtn.erased = true;

  publisher.publishLog("WARN","MAIN","BOOT long press — EEPROM erase");
  displayManager.showFaultScreen("Erase");
  delay(500);
  storageManager.eraseAll();
  delay(500);
  ESP.restart();
}

// =============================================================================
// MISE À JOUR DISPLAY
// =============================================================================

static unsigned long lastDisplayUpdate = 0;

void updateDisplay() {
  if (millis() - lastDisplayUpdate < DISPLAY_REFRESH_MS) return;
  lastDisplayUpdate = millis();

  if (!displayManager.isPresent()) return;

  // Commande réellement appliquée + priorité (repères T / D / ~ sur l'OLED)
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    displayManager.setZoneCmd(z,
                               zoneManager.getCurrentCmd(z),
                               zoneManager.getZoneState(z),
                               zoneManager.getZone(z).activePriority);
  }

  displayManager.setMqttStatus(publisher.isMqttConnected());
  displayManager.setLinkyData(linkyReader.getIinst(), linkyReader.isDataFresh());

  struct tm t;
  bool clockOk = clockManager.now(t);
  displayManager.setClock(clockOk, clockOk ? t.tm_hour : 0, clockOk ? t.tm_min : 0);

  // Couleur / période issues du PTEC, affichées même si l'option Tempo
  // (HG forcé) est désactivée
  displayManager.setTempoState(tempoManager.getCurrentColor(),
                                tempoManager.getCurrentPeriod(),
                                tempoManager.isForceHG());

  displayManager.setOverloadState(overloadManager.isOverloaded(),
                                   overloadManager.getState());
}

// =============================================================================
// HORLOGE — tick minute (heure NTP valide uniquement)
// =============================================================================

static void onMinuteTick(const struct tm& t, uint8_t dow) {
  uint8_t hh    = (uint8_t)t.tm_hour;
  uint8_t mm    = (uint8_t)t.tm_min;
  uint8_t day   = (uint8_t)t.tm_mday;
  uint8_t month = (uint8_t)(t.tm_mon + 1);

  // Horodatage à jour AVANT les publications déclenchées par ce tick
  // (sinon elles portaient la seconde précédente, ex. 19:59:59 pour 20:00)
  publisher.setTimestamp((uint32_t)hh * 3600UL + (uint32_t)mm * 60UL + (uint32_t)t.tm_sec);

  LOG_D(LOG_CLOCK, "Tick %02u:%02u %02u/%02u dow=%u", hh, mm, day, month, dow);
  scheduleManager.setDayOfWeek(dow);
  linkyReader.onClock(hh, mm, day, month);      // index journaliers 23 h
  scheduleManager.onClock(hh, mm);              // créneaux du calendrier
  tempoManager.onClock(hh, mm, day, month);     // compteurs, saison 1/09
}

// =============================================================================
// LED RGB — réservée à l'état de l'EEPROM (v5.1)
//   éteinte au boot, verte fixe = OK, bleue LED_INFO_MS = EEPROM formatée
//   ou module changé, rouge clignotante = absente ou en erreur.
// La supervision réseau / Linky ne passe plus par la LED (OLED, logs MQTT,
// /health).
// =============================================================================

enum class EeLed : uint8_t { OFF, GREEN, BLUE, RED_ON, RED_OFF };
static EeLed         eeLed          = EeLed::OFF;
static bool          eeLedInfo      = false;   // phase bleue en cours
static unsigned long eeLedInfoStart = 0;
static unsigned long eeLedBlinkMs   = 0;

static void eeLedWrite(EeLed c) {
  if (c == eeLed) return;
  eeLed = c;
  switch (c) {
    case EeLed::GREEN:  neopixelWrite(PIN_RGB_LED, 0, LED_LEVEL, 0); break;
    case EeLed::BLUE:   neopixelWrite(PIN_RGB_LED, 0, 0, LED_LEVEL); break;
    case EeLed::RED_ON: neopixelWrite(PIN_RGB_LED, LED_LEVEL, 0, 0); break;
    default:            neopixelWrite(PIN_RGB_LED, 0, 0, 0);         break;
  }
}

static void updateEepromLed() {
  unsigned long now = millis();
  bool ok = storageManager.isEepromPresent() && supervisor.isOk(Subsys::EEPROM);

  if (!ok) {
    eeLedInfo = false;                       // l'erreur passe avant l'info
    if (now - eeLedBlinkMs >= LED_BLINK_MS) {
      eeLedBlinkMs = now;
      eeLedWrite(eeLed == EeLed::RED_ON ? EeLed::RED_OFF : EeLed::RED_ON);
    }
    return;
  }

  if (eeLedInfo && now - eeLedInfoStart < LED_INFO_MS) {
    eeLedWrite(EeLed::BLUE);
    return;
  }
  eeLedInfo = false;
  eeLedWrite(EeLed::GREEN);
}

// Modes de repli appliqués à chaque transition OK ↔ DEGRADED
static void onHealthChange(Subsys s, HealthState st) {
  bool ok = (st == HealthState::OK);

  switch (s) {
    case Subsys::LINKY:
      if (ok) {
        // Resynchronise le délestage avec la mesure courante
        overloadManager.onIinstUpdate(linkyReader.getIinst(),
                                      linkyReader.getIsousc(),
                                      linkyReader.isOverload());
        publisher.publishLog("INFO", "SUP", "Linky data OK");
      } else {
        // Plus de mesure : le délestage ne peut plus être maintenu ni
        // déclenché ; les zones gardent leur commande (Tempo inchangé).
        overloadManager.onDataLost();
        displayManager.setLinkyStatus(false);
        publisher.publishLog("ERROR", "SUP",
          "Linky no data for %lus — overload detection suspended",
          (unsigned long)(linkyReader.lastFrameAgeMs() / 1000));
      }
      break;

    case Subsys::EEPROM:
      // StorageManager ignore déjà les écritures quand l'EEPROM est
      // absente : les zones continuent de fonctionner depuis la RAM.
      if (ok) {
        publisher.publishLog("INFO", "SUP", "EEPROM back online");
      } else {
        publisher.publishLog("ERROR", "SUP",
          "EEPROM lost — commands no longer persisted (errors=%lu)",
          (unsigned long)storageManager.getErrorCount());
      }
      break;

    case Subsys::WIFI:
    case Subsys::MQTT:
      // Fonctionnement local complet sans réseau : rien à replier,
      // l'information passe par l'OLED (ligne 1) et le log série.
      break;

    default: break;
  }
  updateEepromLed();
}

static void checkHealth() {
  static unsigned long lastCheck  = 0;
  static unsigned long lastEeprom = 0;
  static uint32_t      lastEeErr  = UINT32_MAX;   // 1er contrôle = référence

  unsigned long now = millis();
  if (now - lastCheck < HEALTH_CHECK_MS) return;
  lastCheck = now;

  supervisor.report(Subsys::LINKY, linkyReader.isDataFresh());
  supervisor.report(Subsys::WIFI,  WiFi.isConnected());
  supervisor.report(Subsys::MQTT,  publisher.isMqttConnected());

  if (now - lastEeprom >= EEPROM_CHECK_MS) {
    lastEeprom = now;
    uint32_t err = storageManager.getErrorCount();
    bool ok = storageManager.recheck() &&
              (lastEeErr == UINT32_MAX || err == lastEeErr);
    lastEeErr = err;
    supervisor.report(Subsys::EEPROM, ok);
  }
}

// À chaque (re)connexion MQTT : état complet pour resynchroniser le broker
// et les clients ; au premier passage, raison du dernier redémarrage.
static void handleMqttEdge() {
  static bool lastUp   = false;
  static bool bootSent = false;

  bool up = publisher.isMqttConnected();
  if (up == lastUp) return;
  lastUp = up;

  if (up) {
    if (!bootSent) {
      bootSent = true;
      publisher.publishLog("INFO", "MAIN",
                           "Boot fw=%s reset=%s eeprom=%s/%s display=%s id=%s",
                           fwVersion, supervisor.resetReason(),
                           storageManager.chipName(),
                           storageManager.hasSerial()
                             ? storageManager.getSerialHex() : "-",
                           displayManager.getTypeStr(),
                           publisher.getClientId());
      if (storageManager.serialChanged()) {
        publisher.publishLog("WARN", "STOR",
          "Module EEPROM remplacé (n° série %s) — vérifier les réglages",
          storageManager.getSerialHex());
      }
      if (storageManager.wasFormatted()) {
        publisher.publishLog("WARN", "STOR",
          "EEPROM vierge ou étrangère formatée au démarrage");
      }
    }
    supervisor.note(TraceNote::MQTT_CONNECTED, publisher.getMqttConnectCount());
    publisher.publishFullState();
    tempoManager.publishState();   // topic tempo retenu : état dès la connexion
    LOG(LOG_MAIN, "MQTT up — full state published");
  } else {
    LOG_W(LOG_MAIN, "MQTT down — running locally");
  }
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  // LED RGB éteinte dès le boot (réservée à l'état de l'EEPROM)
  neopixelWrite(PIN_RGB_LED, 0, 0, 0);

  Serial.begin(115200);
  delay(200);

  // Réglages des traces sauvegardés à chaud (NVS) — avant tout autre log
  storageManager.loadLogConfig();

  getVersion(fwVersion, sizeof(fwVersion));
  LOG(LOG_MAIN, "%s — %s", fwVersion, PROC);

  // --- I2C ---
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  delay(10);

  // --- StorageManager (EEPROM 24C32…24C512, identification facultative,
  //     en-tête, formatage) ---
  storageManager.begin();
  storageManager.dump();                      // série, niveau DEBUG

  // --- LED EEPROM : bleue quelques secondes si formatée / module changé ---
  if (storageManager.isEepromPresent() &&
      (storageManager.wasFormatted() || storageManager.serialChanged())) {
    eeLedInfo      = true;
    eeLedInfoStart = millis();
  }
  updateEepromLed();

  // --- DisplayManager : détection OLED (0x3C/0x3D) puis TM1637 ---
  displayManager.begin();
  {
    char bootTxt[24];
    snprintf(bootTxt, sizeof(bootTxt), "v%s", FW_VERSION);
    displayManager.showBootScreen(bootTxt);
  }
  LOG(LOG_MAIN, "Display: %s", displayManager.getTypeStr());

  // --- ZoneManager ---
  zoneManager.begin();

  // --- WiFi FIRST ---
  setupWifi();
  delay(100);

  // --- Heure NTP (après WiFi.begin : la pile réseau doit exister) ---
  clockManager.begin(onMinuteTick);

  // --- Publisher ---
  publisher.begin(&zoneManager, &commandHandler, &tempoManager, &linkyReader);
  publisher.setFirmwareVersion(fwVersion);

  // --- TempoManager ---
  tempoManager.begin(&commandHandler, &publisher);
  {
    bool en, whp, rhp, rhc;
    if (storageManager.loadTempoConfig(en, whp, rhp, rhc)) {
      if (en) tempoManager.setEnabled(true);
      tempoManager.setForceHG_WhiteHP(whp);
      tempoManager.setForceHG_RedHP(rhp);
      tempoManager.setForceHG_RedHC(rhc);
    }
  }

  // --- OverloadManager ---
  overloadManager.begin(&zoneManager, &commandHandler, &publisher);
  {
    bool en; unsigned long thr, res; HeatingCmd fb;
    if (storageManager.loadOverloadConfig(en, thr, res, fb)) {
      overloadManager.setEnabled(en);
      overloadManager.setThresholdMs(thr);
      overloadManager.setRestoreDelayMs(res);
      overloadManager.setFallbackCmd(fb);
    }
  }

  // --- CommandHandler ---
  commandHandler.begin(&zoneManager, &storageManager,
                        &publisher, &tempoManager);

  // --- LinkyReader ---
  linkyReader.begin(&publisher, &tempoManager, &overloadManager);

  // --- ScheduleManager ---
  scheduleManager.begin(&commandHandler, &storageManager, &publisher);
  scheduleManager.loadAll();

  {
    bool te, se, oe;
    if (storageManager.loadSysFlags(te, se, oe)) {
      if (te) tempoManager.setEnabled(true);
      if (se) scheduleManager.setEnabled(true);
      if (!oe) overloadManager.setEnabled(false);
    }
  }

  // --- Restauration locale des zones depuis l'EEPROM ---
  // Faite ici, sans attendre MQTT : avant ce correctif, un broker absent au
  // démarrage laissait les zones en ECO (état de ZoneManager::begin)
  // jusqu'à la première connexion.
  commandHandler.handleRecovery();
  LOG(LOG_MAIN, "Recovery done (local, EEPROM)");

  // --- WebUI ---
  webUI.begin(&zoneManager, &commandHandler, &tempoManager,
              &scheduleManager, &overloadManager, &storageManager,
              &displayManager, &publisher);

  // --- GPIO switches ---
  pinMode(PIN_SWITCH_Z1, INPUT_PULLUP);
  pinMode(PIN_SWITCH_Z2, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN,    INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_SWITCH_Z1), isrSwitchZ1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_SWITCH_Z2), isrSwitchZ2, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOOT_BTN),    isrBootButton, FALLING);

  sysStatus.selectedZone = 1;
  displayManager.setSelectedZone(1);

  // --- Supervision ---
  supervisor.begin(&publisher, onHealthChange);
  if (storageManager.serialChanged()) supervisor.note(TraceNote::EEPROM_SERIAL_CHANGED);
  if (storageManager.wasFormatted())  supervisor.note(TraceNote::EEPROM_FORMATTED);

  // --- Task Watchdog sur loopTask (5 s, panic → redémarrage) ---
  // loop() le nourrit automatiquement à chaque tour. Les timeouts MQTT
  // ont été réduits pour qu'aucune tâche normale n'en approche.
  enableLoopWDT();

  LOG(LOG_MAIN, "Setup complete — entering loop");
}

// =============================================================================
// LOOP — ordonnée par priorité décroissante
// =============================================================================

void loop() {
  supervisor.loopStart();

  // --- Priorité 1 : mesure et sécurité électrique ---
  supervisor.run(SupTask::LINKY, [] {
    uint32_t f = linkyReader.getFrameCount();
    uint32_t b = linkyReader.getBadLineCount();
    linkyReader.update();
    if (linkyReader.getBadLineCount() != b) return TaskResult::FAIL;
    return linkyReader.getFrameCount() != f ? TaskResult::DONE : TaskResult::IDLE;
  });

  supervisor.run(SupTask::OVERLOAD, [] {
    OverloadState before = overloadManager.getState();
    overloadManager.update();
    return overloadManager.getState() != before ? TaskResult::DONE
                                                : TaskResult::IDLE;
  });

  supervisor.run(SupTask::ZONES, [] {
    uint32_t w = zoneManager.getHwWriteCount();
    zoneManager.update();
    return zoneManager.getHwWriteCount() != w ? TaskResult::DONE
                                              : TaskResult::IDLE;
  });

  supervisor.run(SupTask::SYSFSM, [] {
    SystemState before = sysState;
    updateSystemFSM();
    return sysState != before ? TaskResult::DONE : TaskResult::IDLE;
  });

  // --- Horloge NTP : tick minute (index Linky, Tempo, calendrier) ---
  supervisor.run(SupTask::CLOCK, [] {
    bool tick = clockManager.update();
    publisher.setTimestamp(clockManager.secondsOfDay());
    return tick ? TaskResult::DONE : TaskResult::IDLE;
  });

  // --- Priorité 2 : entrées utilisateur ---
  supervisor.run(SupTask::INPUTS, [] {
    uint32_t acc = commandHandler.getAcceptedCount();
    uint32_t rej = commandHandler.getRejectedCount();
    uint32_t ee  = storageManager.getErrorCount();
    handleSwitchZ1();
    handleSwitchZ2();
    handleBootButton();
    if (commandHandler.getRejectedCount() != rej ||
        storageManager.getErrorCount() != ee) return TaskResult::FAIL;
    return commandHandler.getAcceptedCount() != acc ? TaskResult::DONE
                                                    : TaskResult::IDLE;
  });

  // --- Priorité 3 : interface locale ---
  supervisor.run(SupTask::SCREEN, [] {
    uint32_t r = displayManager.getRenderCount();
    updateDisplay();
    displayManager.setWifiStatus(WiFi.isConnected());   // réactivité LED
    displayManager.update();
    return displayManager.getRenderCount() != r ? TaskResult::DONE
                                                : TaskResult::IDLE;
  });

  supervisor.run(SupTask::WEB, [] {
    uint32_t n = webUI.getRequestCount();
    webUI.update();
    return webUI.getRequestCount() != n ? TaskResult::DONE : TaskResult::IDLE;
  });

  // --- Priorité 4 : réseau ---
  supervisor.run(SupTask::WIFI, [] { return wifiWatchdog(); });

  supervisor.run(SupTask::MQTT, [] {
    uint32_t fails = publisher.getMqttFailCount();
    uint32_t rx    = publisher.getMqttRxCount();
    uint32_t conn  = publisher.getMqttConnectCount();
    publisher.update();
    handleMqttEdge();
    if (publisher.getMqttFailCount() > fails) return TaskResult::FAIL;
    return (publisher.getMqttRxCount() != rx ||
            publisher.getMqttConnectCount() != conn) ? TaskResult::DONE
                                                     : TaskResult::IDLE;
  });

  // --- Supervision ---
  checkHealth();
  updateEepromLed();
  supervisor.update();
  supervisor.loopEnd();
}
