#pragma once
// =============================================================================
// log_config.h — Réglage des traces (série + MQTT)
// ESP32 Heating Controller v5.0
// Denis Mattera - 2025
//
// SEUL fichier à modifier pour passer du développement à la production.
//
// 1. Compilation (supprime le code des traces, 0 octet en flash/RAM) :
//      LOG_SERIAL_COMPILED  0 → aucun printf compilé
//      LOG_MQTT_COMPILED    0 → aucune publication de log MQTT compilée
//
// 2. État au démarrage (si compilé) :
//      LOG_SERIAL_DEFAULT / LOG_MQTT_DEFAULT : actif ou non au boot
//      LOG_LEVEL_SERIAL / LOG_LEVEL_MQTT     : niveau minimal affiché
//
// 3. À chaud, par appel de fonction (voir Logger.h) :
//      logSetSerial(false);              // coupe les printf
//      logSetMqtt(true);                 // publication des logs MQTT
//      logSetSerialLevel(LogLevel::WARN);
//      logSetMqttLevel(LogLevel::INFO);
//      logSetProduction();               // série OFF, MQTT ≥ WARN
//      logSetDevelopment();              // série DEBUG, MQTT ≥ INFO
//    ou à distance :
//      MQTT  heatingCtrl_v4/log/set  {"serial":false,"mqtt":true,"level":"WARN","mqttLevel":"INFO"}
//      HTTP  POST /api/config/log    (même JSON)
//    Le réglage à chaud est sauvegardé en NVS et rechargé au boot.
//
// Niveaux : ERROR < WARN < INFO < DEBUG
//   ERROR : défaut nécessitant une action (EEPROM perdue, Linky muet…)
//   WARN  : situation anormale tolérée (parasite, délestage, reconnexion)
//   INFO  : événements de fonctionnement (commandes, transitions d'état)
//   DEBUG : détail (trames, horloge, lectures EEPROM, statistiques)
// =============================================================================

#define LOG_SERIAL_COMPILED   1
#define LOG_MQTT_COMPILED     1

#define LOG_SERIAL_DEFAULT    true
#define LOG_MQTT_DEFAULT      true

#define LOG_LEVEL_SERIAL      LogLevel::INFO
#define LOG_LEVEL_MQTT        LogLevel::INFO
