# Changelog

Toutes les évolutions notables de ce projet sont documentées ici.  
Format : [Semantic Versioning](https://semver.org/lang/fr/)

---

## [4.0-2Z] — 2025

### Refonte complète en architecture modulaire (libs PlatformIO)

**Ajouts**
- Architecture découpée en bibliothèques indépendantes : ZoneManager, CommandHandler, LinkyReader, TempoManager, OverloadManager, ScheduleManager, DisplayManager, StorageManager, Publisher, WebUI
- Planificateur hebdomadaire configurable depuis la WebUI
- Gestion dual EEPROM : 24C02 (256B) et 24C32 (4KB) sélectionnable par `#define`
- Compteurs saison Tempo (jours rouges/blancs) persistants en NVS Flash
- LED RGB de statut système (WiFi, MQTT, délestage)
- Bouton BOOT : appui court = reset WiFi, appui long (5s) = reset usine
- Support multi-clic sur switches physiques
- WebSockets pour mise à jour temps réel de la WebUI
- Interface Web responsive avec visualisation Linky et Tempo

**Modifications**
- Migration vers PlatformIO / VS Code (depuis Arduino IDE)
- Migration vers ESP32-C6-DevKitC-1-N8 (8MB Flash)
- Table de partitions personnalisée (OTA + SPIFFS)
- Commande CM2 : cycle automatique 7s/293s conforme à la norme fil pilote

**Corrections**
- Debounce ISR robuste via lecture directe registre GPIO
- Reconnexion WiFi/MQTT automatique avec compteur d'échecs

---

## [3.7] — 2024

### Version Arduino IDE — fichier monolithique

**Fonctionnalités présentes**
- Gestion 2 zones fil pilote (STOP, HG, ECO, CONFORT)
- Lecture Linky TIC historique
- Tempo EDF basique
- MQTT (PubSubClient)
- WebUI simple (WebServer)
- Affichage TM1637
- EEPROM 24C02

---

*Ce fichier est maintenu manuellement à chaque évolution significative.*
