#pragma once
// =============================================================================
// Logger.h — Contrôle centralisé des traces série et des logs MQTT
// ESP32 Heating Controller v5.0
// Denis Mattera - 2025
//
// Macros (remplacent l'ancien LOG de config.h) :
//   LOG_E(prefix, fmt, ...)   erreur
//   LOG_W(prefix, fmt, ...)   avertissement
//   LOG(prefix, fmt, ...)     information (niveau INFO, compatible v4)
//   LOG_D(prefix, fmt, ...)   détail / debug
//   LOG_RAW_ENABLED()         vrai si les dumps série DEBUG sont autorisés
//
// La publication MQTT (Publisher::publishLog) consulte logMqttOn(niveau).
// Réglages : voir log_config.h.
// =============================================================================

#include <Arduino.h>

enum class LogLevel : uint8_t { ERROR = 0, WARN = 1, INFO = 2, DEBUG = 3 };

#include "log_config.h"

struct LogState {
  bool     serial      = LOG_SERIAL_DEFAULT;
  bool     mqtt        = LOG_MQTT_DEFAULT;
  LogLevel serialLevel = LOG_LEVEL_SERIAL;
  LogLevel mqttLevel   = LOG_LEVEL_MQTT;
};

inline LogState& logState() {
  static LogState s;
  return s;
}

// -----------------------------------------------------------------------------
// Interrogation
// -----------------------------------------------------------------------------
inline bool logSerialOn(LogLevel lvl) {
#if LOG_SERIAL_COMPILED
  return logState().serial && (uint8_t)lvl <= (uint8_t)logState().serialLevel;
#else
  (void)lvl; return false;
#endif
}

inline bool logMqttOn(LogLevel lvl) {
#if LOG_MQTT_COMPILED
  return logState().mqtt && (uint8_t)lvl <= (uint8_t)logState().mqttLevel;
#else
  (void)lvl; return false;
#endif
}

// -----------------------------------------------------------------------------
// Réglage à chaud
// -----------------------------------------------------------------------------
inline void logSetSerial(bool on)            { logState().serial = on; }
inline void logSetMqtt(bool on)              { logState().mqtt   = on; }
inline void logSetSerialLevel(LogLevel lvl)  { logState().serialLevel = lvl; }
inline void logSetMqttLevel(LogLevel lvl)    { logState().mqttLevel   = lvl; }

inline void logSetProduction() {
  logSetSerial(false);
  logSetMqtt(true);
  logSetMqttLevel(LogLevel::WARN);
}

inline void logSetDevelopment() {
  logSetSerial(true);
  logSetSerialLevel(LogLevel::DEBUG);
  logSetMqtt(true);
  logSetMqttLevel(LogLevel::INFO);
}

// -----------------------------------------------------------------------------
// Conversions texte (MQTT / HTTP / publishLog)
// -----------------------------------------------------------------------------
inline const char* logLevelToStr(LogLevel l) {
  switch (l) {
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::WARN:  return "WARN";
    case LogLevel::INFO:  return "INFO";
    default:              return "DEBUG";
  }
}

// "ERROR"/"WARN"/"INFO"/"DEBUG" (insensible à la casse) ; défaut = INFO
inline LogLevel logLevelFromStr(const char* s) {
  if (!s) return LogLevel::INFO;
  if (strcasecmp(s, "ERROR") == 0) return LogLevel::ERROR;
  if (strcasecmp(s, "WARN")  == 0 || strcasecmp(s, "WARNING") == 0) return LogLevel::WARN;
  if (strcasecmp(s, "DEBUG") == 0) return LogLevel::DEBUG;
  return LogLevel::INFO;
}

// -----------------------------------------------------------------------------
// Macros
// -----------------------------------------------------------------------------
#if LOG_SERIAL_COMPILED
  #define LOG_AT(lvl, prefix, fmt, ...) \
    do { if (logSerialOn(lvl)) Serial.printf("%s " fmt "\n", prefix, ##__VA_ARGS__); } while (0)
  #define LOG_RAW_ENABLED() logSerialOn(LogLevel::DEBUG)
#else
  #define LOG_AT(lvl, prefix, fmt, ...) do {} while (0)
  #define LOG_RAW_ENABLED() false
#endif

#define LOG_E(prefix, fmt, ...) LOG_AT(LogLevel::ERROR, prefix, fmt, ##__VA_ARGS__)
#define LOG_W(prefix, fmt, ...) LOG_AT(LogLevel::WARN,  prefix, fmt, ##__VA_ARGS__)
#define LOG(prefix, fmt, ...)   LOG_AT(LogLevel::INFO,  prefix, fmt, ##__VA_ARGS__)
#define LOG_D(prefix, fmt, ...) LOG_AT(LogLevel::DEBUG, prefix, fmt, ##__VA_ARGS__)
