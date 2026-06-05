# ESP32 Heating Controller — 2 zones (TM1637)

Gestionnaire de chauffage électrique à fil pilote pour **2 zones**, basé sur ESP32-C6.  
Contrôle les radiateurs via fil pilote (6 ordres), lecture du compteur Linky, gestion Tempo EDF, interface Web et MQTT.

> **Variante matérielle :** affichage TM1637 (4 digits 7 segments) — PCB en production.  
> La variante 4 zones avec afficheur OLED est disponible dans le repo [`esp32-heating-4z`](../esp32-heating-4z).

---

## Fonctionnalités

- Pilotage fil pilote **2 zones** — 5 ordres : STOP, Hors-Gel, ECO, CONFORT, Confort -2°C
- Lecture trame **Linky TIC** (mode historique)
- Gestion **Tempo EDF** — jours Bleu/Blanc/Rouge avec compteurs saison (NVS Flash)
- **Délestage automatique** sur dépassement de puissance souscrite
- Interface **Web responsive** embarquée (WebSockets)
- Communication **MQTT** complète (status, commandes, log)
- **Planificateur** hebdomadaire configurable depuis la WebUI
- Sauvegarde des états en **EEPROM I2C** (24C02 ou 24C32)
- Boutons physiques avec gestion multi-clic et appui long
- LED RGB de statut système

---

## Architecture matérielle

### Composants

| Composant | Référence | Rôle |
|-----------|-----------|------|
| Microcontrôleur | ESP32-C6-DevKitC-1-N8 | CPU WiFi/MQTT/Web |
| Afficheur | TM1637 4 digits | Affichage zone/mode |
| EEPROM | 24C02 (I2C, 0x50) | Persistance des états |
| Optocoupleurs | 2× MOC3041 par zone | Commande fil pilote 230V |
| Alimentation | 5V/USB ou régulateur | Alimentation carte |

### Brochage ESP32-C6

| Fonction | GPIO |
|----------|------|
| Zone 1 — MOC positif | GPIO 3 |
| Zone 1 — MOC négatif | GPIO 2 |
| Zone 2 — MOC positif | GPIO 11 |
| Zone 2 — MOC négatif | GPIO 10 |
| Switch sélection zone | GPIO 7 |
| Switch changement mode | GPIO 6 |
| Linky RX (TIC) | GPIO 4 |
| TM1637 CLK | GPIO 23 |
| TM1637 DIO | GPIO 22 |
| I2C SDA (EEPROM) | GPIO 18 |
| I2C SCL (EEPROM) | GPIO 19 |
| LED RGB | GPIO 8 |
| Bouton BOOT | GPIO 9 |

### Principe de commande fil pilote (MOC3041, actif bas)

| Ordre | MOC+ | MOC− |
|-------|------|------|
| STOP | LOW | HIGH |
| Hors-Gel | HIGH | LOW |
| ECO | LOW | LOW |
| CONFORT | HIGH | HIGH |
| CM2 | cycle 7s/293s | — |

---

## Installation

### Prérequis

- [PlatformIO](https://platformio.org/) (VS Code ou CLI)
- ESP32-C6-DevKitC-1 avec 8 MB Flash

### Configuration

Copier et adapter `include/config.h` :

```cpp
// Réseau WiFi
inline constexpr char WIFI_SSID[]     = "YOUR_SSID";
inline constexpr char WIFI_PASSWORD[] = "YOUR_PASSWORD";

// IP fixe (adapter à votre réseau)
inline const IPAddress LOCAL_IP (192, 168, 1, 50);
inline const IPAddress GATEWAY  (192, 168, 1, 1);

// Broker MQTT
inline constexpr char MQTT_BROKER[] = "192.168.1.x";
```

Choisir le type d'EEPROM dans `config.h` :

```cpp
#define EEPROM_TYPE_24C02   // 256 octets (défaut)
// #define EEPROM_TYPE_24C32   // 4KB
```

### Compilation et upload

```bash
pio run --target upload
pio device monitor
```

---

## Interface Web

Accéder via `http://<LOCAL_IP>` depuis le réseau local.

- Contrôle manuel de chaque zone
- Visualisation de la puissance Linky en temps réel
- Période Tempo du jour (Bleu/Blanc/Rouge)
- Planificateur hebdomadaire

---

## Topics MQTT

### Publication (device → broker)

| Topic | Contenu |
|-------|---------|
| `heatingCtrl_v4/status` | JSON état complet du système |
| `heatingCtrl_v4/zone/N/mode` | Mode actuel de la zone N |
| `heatingCtrl_v4/tempo` | Période Tempo courante |
| `heatingCtrl_v4/linky` | Données Linky (PAPP, PTEC…) |
| `heatingCtrl_v4/log` | Messages de log |

### Souscription (broker → device)

| Topic | Payload | Action |
|-------|---------|--------|
| `heatingCtrl_v4/zone/N/cmd` | `STOP` `HG` `ECO` `CONF` `CM2` | Commande zone N |

---

## Structure du projet

```
esp32-heating-2z/
├── include/
│   ├── config.h          # Configuration (pins, réseau, timings)
│   └── types.h           # Types et structures partagés
├── src/
│   └── main.cpp          # Orchestrateur principal + FSM système
├── lib/
│   ├── ZoneManager/      # Gestion des zones de chauffage
│   ├── CommandHandler/   # Traitement des commandes MQTT/Web
│   ├── LinkyReader/      # Décodage trame TIC Linky
│   ├── TempoManager/     # Gestion périodes Tempo EDF
│   ├── OverloadManager/  # Délestage automatique
│   ├── ScheduleManager/  # Planificateur hebdomadaire
│   ├── DisplayManager/   # Affichage TM1637
│   ├── StorageManager/   # EEPROM I2C (24C02/24C32)
│   ├── Publisher/        # Publication MQTT
│   └── WebUI/            # Interface Web + WebSockets
├── index.html            # WebUI (embarquée dans le firmware)
├── partitions.csv        # Table de partitions 8MB Flash
├── platformio.ini        # Configuration PlatformIO
├── CHANGELOG.md
└── LICENSE
```

---

## Dépendances

```ini
lib_deps =
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^7.0.0
    erriez/ErriezTM1637
```

---

## Licence

MIT — voir [LICENSE](LICENSE)

## Auteur

Denis Mattera — 2025

---

## Hardware

Les fichiers de fabrication sont disponibles dans le dossier `hardware/` :

```
hardware/
├── main-board/
│   ├── schematic.svg       # Schéma électronique
│   ├── pcb_top.svg         # Vue du PCB
│   ├── gerbers.zip         # Fichiers de fabrication JLCPCB
│   └── BOM.csv             # Liste des composants
├── display-board/
│   ├── schematic.svg
│   ├── pcb_top.svg
│   ├── gerbers.zip
│   └── BOM.csv
└── README.md               # Notes de fabrication (JLCPCB settings)
```

---

## Commander des cartes

Ce projet représente 4 ans de développement, de prototypage et de tests en conditions réelles. Le firmware est open source — si vous souhaitez soutenir le projet ou gagner du temps, les cartes sont disponibles à la commande.

| Option | Contenu | Prix indicatif |
|--------|---------|----------------|
| **PCB nu** | Carte principale + carte affichage, sans composants | 15€ |
| **Kit** | PCB + tous les composants sélectionnés et testés | 35€ |
| **Carte assemblée** | Prête à flasher et configurer | 55€ |

*Frais de port en sus. Expédition depuis la France.*

📧 Commandes et questions : **support@papymakers.com** - https://papymakers.com/

---

## Contact & Support

- **Bug / question technique** → ouvrir une [Issue](../../issues)
- **Commandes** → support@papymakers.com
- **Discussions générales** → onglet [Discussions](../../discussions)
