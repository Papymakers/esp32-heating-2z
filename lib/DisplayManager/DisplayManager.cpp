// =============================================================================
// DisplayManager.cpp — OLED SH1106 (U8g2) ou TM1637, détecté au démarrage
// ESP32 Heating Controller v5.1-2Z
// Denis Mattera - 2025
// =============================================================================

#include "DisplayManager.h"
#include <Wire.h>
#include <U8g2lib.h>

DisplayManager displayManager;

// SH1106 128×64, tampon d'une page (128 o) : rendu page par page.
// Bus I2C partagé avec l'EEPROM, déjà initialisé par Wire.begin() dans
// setup() à 400 kHz ; U8g2 ne reconfigure pas l'horloge du bus
// (U8X8_DO_NOT_SET_WIRE_CLOCK dans platformio.ini).
static U8G2_SH1106_128X64_NONAME_1_HW_I2C s_oled(U8G2_R0, U8X8_PIN_NONE);

// Mise en page OLED (lignes de base)
static constexpr int OLED_Y_L1   = 10;   // police 6x12
static constexpr int OLED_Y_SEP1 = 13;
static constexpr int OLED_Y_L2   = 29;   // police 7x14B
static constexpr int OLED_Y_L3   = 46;
static constexpr int OLED_Y_SEP2 = 50;
static constexpr int OLED_Y_L4   = 62;   // police 6x12
static constexpr unsigned long OLED_MIN_RENDER_MS = 200;
static constexpr unsigned long BLINK_MS = 500;

// =============================================================================
// DÉTECTION
// =============================================================================

bool DisplayManager::_probeI2c(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Le TM1637 n'est pas un composant I2C : il acquitte chaque octet reçu en
// tirant DIO à l'état bas pendant le 9e coup d'horloge. On envoie la
// commande « écriture de données » (0x40, sans effet visible) et on lit
// cet acquittement, DIO relâchée avec le pull-up interne : sans TM1637,
// la ligne reste haute.
bool DisplayManager::_probeTm1637(uint8_t clk, uint8_t dio) {
  auto dly = [] { delayMicroseconds(5); };

  pinMode(clk, INPUT_PULLUP);
  pinMode(dio, INPUT_PULLUP);
  delayMicroseconds(50);
  // Lignes au repos forcées à l'état bas : rien d'exploitable branché
  if (digitalRead(clk) == LOW || digitalRead(dio) == LOW) return false;

  digitalWrite(clk, HIGH); pinMode(clk, OUTPUT);
  digitalWrite(dio, HIGH); pinMode(dio, OUTPUT);
  dly();

  // START : DIO descend pendant que CLK est haut
  digitalWrite(dio, LOW); dly();

  // Octet 0x40, bit de poids faible en premier
  uint8_t b = 0x40;
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(clk, LOW); dly();
    digitalWrite(dio, (b & 0x01) ? HIGH : LOW); dly();
    digitalWrite(clk, HIGH); dly();
    b >>= 1;
  }

  // 9e coup d'horloge : lecture de l'acquittement
  digitalWrite(clk, LOW);
  pinMode(dio, INPUT_PULLUP);
  dly();
  digitalWrite(clk, HIGH); dly();
  bool ack = (digitalRead(dio) == LOW);
  digitalWrite(clk, LOW); dly();

  // STOP : DIO monte pendant que CLK est haut
  digitalWrite(dio, LOW); pinMode(dio, OUTPUT); dly();
  digitalWrite(clk, HIGH); dly();
  digitalWrite(dio, HIGH); dly();
  return ack;
}

// =============================================================================
// INITIALISATION
// =============================================================================

bool DisplayManager::begin() {
  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    _zoneCmd[i]   = HeatingCmd::ECO;
    _zoneState[i] = ZoneState::IDLE;
    _zonePrio[i]  = CommandPriority::PRIO_DEFAULT;
  }

  // 1. OLED (prioritaire)
  if      (_probeI2c(OLED_ADDR_1)) _oledAddr = OLED_ADDR_1;
  else if (_probeI2c(OLED_ADDR_2)) _oledAddr = OLED_ADDR_2;

  if (_oledAddr) {
    s_oled.setI2CAddress(_oledAddr << 1);
    s_oled.begin();
    s_oled.setContrast(OLED_CONTRAST);
    s_oled.setFontPosBaseline();
    _type = DisplayType::OLED;
    LOG(LOG_DISP, "OLED SH1106 detected @0x%02X (sleep %us)",
        _oledAddr, OLED_SLEEP_SEC);
  }
  // 2. TM1637
  else if (_probeTm1637(TM1637_CLK_PIN, TM1637_DIO_PIN)) {
    _tm1637.begin();
    _tm1637.setBrightness(TM1637_BRIGHTNESS);
    _tmClearAll();
    _type = DisplayType::TM1637;
    LOG(LOG_DISP, "TM1637 detected (CLK=%d DIO=%d)",
        TM1637_CLK_PIN, TM1637_DIO_PIN);
  }
  // 3. Aucun
  else {
    _type = DisplayType::NONE;
    LOG_W(LOG_DISP, "No display detected — running headless");
    return false;
  }

  _present      = true;
  _screenOn     = true;
  _lastActivity = millis();
  _dirty        = true;
  return true;
}

uint16_t DisplayManager::_sleepSec() const {
  switch (_type) {
    case DisplayType::OLED:   return OLED_SLEEP_SEC;
    case DisplayType::TM1637: return TM1637_SLEEP_SEC;
    default:                  return 0;
  }
}

// =============================================================================
// LOOP
// =============================================================================

void DisplayManager::update() {
  if (!_present || !_screenOn) return;

  unsigned long now = millis();

  // Veille (jamais pendant un délestage ni un écran de défaut)
  uint16_t sleepS = _sleepSec();
  if (sleepS > 0 && !_overloadActive && !_faultShown &&
      now - _lastActivity >= (unsigned long)sleepS * 1000UL) {
    _sleep();
    return;
  }

  if (_faultShown) return;

  // Clignotement du bandeau de délestage (OLED)
  if (_type == DisplayType::OLED && _overloadActive &&
      now - _lastBlink >= BLINK_MS) {
    _lastBlink = now;
    _blinkOn   = !_blinkOn;
    _dirty     = true;
  }

  if (_type == DisplayType::TM1637) {
    if (_dirty) { _tmRender(); _dirty = false; return; }

    // Digit 4 mis à jour si WiFi ou Linky change
    if (_wifiConnected != _lastWifiState || _linkyConnected != _lastLinkyState) {
      _lastWifiState  = _wifiConnected;
      _lastLinkyState = _linkyConnected;
      _tmWriteStatus();
    }
    return;
  }

  // OLED : un rendu complet (8 pages) prend ~25 ms à 400 kHz ; on le
  // limite à un toutes les OLED_MIN_RENDER_MS.
  if (_dirty && now - _lastRender >= OLED_MIN_RENDER_MS) {
    _lastRender = now;
    _dirty      = false;
    _oledRender();
  }
}

void DisplayManager::_sleep() {
  _screenOn = false;
  if (_type == DisplayType::OLED) {
    s_oled.setPowerSave(1);
  } else if (_type == DisplayType::TM1637) {
    _tmClearAll();
    _tm1637.setBrightness(0);
  }
  LOG_D(LOG_DISP, "Display OFF (timeout %us)", _sleepSec());
}

// =============================================================================
// RÉVEIL
// =============================================================================

void DisplayManager::wake() {
  if (!_present) return;
  _lastActivity = millis();

  if (!_screenOn) {
    if (_type == DisplayType::OLED) s_oled.setPowerSave(0);
    else                            _tm1637.setBrightness(TM1637_BRIGHTNESS);
    _screenOn   = true;
    _dirty      = true;
    _lastRender = 0;
    LOG_D(LOG_DISP, "Display ON (wake)");
  }
}

// =============================================================================
// SETTERS
// =============================================================================

void DisplayManager::setZoneCmd(uint8_t zoneId, HeatingCmd cmd, ZoneState state,
                                CommandPriority prio) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;
  if (_zoneCmd[zi] == cmd && _zoneState[zi] == state && _zonePrio[zi] == prio) return;
  _zoneCmd[zi]   = cmd;
  _zoneState[zi] = state;
  _zonePrio[zi]  = prio;
  _dirty = true;
}

void DisplayManager::setSelectedZone(uint8_t zoneId) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  _selectedZone = zoneId;
  // Pas de dirty — un switch par zone, pas de zone sélectionnée affichée
}

void DisplayManager::setWifiStatus(bool connected, const char* ip) {
  (void)ip;
  if (_wifiConnected == connected) return;
  _wifiConnected = connected;
  _dirty = true;
}

void DisplayManager::setMqttStatus(bool connected) {
  if (_mqttConnected == connected) return;
  _mqttConnected = connected;
  if (_type == DisplayType::OLED) _dirty = true;
}

void DisplayManager::setLinkyStatus(bool connected) {
  if (_linkyConnected == connected) return;
  _linkyConnected = connected;
  _dirty = true;
}

void DisplayManager::setLinkyData(uint8_t iinst, bool fresh) {
  if (_iinst == iinst && _linkyFresh == fresh) return;
  _iinst      = iinst;
  _linkyFresh = fresh;
  if (_type == DisplayType::OLED) _dirty = true;
}

void DisplayManager::setClock(bool valid, uint8_t hh, uint8_t mm) {
  if (_clockValid == valid && _hh == hh && _mm == mm) return;
  _clockValid = valid;
  _hh = hh;
  _mm = mm;
  if (_type == DisplayType::OLED) _dirty = true;
}

void DisplayManager::setTempoState(TempoColor color, TempoPeriod period,
                                    bool forceHG) {
  if (_tempoColor == color && _tempoPeriod == period &&
      _tempoForceHG == forceHG) return;
  _tempoColor   = color;
  _tempoPeriod  = period;
  _tempoForceHG = forceHG;
  _dirty = true;
}

void DisplayManager::setOverloadState(bool active, OverloadState state) {
  (void)state;
  if (_overloadActive == active) return;
  _overloadActive = active;
  _blinkOn   = true;
  _lastBlink = millis();
  _dirty     = true;
  // Début de délestage : l'écran se rallume pour le signaler
  if (active) wake();
  else        _lastActivity = millis();   // veille 60 s après la fin
}

// =============================================================================
// ÉCRANS SPÉCIAUX
// =============================================================================

void DisplayManager::forceRefresh() {
  if (!_present) return;
  _faultShown = false;
  _dirty      = true;
}

void DisplayManager::showBootScreen(const char* version) {
  if (!_present) return;
  wake();

  if (_type == DisplayType::OLED) {
    _oledMessage("NAUCRATE 2Z", version);
  } else {
    // Allume tous les segments au boot (test display)
    for (uint8_t i = 0; i < 5; i++) _tm1637.writeData(i, DISP_RESET);
  }
  LOG_D(LOG_DISP, "Boot screen shown");
}

void DisplayManager::showFaultScreen(const char* reason) {
  if (!_present) return;
  wake();
  _faultShown = true;

  if (_type == DisplayType::OLED) {
    _oledMessage("DEFAUT", reason ? reason : "");
  } else {
    for (uint8_t i = 0; i < NUM_ZONES; i++) _tm1637.writeData(i, DISP_RESET);
    _tm1637.writeData(4, DISP_RESET);
  }
  LOG(LOG_DISP, "Fault screen (%s)", reason ? reason : "");
}

// =============================================================================
// RENDU OLED
// =============================================================================

static const char* oledCmdLabel(HeatingCmd cmd) {
  switch (cmd) {
    case HeatingCmd::STOP: return "ARRET";
    case HeatingCmd::HG:   return "HORS-GEL";
    case HeatingCmd::ECO:  return "ECO";
    case HeatingCmd::CONF: return "CONFORT";
    case HeatingCmd::CM2:  return "CONF -2";
    default:               return "?";
  }
}

static const char* oledTempoLabel(TempoColor c) {
  switch (c) {
    case TempoColor::TEMPO_BLUE:  return "BLEU";
    case TempoColor::TEMPO_WHITE: return "BLANC";
    case TempoColor::TEMPO_RED:   return "ROUGE";
    default:                      return "----";
  }
}

static const char* oledPeriodLabel(TempoPeriod p) {
  switch (p) {
    case TempoPeriod::HC: return "HC";
    case TempoPeriod::HP: return "HP";
    default:              return "--";
  }
}

void DisplayManager::_oledMessage(const char* l1, const char* l2) {
  _renders++;
  s_oled.firstPage();
  do {
    s_oled.setFont(u8g2_font_7x14B_tr);
    s_oled.drawStr((128 - s_oled.getStrWidth(l1)) / 2, 28, l1);
    s_oled.setFont(u8g2_font_6x12_tr);
    s_oled.drawStr((128 - s_oled.getStrWidth(l2)) / 2, 48, l2);
  } while (s_oled.nextPage());
}

void DisplayManager::_oledZoneLine(uint8_t zoneId, int baseline) {
  uint8_t zi = zoneId - 1;
  char line[20];
  const char* label = (_zoneState[zi] == ZoneState::LOCKED)
                        ? "BLOQUEE" : oledCmdLabel(_zoneCmd[zi]);
  snprintf(line, sizeof(line), "Z%u %s", zoneId, label);
  s_oled.drawStr(0, baseline, line);

  // Repères, alignés à droite
  char marks[4] = {0};
  uint8_t n = 0;
  if (_zoneState[zi] == ZoneState::OVERRIDE ||
      _zoneState[zi] == ZoneState::RESTORE)       marks[n++] = 'D';
  if (_zonePrio[zi] == CommandPriority::PRIO_TEMPO) marks[n++] = 'T';
  if (_zoneCmd[zi] == HeatingCmd::CM2)              marks[n++] = '~';
  if (n) s_oled.drawStr(128 - s_oled.getStrWidth(marks), baseline, marks);
}

void DisplayManager::_oledRender() {
  _renders++;

  // L1 : WiFi / MQTT + heure
  char l1[24];
  snprintf(l1, sizeof(l1), "WiFi %s MQTT %s",
           _wifiConnected ? "OK" : "--",
           (_wifiConnected && _mqttConnected) ? "OK" : "--");
  char clk[8];
  if (_clockValid) snprintf(clk, sizeof(clk), "%02u:%02u", _hh % 24, _mm % 60);
  else             snprintf(clk, sizeof(clk), "--:--");

  // L4 : Tempo + Iinst, ou bandeau de délestage
  char l4left[16], l4right[8], delest[20];
  snprintf(l4left, sizeof(l4left), "%s %s",
           oledTempoLabel(_tempoColor), oledPeriodLabel(_tempoPeriod));
  if (_linkyFresh) snprintf(l4right, sizeof(l4right), "%uA", _iinst);
  else             snprintf(l4right, sizeof(l4right), "--A");
  snprintf(delest, sizeof(delest), "DELEST %uA", _iinst);

  s_oled.firstPage();
  do {
    s_oled.setDrawColor(1);

    s_oled.setFont(u8g2_font_6x12_tr);
    s_oled.drawStr(0, OLED_Y_L1, l1);
    s_oled.drawStr(128 - s_oled.getStrWidth(clk), OLED_Y_L1, clk);
    s_oled.drawHLine(0, OLED_Y_SEP1, 128);

    s_oled.setFont(u8g2_font_7x14B_tr);
    _oledZoneLine(1, OLED_Y_L2);
    if (NUM_ZONES >= 2) _oledZoneLine(2, OLED_Y_L3);

    s_oled.setFont(u8g2_font_6x12_tr);
    if (_overloadActive) {
      if (_blinkOn) {
        s_oled.drawBox(0, OLED_Y_SEP2 + 1, 128, 64 - (OLED_Y_SEP2 + 1));
        s_oled.setDrawColor(0);
        s_oled.drawStr((128 - s_oled.getStrWidth(delest)) / 2, OLED_Y_L4, delest);
        s_oled.setDrawColor(1);
      } else {
        s_oled.drawHLine(0, OLED_Y_SEP2, 128);
        s_oled.drawStr((128 - s_oled.getStrWidth(delest)) / 2, OLED_Y_L4, delest);
      }
    } else {
      s_oled.drawHLine(0, OLED_Y_SEP2, 128);
      s_oled.drawStr(0, OLED_Y_L4, l4left);
      s_oled.drawStr(128 - s_oled.getStrWidth(l4right), OLED_Y_L4, l4right);
    }
  } while (s_oled.nextPage());
}

// =============================================================================
// RENDU TM1637 (inchangé depuis la v4)
// =============================================================================

void DisplayManager::_tmRender() {
  if (!_present || !_screenOn) return;
  _renders++;

  // Zones : on affiche la commande réellement appliquée par ZoneManager.
  // Elle intègre déjà le HG Tempo et le fallback de délestage.
  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    _tmWriteZone(z, _zoneCmd[z - 1]);
  }
  _lastWifiState  = _wifiConnected;
  _lastLinkyState = _linkyConnected;
  _tmWriteStatus();
}

void DisplayManager::_tmWriteZone(uint8_t zoneId, HeatingCmd cmd) {
  uint8_t digit = zoneId - 1;  // Zone 1 → digit 0, Zone 2 → digit 1
  _tm1637.writeData(digit, heatingCmdToDisp(cmd));
}

void DisplayManager::_tmWriteStatus() {
  // Digit 4 = GRID5 : bitmask WiFi + Linky
  uint8_t displayByte = 0;
  if (_wifiConnected)  displayByte |= DISP_WIFI;
  if (_linkyConnected) displayByte |= DISP_LINK;

  _tm1637.writeData(4, 0x00);  // Clear d'abord
  delay(1);
  _tm1637.writeData(4, displayByte);
}

void DisplayManager::_tmClearAll() {
  for (uint8_t i = 0; i < 5; i++) _tm1637.writeData(i, DISP_OFF);
}
