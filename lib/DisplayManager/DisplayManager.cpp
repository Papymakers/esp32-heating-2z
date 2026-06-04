// =============================================================================
// DisplayManager.cpp — Affichage TM1637 via ErriezTM1637
// ESP32 Heating Controller v4.0-2Z
// Denis Mattera - 2025
// =============================================================================

#include "DisplayManager.h"

DisplayManager displayManager;

// =============================================================================
// INITIALISATION
// =============================================================================

bool DisplayManager::begin() {
  _tm1637.begin();
  _tm1637.setBrightness(TM1637_BRIGHTNESS);
  _clearAll();

  _present      = true;
  _screenOn     = true;
  _lastActivity = millis();

  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    _zoneCmd[i]   = HeatingCmd::ECO;
    _zoneState[i] = ZoneState::IDLE;
  }

  LOG(LOG_DISP, "TM1637 ready (CLK=%d DIO=%d)", TM1637_CLK_PIN, TM1637_DIO_PIN);
  return true;
}

// =============================================================================
// LOOP
// =============================================================================

void DisplayManager::update() {
  if (!_present) return;

  if (_screenOn) {
    if (DISPLAY_TIMEOUT_SEC > 0 &&
        millis() - _lastActivity >= (unsigned long)DISPLAY_TIMEOUT_SEC * 1000UL) {
      _clearAll();
      _tm1637.setBrightness(0);
      _screenOn = false;
      LOG(LOG_DISP, "Display OFF (timeout %ds)", DISPLAY_TIMEOUT_SEC);
      return;
    }

    if (_dirty) {
      _render();
      _dirty = false;
      return;
    }

    // WiFi — réécrit uniquement si état a changé
    if (_wifiConnected != _lastWifiState) {
      _lastWifiState = _wifiConnected;
      _tm1637.writeData(4, _wifiConnected ? DISP_WIFI : 0x00);
    }

    // Linky — rafraîchi toutes les 100ms pour suivre le clignotement STX/ETX
    unsigned long now = millis();
    if (now - _lastStatusMs >= 100UL) {
      _lastStatusMs = now;
      uint8_t digit4 = (_wifiConnected  ? DISP_WIFI : 0x00) |
                       (_linkyConnected ? DISP_LINK : 0x00);
      _tm1637.writeData(4, digit4);
    }
  }
}

// =============================================================================
// RÉVEIL
// =============================================================================

void DisplayManager::wake() {
  if (!_present) return;
  _lastActivity = millis();

  if (!_screenOn) {
    _tm1637.setBrightness(TM1637_BRIGHTNESS);
    _screenOn = true;
    _dirty    = true;
    LOG(LOG_DISP, "Display ON (wake)");
  }
}

// =============================================================================
// SETTERS
// =============================================================================

void DisplayManager::setZoneCmd(uint8_t zoneId, HeatingCmd cmd, ZoneState state) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;
  if (_zoneCmd[zi] == cmd && _zoneState[zi] == state) return;
  _zoneCmd[zi]   = cmd;
  _zoneState[zi] = state;
  _dirty = true;
}

void DisplayManager::setSelectedZone(uint8_t zoneId) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  _selectedZone = zoneId;
  // Pas de dirty — la zone sélectionnée n'est pas affichée sur TM1637
}

void DisplayManager::setWifiStatus(bool connected, const char* ip) {
  (void)ip;
  if (_wifiConnected == connected) return;
  _wifiConnected = connected;
  _dirty = true;
}

void DisplayManager::setLinkyStatus(bool connected) {
  if (_linkyConnected == connected) return;
  _linkyConnected = connected;
  _dirty = true;
}

void DisplayManager::setTempoState(TempoColor color, TempoPeriod period,
                                    bool forceHG) {
  (void)color; (void)period;
  if (_tempoForceHG == forceHG) return;
  _tempoForceHG = forceHG;
  _dirty = true;
}

void DisplayManager::setOverloadState(bool active, OverloadState state) {
  (void)state;
  if (_overloadActive == active) return;
  _overloadActive = active;
  _dirty = true;
}

// =============================================================================
// ÉCRANS SPÉCIAUX
// =============================================================================

void DisplayManager::forceRefresh() {
  if (!_present) return;
  _dirty = true;
}

void DisplayManager::showBootScreen(const char* version) {
  if (!_present) return;
  (void)version;
  wake();

  // Allume tous les segments au boot (test display)
  for (uint8_t i = 0; i < 5; i++) {
    _tm1637.writeData(i, DISP_RESET);
  }
  LOG(LOG_DISP, "Boot screen shown");
}

void DisplayManager::showFaultScreen(const char* reason) {
  if (!_present) return;
  (void)reason;
  wake();

  // Affiche RESET sur toutes les zones
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    _tm1637.writeData(i, DISP_RESET);
  }
  _tm1637.writeData(4, DISP_RESET);
  LOG(LOG_DISP, "Fault screen");
}

// =============================================================================
// RENDU PRINCIPAL
// =============================================================================

void DisplayManager::_render() {
  if (!_present || !_screenOn) return;

  // Zones
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    HeatingCmd cmd = _zoneCmd[z - 1];

    // Si délestage actif → STOP sur toutes les zones
    if (_overloadActive) { cmd = HeatingCmd::STOP; }

    // Si Tempo force HG → HG sauf si la zone est en STOP
    else if (_tempoForceHG && cmd != HeatingCmd::STOP) {
      cmd = HeatingCmd::HG;
    }

    _writeZone(z, cmd);
  }

  // Digit 4 : status WiFi + Linky
  _writeStatus();
}

void DisplayManager::_writeZone(uint8_t zoneId, HeatingCmd cmd) {
  uint8_t digit   = zoneId - 1;  // Zone 1 → digit 0, Zone 2 → digit 1
  uint8_t dispCmd = heatingCmdToDisp(cmd);
  _tm1637.writeData(digit, dispCmd);
}

void DisplayManager::_writeStatus() {
  // Digit 4 = GRID5 : bitmask WiFi + Linky
  uint8_t displayByte = 0;
  if (_wifiConnected)  displayByte |= DISP_WIFI;
  if (_linkyConnected) displayByte |= DISP_LINK;

  _tm1637.writeData(4, 0x00);  // Clear d'abord
  delay(1);
  _tm1637.writeData(4, displayByte);
}

void DisplayManager::_clearAll() {
  for (uint8_t i = 0; i < 5; i++) {
    _tm1637.writeData(i, DISP_OFF);
  }
}
