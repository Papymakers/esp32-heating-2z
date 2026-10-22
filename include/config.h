#pragma once
// =============================================================================
// config.h — Configuration hardware, pins, réseau, timings
// ESP32 Heating Controller v5.1 — Naucrate 2 zones (afficheur OLED ou TM1637,
// détecté au démarrage)
// Denis Mattera - 2025
// =============================================================================

#include <Arduino.h>
#include "types.h"

// =============================================================================
// FIRMWARE
// =============================================================================

inline constexpr char FW_NAME[]    = "ESP32_Heating";
inline constexpr char FW_VERSION[] = "5.1.7-2Z"; // v5.1.7 : topic tempo retenu
#define BUILD_ID __DATE__ " " __TIME__

inline void getVersion(char* buffer, size_t size) {
  snprintf(buffer, size, "%s v%s", FW_NAME, FW_VERSION);
}

inline constexpr char PROC[] = "ESP32-C6-DevKitC-1-N8";

// =============================================================================
// RÉSEAU
// =============================================================================

// Identifiants WiFi dans include/secrets.h (exclu du dépôt Git).
// Copier include/secrets.example.h en include/secrets.h et le compléter.
#include "secrets.h"

inline const IPAddress LOCAL_IP (192, 168, 1, 50);
inline const IPAddress GATEWAY  (192, 168, 1, 20);
inline const IPAddress SUBNET   (255, 255, 255, 0);
inline const IPAddress DNS      (192, 168, 1, 20);

inline constexpr char     MQTT_BROKER[]    = "192.168.1.20";
inline constexpr uint16_t MQTT_PORT       = 1883;
// Identifiant client MQTT = préfixe + 3 derniers octets de l'adresse MAC
// (ex. naucrate-2z-a1b2c3) : unique par module, quel que soit l'afficheur.
// Deux modules ne peuvent plus se déconnecter mutuellement du broker.
inline constexpr char     MQTT_CLIENT_PREFIX[] = "naucrate-2z";

// =============================================================================
// HEURE — NTP (seule source d'heure depuis la v5.1)
// =============================================================================

// Fuseau POSIX Europe/Paris : heure légale + changements été/hiver
inline constexpr char NTP_TZ[]      = "CET-1CEST,M3.5.0,M10.5.0/3";
// Serveurs NTP (le premier peut être un serveur du réseau local)
inline constexpr char NTP_SERVER1[] = "fr.pool.ntp.org";
inline constexpr char NTP_SERVER2[] = "pool.ntp.org";
// Heure considérée valide au-delà de cette date (2024-01-01 00:00 UTC)
constexpr time_t NTP_VALID_EPOCH    = 1704067200;

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
constexpr uint8_t PIN_SWITCH_Z1    =  7;   // Switch Zone 1
constexpr uint8_t PIN_SWITCH_Z2    =  6;   // Switch Zone 2

// =============================================================================
// AFFICHAGE — détecté au démarrage (OLED prioritaire, sinon TM1637, sinon
// fonctionnement sans afficheur)
// =============================================================================

// OLED SH1106 1,3" 128×64 — sur le bus I2C de l'EEPROM (SDA 18 / SCL 19)
constexpr uint8_t OLED_ADDR_1      = 0x3C;
constexpr uint8_t OLED_ADDR_2      = 0x3D;
constexpr uint8_t OLED_SLEEP_SEC   = 60;    // veille écran (0 = jamais)
constexpr uint8_t OLED_CONTRAST    = 128;   // 0-255

// TM1637 — carte LED (broches dédiées)
constexpr uint8_t TM1637_CLK_PIN   = 23;
constexpr uint8_t TM1637_DIO_PIN   = 22;
constexpr uint8_t TM1637_BRIGHTNESS = 3;   // 0-7
constexpr uint8_t TM1637_SLEEP_SEC = 0;    // veille (0 = jamais)

// =============================================================================
// LED RGB (GPIO8) — réservée à l'état de l'EEPROM
//   éteinte au boot, verte fixe = OK, bleue quelques secondes = EEPROM
//   formatée ou module changé, rouge clignotante = absente / en erreur
// =============================================================================

constexpr uint8_t       LED_LEVEL          = 20;    // intensité (0-255)
constexpr unsigned long LED_INFO_MS        = 4000;  // durée du bleu
constexpr unsigned long LED_BLINK_MS       = 500;   // demi-période du rouge

// =============================================================================
// EEPROM I2C
// =============================================================================

// EEPROM à adressage 16 bits, de 24C32 (4 Ko) à 24C512 (64 Ko), sans
// détection de taille : la carte mémoire n'occupe que le premier Ko
// (calendrier compris). Références testées en compilation : 24C32 /
// 24LC32A / AT24C32, 24C64…24C256, 24CS512 (avec n° de série et ID
// fabricant), 24LC512, AT24C512. N° de série et ID fabricant sont lus
// s'ils existent, jamais exigés. La 24C02 (adressage 8 bits) n'est PAS
// compatible.

constexpr uint8_t EEPROM_ADDR     = 0x50;  // matrice : 1010 A2 A1 A0 (A2..A0 = 0)
constexpr uint8_t EEPROM_SEC_ADDR = 0x58;  // registre sécurité 24CSxxx : 1011 A2 A1 A0
constexpr uint8_t EEPROM_MFG_CODE = 0x7C;  // code réservé F8h/F9h (ID fabricant)

// Taille minimale requise (24C32) — la puce réelle peut être plus grande
constexpr uint32_t EEPROM_SIZE        = 4096;
// Page déclarée = plus petite page de la famille (32 o sur 24C32/24C64).
// _write() ne franchit jamais une limite de page : 32 o convient donc aussi
// aux puces à pages de 64 ou 128 o (formatage un peu plus lent, ~0,2 s).
constexpr uint16_t EEPROM_PAGE        = 32;
constexpr uint32_t EEPROM_MFG_ID_CS512 = 0x00D0C8;  // Microchip 24CS512, rév. 1

constexpr bool     EEPROM_ADDR_16  = true;   // adresses mémoire sur 16 bits
constexpr uint16_t EE_WRITE_CHUNK  = 64;     // ≤ buffer Wire (128) - 2 octets d'adresse

// Registre de sécurité 24CSxxx (adresses de mot)
constexpr uint16_t EE_SEC_SERIAL   = 0x0800; // 16 octets, lecture seule
constexpr uint16_t EE_SEC_IDPAGE   = 0x0880; // 128 octets, programmable et verrouillable

// -----------------------------------------------------------------------------
// Carte mémoire v1 (inchangée depuis la v5.0, tient dans 1 Ko)
// Enregistrements alignés sur 16 octets : l'ECC du 24CS512 travaille par
// mots de 4 octets et réécrit le mot entier à chaque écriture partielle ;
// l'alignement évite qu'un enregistrement en use un autre.
// -----------------------------------------------------------------------------
constexpr uint8_t  EE_LAYOUT_VERSION = 1;
constexpr char     EE_MAGIC[4]       = { 'N', 'A', 'U', 'C' };

constexpr uint16_t EE_HEADER        = 0x0000;  // 16 o : magic, version, réservé
constexpr uint16_t EE_ZONE_BASE     = 0x0010;  // 4 zones × 16 o
constexpr uint16_t EE_ZONE_STRIDE   = 16;
constexpr uint16_t EE_SYS_FLAGS     = 0x0050;  // 1 o
constexpr uint16_t EE_TEMPO_RED     = 0x0054;  // 1 o : compteur jours rouges
constexpr uint16_t EE_TEMPO_WHITE   = 0x0055;  // 1 o : compteur jours blancs
constexpr uint16_t EE_PROFILES_BASE = 0x0100;  // 8 profils × 64 o
constexpr uint16_t EE_PROFILE_STRIDE= 64;
constexpr uint16_t EE_SCHEDULE_BASE = 0x0300;  // 4 zones × 16 o
constexpr uint16_t EE_SCHED_STRIDE  = 16;
constexpr uint16_t EE_USED_END      = 0x0400;  // fin de la zone utilisée

static_assert(EE_ZONE_BASE + 4 * EE_ZONE_STRIDE <= EE_SYS_FLAGS, "zones / flags");
static_assert(EE_SCHEDULE_BASE + 4 * EE_SCHED_STRIDE <= EE_USED_END, "layout");
static_assert(EE_USED_END <= EEPROM_SIZE, "layout > EEPROM");

// =============================================================================
// TIMINGS
// =============================================================================

constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 5000;
constexpr uint8_t       WIFI_MAX_FAIL_COUNT    = 5;
constexpr unsigned long MQTT_RECONNECT_MS      = 5000;
constexpr unsigned long DEBOUNCE_MS            = 50;    // anti-rebond ISR
constexpr unsigned long SWITCH_CONFIRM_MS      = 100;   // fenêtre confirmation niveau bas
constexpr unsigned long SWITCH_MULTI_CLICK_MS  = 600;
constexpr unsigned long BOOT_LONG_PRESS_MS     = 5000;  // appui BOOT → effacement EEPROM
constexpr unsigned long CM2_INTERVAL_7S        = 7000;
constexpr unsigned long CM2_INTERVAL_293S      = 293000;
constexpr unsigned long OVERLOAD_THRESHOLD_MS  = 10000;
constexpr unsigned long FALLBACK_DURATION_MS   = 300000;
constexpr unsigned long SCHEDULE_CHECK_MS      = 30000;
constexpr unsigned long DISPLAY_REFRESH_MS     = 1000;

// =============================================================================
// SUPERVISION
// =============================================================================

constexpr unsigned long MQTT_RECONNECT_MAX_MS   = 60000;  // backoff max reconnexion MQTT
constexpr uint32_t      MQTT_CONNECT_TIMEOUT_MS = 1500;   // timeout connexion TCP au broker
constexpr uint16_t      MQTT_SOCKET_TIMEOUT_S   = 2;      // attente CONNACK / lecture
constexpr size_t        LINKY_RX_BUFFER         = 1024;   // ~8 s de TIC à 1200 bauds
constexpr unsigned long LINKY_STALE_MS          = 15000;  // pas de trame → données périmées
constexpr unsigned long HEALTH_CHECK_MS         = 5000;   // période des contrôles de santé
constexpr unsigned long EEPROM_CHECK_MS         = 60000;  // période de sonde EEPROM
constexpr unsigned long HEALTH_PUBLISH_MS       = 60000;  // publication MQTT de la santé
constexpr uint32_t      TASK_BLOCK_ALERT_US     = 1000000;// blocage > 1 s → log MQTT

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
// TRACES — préfixes ; activation/désactivation dans log_config.h
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
inline constexpr char LOG_SUP[]   = "[SUP] ";
inline constexpr char LOG_CLOCK[] = "[CLK] ";

#include "Logger.h"
