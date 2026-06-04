#pragma once
// =============================================================================
// config.h — Configuration hardware, pins, réseau, timings
// ESP32 Heating Controller v4.0 — variante 2 zones TM1637
// Denis Mattera - 2025
// =============================================================================

#include <Arduino.h>
#include "types.h"

// =============================================================================
// FIRMWARE
// =============================================================================

inline constexpr char FW_NAME[]    = "ESP32_Heating";
inline constexpr char FW_VERSION[] = "4.0-2Z";
#define BUILD_ID __DATE__ " " __TIME__

inline void getVersion(char* buffer, size_t size) {
  snprintf(buffer, size, "%s v%s", FW_NAME, FW_VERSION);
}

inline constexpr char PROC[] = "ESP32-C6-DevKitC-1-N8";

// =============================================================================
// RÉSEAU — À adapter à votre installation
// =============================================================================

inline constexpr char WIFI_SSID[]      = "YOUR_SSID";
inline constexpr char WIFI_PASSWORD[]  = "YOUR_PASSWORD";

inline const IPAddress LOCAL_IP (192, 168, 1, 50);   // À adapter
inline const IPAddress GATEWAY  (192, 168, 1, 1);    // À adapter
inline const IPAddress SUBNET   (255, 255, 255, 0);
inline const IPAddress DNS      (192, 168, 1, 1);    // À adapter

inline constexpr char     MQTT_BROKER[]    = "192.168.1.x";  // IP de votre broker MQTT
inline constexpr uint16_t MQTT_PORT       = 1883;
inline constexpr char     DEVICE_ID[]     = "heatingCtrl_v4";
inline constexpr char     MQTT_CLIENT_ID[] = "heatingCtrl_v4";

// =============================================================================
// ZONES — 2 zones (PCB existant)
// =============================================================================

constexpr uint8_t NUM_ZONES = 2;

// Sorties MOC3041 (actif bas) — câblage PCB existant
//   STOP  → pos=LOW,  neg=HIGH
//   HG    → pos=HIGH, neg=LOW
//   ECO   → pos=LOW,  neg=LOW
//   CONF  → pos=HIGH, neg=HIGH

inline constexpr uint8_t PIN_MOC_POS[NUM_ZONES] = {  3, 11 };
inline constexpr uint8_t PIN_MOC_NEG[NUM_ZONES] = {  2, 10 };

// =============================================================================
// PINS DIVERS — PCB existant inchangé
// =============================================================================

constexpr uint8_t PIN_SDA          = 18;
constexpr uint8_t PIN_SCL          = 19;
constexpr uint8_t PIN_LINKY_RX     =  4;   // Serial1 RX (TIC Linky)
constexpr uint8_t PIN_LINKY_TX     = 21;   // Serial1 TX (non utilisé)
constexpr uint8_t PIN_RGB_LED      =  8;   // LED RGB onboard
constexpr uint8_t PIN_BOOT_BTN     =  9;   // Bouton BOOT
constexpr uint8_t PIN_SWITCH_ZONE  =  7;   // Switch Z1 — sélection zone
constexpr uint8_t PIN_SWITCH_MODE  =  6;   // Switch Z2 — changement mode

// =============================================================================
// AFFICHAGE TM1637 (4 digits, 7 segments)
// =============================================================================

constexpr uint8_t TM1637_CLK_PIN    = 23;
constexpr uint8_t TM1637_DIO_PIN    = 22;
constexpr uint8_t TM1637_BRIGHTNESS = 4;   // 0-7

// Timeout veille affichage (secondes)
constexpr uint8_t DISPLAY_TIMEOUT_SEC = 20;

// =============================================================================
// EEPROM I2C
// =============================================================================

// Décommente selon la puce installée :
#define EEPROM_TYPE_24C02   // 256 octets, adresse 1 octet
// #define EEPROM_TYPE_24C32   // 4KB, adresse 2 octets

constexpr uint8_t EEPROM_ADDR = 0x50;

#ifdef EEPROM_TYPE_24C02
  constexpr uint16_t EEPROM_SIZE      = 256;
  constexpr bool     EEPROM_ADDR_16   = false;
  constexpr uint16_t EE_ZONE_BASE     = 0x00;
  constexpr uint16_t EE_SYS_FLAGS     = 0x28;
  constexpr uint16_t EE_TEMPO_RED     = 0x29;
  constexpr uint16_t EE_TEMPO_WHITE   = 0x2A;
  constexpr uint16_t EE_PROFILES_BASE = 0x30;
  constexpr uint16_t EE_SCHEDULE_BASE = 0x30;
#else
  constexpr uint16_t EEPROM_SIZE      = 4096;
  constexpr bool     EEPROM_ADDR_16   = true;
  constexpr uint16_t EE_ZONE_BASE     = 0x000;
  constexpr uint16_t EE_SYS_FLAGS     = 0x028;
  constexpr uint16_t EE_TEMPO_RED     = 0x029;
  constexpr uint16_t EE_TEMPO_WHITE   = 0x02A;
  constexpr uint16_t EE_PROFILES_BASE = 0x030;
  constexpr uint16_t EE_SCHEDULE_BASE = 0x230;
#endif

inline constexpr uint16_t eeZoneAddr(uint8_t zoneIndex) {
  return EE_ZONE_BASE + (zoneIndex * 10);
}

// =============================================================================
// TIMINGS
// =============================================================================

constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 5000;
constexpr uint8_t       WIFI_MAX_FAIL_COUNT    = 5;
constexpr unsigned long MQTT_RECONNECT_MS      = 5000;
constexpr unsigned long DEBOUNCE_MS            = 250;
constexpr unsigned long LONG_PRESS_MS          = 5000;
constexpr unsigned long SWITCH_MULTI_CLICK_MS  = 600;
constexpr unsigned long CM2_INTERVAL_7S        = 7000;
constexpr unsigned long CM2_INTERVAL_293S      = 293000;
constexpr unsigned long OVERLOAD_THRESHOLD_MS  = 10000;
constexpr unsigned long FALLBACK_DURATION_MS   = 300000;
constexpr unsigned long SCHEDULE_CHECK_MS      = 30000;
constexpr unsigned long DISPLAY_REFRESH_MS     = 1000;

// =============================================================================
// MQTT TOPICS
// =============================================================================

inline constexpr char MQTT_TOPIC_BASE[]      = "heatingCtrl_v4";
inline constexpr char MQTT_TOPIC_STATUS[]    = "heatingCtrl_v4/status";
inline constexpr char MQTT_TOPIC_TEMPO[]     = "heatingCtrl_v4/tempo";
inline constexpr char MQTT_TOPIC_LINKY[]     = "heatingCtrl_v4/linky";
inline constexpr char MQTT_TOPIC_CMD_IN[]    = "heatingCtrl_v4/zone/+/cmd";
inline constexpr char MQTT_TOPIC_LOG[]       = "heatingCtrl_v4/log";
inline constexpr char MQTT_TOPIC_ZONE_FMT[]  = "heatingCtrl_v4/zone/%d/%s";

// =============================================================================
// COMMANDES VALIDES
// =============================================================================

inline constexpr char CMD_STOP[] = "STOP";
inline constexpr char CMD_HG[]   = "HG";
inline constexpr char CMD_ECO[]  = "ECO";
inline constexpr char CMD_CONF[] = "CONF";
inline constexpr char CMD_CM2[]  = "CM2";

// =============================================================================
// DEBUG
// =============================================================================

inline constexpr char LOG_MAIN[]  = "[MAIN]";
inline constexpr char LOG_ZONE[]  = "[ZONE]";
inline constexpr char LOG_CMD[]   = "[CMD] ";
inline constexpr char LOG_TEMPO[] = "[TMPO]";
inline constexpr char LOG_SCHED[] = "[SCHED]";
inline constexpr char LOG_OVLD[]  = "[OVLD]";
inline constexpr char LOG_LINKY[] = "[LNKY]";
inline constexpr char LOG_MQTT[]  = "[MQTT]";
inline constexpr char LOG_WEB[]   = "[WEB] ";
inline constexpr char LOG_DISP[]  = "[DISP]";
inline constexpr char LOG_STORE[] = "[STOR]";

#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
  #define LOG(prefix, fmt, ...) Serial.printf("%s " fmt "\n", prefix, ##__VA_ARGS__)
#else
  #define LOG(prefix, fmt, ...) do {} while(0)
#endif
