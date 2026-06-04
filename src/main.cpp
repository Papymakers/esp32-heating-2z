// =============================================================================
// main.cpp — Orchestrateur principal + System FSM
// ESP32 Heating Controller v4.0-2Z — variante 2 zones TM1637
// Denis Mattera - 2025
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
#include "Publisher.h"
#include "WebUI.h"
#include "config.h"
#include "types.h"

// =============================================================================
// ÉTAT SYSTÈME
// =============================================================================

static SystemState  sysState  = SystemState::INIT;
static SystemStatus sysStatus = {};
static char         fwVersion[32] = {0};

// =============================================================================
// SWITCHES PHYSIQUES
// =============================================================================

static PhysicalSwitch swZone = { false, 0, 0, 0 };
static PhysicalSwitch swMode = { false, 0, 0, 0 };
static BootButton     bootBtn = { false, 0, false };

static unsigned long swZoneLastPress = 0;
static unsigned long swModeLastPress = 0;
static bool          swZonePending   = false;
static bool          swModePending   = false;

// =============================================================================
// ISR
// =============================================================================

void IRAM_ATTR isrSwitchZ1() {
  unsigned long now = millis();
  if (now - swZone.lastDebounce > DEBOUNCE_MS) {
    bool pinState = (REG_READ(GPIO_IN_REG) >> PIN_SWITCH_ZONE) & 1;
    swZone.pressed      = true;
    swZone.glitch       = pinState;
    swZone.lastDebounce = now;
  }
}

void IRAM_ATTR isrSwitchZ2() {
  unsigned long now = millis();
  if (now - swMode.lastDebounce > DEBOUNCE_MS) {
    bool pinState = (REG_READ(GPIO_IN_REG) >> PIN_SWITCH_MODE) & 1;
    swMode.pressed      = true;
    swMode.glitch       = pinState;
    swMode.lastDebounce = now;
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
    LOG(LOG_MAIN, "Static IP config failed");
  }
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiFailCount = 0;
  LOG(LOG_MAIN, "WiFi connecting to '%s'...", WIFI_SSID);
}

void wifiWatchdog() {
  if (millis() - lastWifiCheck < WIFI_CHECK_INTERVAL_MS) return;
  lastWifiCheck = millis();

  bool connected = WiFi.isConnected();

  if (connected != sysStatus.wifiConnected) {
    sysStatus.wifiConnected = connected;
    displayManager.setWifiStatus(connected,
                                  connected ? WiFi.localIP().toString().c_str()
                                            : nullptr);
    publisher.setSystemState(sysState);

    if (connected) {
      LOG(LOG_MAIN, "WiFi connected: %s", WiFi.localIP().toString().c_str());
      wifiFailCount = 0;
    } else {
      LOG(LOG_MAIN, "WiFi disconnected");
    }
  }

  if (!connected) {
    wifiFailCount++;
    if (wifiFailCount >= WIFI_MAX_FAIL_COUNT) {
      LOG(LOG_MAIN, "WiFi: max retries — reconnecting");
      setupWifi();
      wifiFailCount = 0;
    }
  }
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

  if (!sw.pressed) {
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
      displayManager.setZoneCmd(zone, newCmd, zoneManager.getZoneState(zone));
      displayManager.wake();
    }
    return;
  }

  // Appui long → STOP direct
  if (millis() - sw.lastDebounce > LONG_PRESS_MS) {
    sw.pressed    = false;
    sw.clickCount = 0;
    pending       = false;
    LOG(LOG_MAIN, "SW%d long press → STOP Z%d", zone, zone);
    commandHandler.handleCmd(zone, HeatingCmd::STOP,
                              CommandOrigin::USER, CommandSource::SRC_SW);
    displayManager.setZoneCmd(zone, HeatingCmd::STOP, zoneManager.getZoneState(zone));
    displayManager.wake();
    return;
  }

  sw.pressed = false;

  // Log état GPIO — diagnostic glitch ou appui réel
  if (sw.glitch) {
    LOG(LOG_MAIN, "SW%d glitch GPIO%d still HIGH", zone, pin);
    publisher.publishLog("WARN", "SW",
      "Z%d SW glitch GPIO%d still HIGH", zone, pin);
    return;
  }

  publisher.publishLog("INFO", "SW",
    "Z%d SW pressed GPIO%d click#%d", zone, pin, sw.clickCount + 1);

  displayManager.wake();
  sw.clickCount++;
  lastPress = millis();
  pending   = true;
}

void handleSwitchZ1() {
  handleSwitch(1, swZone, swZoneLastPress, swZonePending, PIN_SWITCH_ZONE);
}

void handleSwitchZ2() {
  handleSwitch(2, swMode, swModeLastPress, swModePending, PIN_SWITCH_MODE);
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

  if (millis() - bootBtn.pressStartTime < LONG_PRESS_MS) return;

  LOG(LOG_MAIN, "BOOT held → EEPROM ERASE");
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

  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    displayManager.setZoneCmd(z,
                               zoneManager.getCurrentCmd(z),
                               zoneManager.getZoneState(z));
  }

  displayManager.setMqttStatus(publisher.isMqttConnected());

  if (tempoManager.isActive()) {
    displayManager.setTempoState(tempoManager.getCurrentColor(),
                                  tempoManager.getCurrentPeriod(),
                                  tempoManager.isForceHG());
  }

  displayManager.setOverloadState(overloadManager.isOverloaded(),
                                   overloadManager.getState());
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(200);

  getVersion(fwVersion, sizeof(fwVersion));
  Serial.printf("\n%s\n%s\n\n", fwVersion, PROC);

  // --- I2C ---
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  delay(10);

  // --- StorageManager ---
  storageManager.begin();
  storageManager.dump();

  // --- DisplayManager TM1637 ---
  displayManager.begin();
  displayManager.showBootScreen(fwVersion);

  // --- ZoneManager ---
  zoneManager.begin();

  // --- WiFi FIRST ---
  setupWifi();
  delay(100);

  // --- Publisher ---
  publisher.begin(&zoneManager, &commandHandler, &tempoManager);
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

  // --- WebUI ---
  webUI.begin(&zoneManager, &commandHandler, &tempoManager,
              &scheduleManager, &overloadManager, &storageManager,
              &displayManager, &publisher);

  // --- GPIO switches ---
  pinMode(PIN_SWITCH_ZONE, INPUT_PULLUP);
  pinMode(PIN_SWITCH_MODE, INPUT_PULLUP);
  pinMode(PIN_BOOT_BTN,    INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_SWITCH_ZONE), isrSwitchZ1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_SWITCH_MODE), isrSwitchZ2, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BOOT_BTN),    isrBootButton, FALLING);

  sysStatus.selectedZone = 1;
  displayManager.setSelectedZone(1);

  // --- RGB LED : orange = boot ---
  neopixelWrite(PIN_RGB_LED, 20, 10, 0);

  LOG(LOG_MAIN, "Setup complete — entering loop");
}

// =============================================================================
// LOOP
// =============================================================================

static bool recoveryDone = false;

void loop() {
  wifiWatchdog();
  publisher.update();

  if (!recoveryDone && publisher.isMqttConnected()) {
    recoveryDone = true;
    commandHandler.handleRecovery();
    LOG(LOG_MAIN, "Recovery done");
    neopixelWrite(PIN_RGB_LED, 0, 5, 0);
    publisher.publishFullState();
  }

  linkyReader.update();
  zoneManager.update();
  overloadManager.update();
  webUI.update();

  handleSwitchZ1();
  handleSwitchZ2();
  handleBootButton();

  updateDisplay();
  displayManager.update();
  updateSystemFSM();

  static bool lastWifi = false;
  bool curWifi = WiFi.isConnected();
  if (curWifi != lastWifi) {
    lastWifi = curWifi;
    displayManager.setWifiStatus(curWifi,
      curWifi ? WiFi.localIP().toString().c_str() : nullptr);
  }
}
