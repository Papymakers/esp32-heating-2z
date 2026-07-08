# Gestionnaire de chauffage 2 zones — ESP32 fil pilote + Linky + MQTT

> 🇫🇷 Français | [🇬🇧 English](README.md)

<p align="center">
  <img src="hardware/images/montage-din.png" alt="Exemple de réalisation — module monté en boîtier DIN" width="480">
</p>

## 🛒 PCB disponible en boutique
le **circuit imprimé nu** est disponible à l'unité sur
**[papymakers.com](https://papymakers.com/produits/gestionnaire-chauffage-2-zones/)** — **15 €**.

> 🆕 **Nouveau (juillet 2026)** — Le **planificateur hebdomadaire** est désormais
> documenté. Il s'active avec une EEPROM 24C32 en remplacement direct de la 24C02.
> Voir [Planificateur hebdomadaire](#planificateur-hebdomadaire-eeprom-24c32).

Gestionnaire de chauffage électrique à fil pilote pour **2 zones**, basé sur ESP32-C6.  
Contrôle les radiateurs via fil pilote (5 ordres utiles), lecture du compteur Linky, gestion Tempo EDF, interface Web et MQTT.

> ⚠️ **Cette carte se raccorde au 230 V.** Son installation et son câblage
> sont réservés à des personnes ayant les compétences requises pour intervenir
> en sécurité sur le réseau électrique. Voir la section
> [Sécurité électrique](#sécurité-électrique) avant toute mise en œuvre.

> **Affichage TM1637** (4 digits 7 segments) — PCB en production.  
> Un kit PCB compagnon (afficheur TM1637 + bargraphe LEDs de statut + switches, en
> boîtier DIN 6 modules) est disponible dans le dépôt
> [`heating-2z-display-board`](https://github.com/Papymakers/heating-2z-display-board).

---

## Fonctionnalités

- Pilotage fil pilote **2 zones** — 5 ordres : STOP, Hors-Gel, ECO, CONFORT, Confort -2°C (CM2)
  <br>*(le Confort -1°C de la norme 6 ordres est volontairement omis : redondant avec le réglage de consigne des radiateurs modernes, il n'apporte pas de palier de délestage utile en pratique)*
- Lecture trame **Linky TIC** (mode historique)
- Gestion **Tempo EDF** — jours Bleu/Blanc/Rouge avec compteurs de saison (rouges/blancs) sauvegardés en EEPROM
- **Délestage automatique** sur dépassement de puissance souscrite
- Interface **Web responsive** embarquée (rafraîchissement par polling HTTP)
- Communication **MQTT** complète (status, commandes, log)
- **Planificateur** hebdomadaire configurable depuis la WebUI *(nécessite une EEPROM 24C32 — voir [note ci-dessous](#planificateur-hebdomadaire-eeprom-24c32))*
- Sauvegarde des états en **EEPROM I2C** (24C02 par défaut, ou 24C32 pour le planificateur)
- Boutons physiques avec gestion multi-clic et appui long
- LED RGB de statut système

---

## Sécurité électrique

Cette carte est alimentée en **230 V** et commande des radiateurs sur le secteur.
Les protections suivantes ne sont **pas** optionnelles.

### Protection de l'alimentation de la carte

L'alimentation 230 V du gestionnaire **doit** être protégée par un **disjoncteur
bipolaire 2 A** dédié (coupure simultanée phase + neutre). Un calibre faible est
volontaire : la carte ne consomme que quelques watts (module HLK-10M05, 10 W max),
un disjoncteur 2 A assure donc une protection rapprochée en cas de défaut interne,
bien plus fine que le disjoncteur de départ du circuit chauffage. La coupure
bipolaire garantit qu'aucune borne de la carte ne reste sous tension à l'entretien.

### Protection des sorties fil pilote

Chaque sortie fil pilote est protégée par un **fusible réarmable (PPTC / polyswitch)
230 V – 100 mA** en série (repère F1, réf. `0ZRE0100FF1A`). Le fil pilote ne véhicule
qu'un signal de très faible courant : 100 mA suffisent largement au fonctionnement
normal, tout en coupant en cas de court-circuit accidentel entre le fil pilote et un
conducteur de puissance. Cette protection est **complémentaire** du disjoncteur amont,
pas un substitut.

### Récapitulatif des protections

| Protection | Repère | Rôle |
|------------|--------|------|
| Disjoncteur bipolaire 2 A (externe) | — | Protection rapprochée de l'alimentation carte + coupure phase/neutre |
| Fusible réarmable PPTC 230 V/100 mA | F1 | Protection des sorties fil pilote |
| Fusible entrée | F2 | Protection primaire du module d'alimentation |
| Varistance 560 V | RV1 | Écrêtage des surtensions secteur |
| Résistance de décharge 1 MΩ | R16 | Décharge du condensateur X2 (C5) au débranchement |

> ⚠️ Le disjoncteur bipolaire 2 A est un **élément externe** à installer dans le
> tableau ou le coffret : il ne fait pas partie de la carte. Ne mettez jamais la
> carte sous tension sans cette protection en place.

### Consignes d'intervention (mise hors tension)

Toute intervention de câblage se fait **hors tension**. Ne travaillez jamais sur le
tableau ou la carte sous tension.

1. **Coupure générale** : ouvrir le disjoncteur de branchement (interrupteur général),
   ou à défaut le disjoncteur bipolaire 2 A dédié à la carte.
2. **Vérification d'absence de tension (VAT)** : contrôler avec un vérificateur
   d'absence de tension (multimètre ou testeur dédié) qu'il n'y a **plus de 230 V**
   sur les bornes avant de toucher quoi que ce soit. Ne jamais se fier au seul fait
   d'avoir baissé le disjoncteur.
3. **Consignation** : si possible, condamner le disjoncteur en position ouverte et
   signaler l'intervention, pour éviter tout réarmement pendant que vous travaillez.
4. Ne rétablir le courant qu'une fois le câblage terminé, vérifié et la carte
   correctement fixée dans son boîtier.

### Raccordement de l'alimentation 230 V

- **Respecter la polarité au bornier d'entrée.** La phase issue du disjoncteur se
  raccorde sur la borne repérée **Ph** (entrée fusible F2) ; le neutre sur la borne N.
- Section des conducteurs 230 V : **1,5 mm² rigide** (H07V-U) pour l'alimentation.
- Serrer fermement les bornes et vérifier qu'aucun brin ne dépasse.

### Raccordement des sorties fil pilote (zones 1 et 2)

- Chaque fil pilote (zone 1, zone 2) se raccorde sur son bornier dédié.
- Section **1,5 mm² maximum**, en conducteur **rigide**. Le fil pilote ne véhicule
  qu'un signal de très faible courant : une section supérieure est inutile et gêne
  le raccordement au bornier.
- Le fil pilote de chaque radiateur (généralement le conducteur **noir** ou repéré)
  rejoint la sortie de la zone correspondante.
- Rappel : chaque sortie est protégée par le PPTC 230 V/100 mA (F1).

### Liaison Linky → gestionnaire (bus TIC)

- La liaison entre les bornes **I1 / I2** du compteur Linky et l'entrée TIC de la carte
  se fait avec un **câble rigide 0,5 mm², une paire**.
- **Le sens de branchement est indifférent côté Linky** : les deux bornes I1 et I2 de
  la sortie télé-information ne sont pas polarisées, on peut inverser les deux fils
  sans conséquence.
- Il s'agit d'un signal bas niveau : maintenir cette paire **séparée des conducteurs
  de puissance 230 V** autant que possible, pour limiter les perturbations sur la trame TIC.

---

## Architecture matérielle

### Composants

| Composant | Référence | Rôle |
|-----------|-----------|------|
| Microcontrôleur | ESP32-C6-DevKitC-1-N8 | CPU WiFi/MQTT/Web |
| Afficheur | TM1637 4 digits | Affichage zone/mode |
| EEPROM | 24C02 (I2C, 0x50) — ou 24C32 pour le planificateur | Persistance des états |
| Optocoupleurs | 2× MOC3041 par zone | Commande fil pilote 230 V |
| Détection secteur | LTV-814S + C5 (470 nF X2) | Présence secteur / synchro |
| Alimentation | HLK-10M05 (230 VAC → 5 V, 10 W) | Alimentation carte |
| Protection sorties | PPTC 230 V / 100 mA (F1) | Fil pilote |
| Décharge C5 | 1 MΩ (R16, face bottom) | Sécurité au débranchement |

> La nomenclature complète est disponible : **[BOM détaillée](hardware/BOM.md)**
> · version importable **[BOM.csv](hardware/BOM.csv)**.

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

### Schéma et carte

Schéma électronique de la carte principale :

![Schéma carte principale](hardware/main-board/schematic_main_board.png)

Carte principale assemblée :

![Carte principale](hardware/main-board/Heating-Control_main-board.jpeg)

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
#define EEPROM_TYPE_24C02   // 256 octets (défaut) — sans planificateur
// #define EEPROM_TYPE_24C32   // 4KB — active le planificateur hebdomadaire
```

> **24C02 (256 octets)** — suffisant pour la persistance des états de zones, des
> réglages Tempo et du délestage. Le **planificateur hebdomadaire est désactivé**
> (pas assez de mémoire pour stocker les profils horaires).
>
> **24C32 (4 Ko)** — active le planificateur hebdomadaire. Remplacement **pin-to-pin
> direct** de la 24C02 (même boîtier 8 broches, même adresse I2C 0x50, A0/A1/A2 à GND).
> Référence testée : **Microchip AT24C32E** (boîtier PDIP-8 : `AT24C32E-PUM`).
> Après remplacement de la puce et bascule du `#define`, effacer l'EEPROM au premier
> démarrage (bouton BOOT maintenu, ou depuis la page web) pour initialiser le nouveau
> plan mémoire.

### Compilation et upload

```bash
pio run --target upload
pio device monitor
```

---

## Interface Web

Accéder via `http://<LOCAL_IP>` depuis le réseau local.

- Contrôle manuel de chaque zone
- Visualisation de la puissance instantanée en temps réel
- Période Tempo du jour (Bleu/Blanc/Rouge)
- Compteurs de jours Tempo consommés sur la saison (rouges/blancs)
- Réglages du délestage automatique
- Planificateur hebdomadaire *(visible uniquement avec une EEPROM 24C32)*

<!-- Capture d'écran à ajouter : ![Interface Web du gestionnaire](docs/webui.png) -->

### Planificateur hebdomadaire (EEPROM 24C32)

La section **Calendrier de chauffage** de la page web n'apparaît que si le firmware
détecte une EEPROM 24C32. Avec une 24C02, cette section est automatiquement masquée.

Le planificateur repose sur des **profils réutilisables** : chaque profil définit une
journée complète en tranches de 30 minutes (48 créneaux), auxquelles on affecte un ordre
(STOP / HG / ECO / CONFORT / CM2). On associe ensuite un profil à chaque zone. L'éditeur
web permet de peindre les créneaux à la souris (clic = cycle d'ordre, glisser = remplir)
et de sauvegarder les profils et leurs affectations.

Le planificateur reste **non prioritaire** : une commande manuelle (bouton, web ou MQTT),
un délestage ou une règle Tempo prennent le pas sur l'ordre programmé.

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
│   ├── WebUI/            # Interface Web + serveur HTTP
│   └── ErriezTM1637/     # Pilote afficheur TM1637 (copie locale, MIT)
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
```

La bibliothèque **ErriezTM1637** (pilotage de l'afficheur) n'est **pas** déclarée dans
`lib_deps` : elle est fournie en **copie locale** dans le dossier `lib/ErriezTM1637/`
(licence MIT, source : <https://github.com/Erriez/ErriezTM1637>). PlatformIO la compile
directement, sans téléchargement.

> ⚠️ **Problème de chargement possible avec VS Code / PlatformIO**
>
> Selon les versions, PlatformIO ne résout plus la bibliothèque depuis le registre
> sous l'identifiant `erriez/ErriezTM1637` : le chargement des dépendances échoue, ou
> une autre bibliothèque `TM1637` entre en conflit (erreurs de compilation dans
> `DisplayManager` : constructeur, `begin()`, `writeData`).
>
> **Solution :** la bibliothèque est déjà embarquée dans `lib/ErriezTM1637/` — il n'y a
> donc rien à installer. Si vous aviez tenté d'ajouter une autre bibliothèque TM1637
> pendant vos essais, purgez le cache des dépendances puis recompilez :
>
> ```bash
> rm -rf .pio/libdeps        # Windows : rmdir /s /q .pio\libdeps
> pio run
> ```
>
> PlatformIO ne re-résout alors que `PubSubClient` et `ArduinoJson`, plus la copie
> locale d'ErriezTM1637 — sans aucun conflit.

---

## Licence

MIT — voir [LICENSE](LICENSE)

## Auteur

Denis Mattera — 2025

---

## Hardware

Le schéma, la nomenclature (BOM) et une vue de la carte principale sont fournis dans
le dossier `hardware/`. Les fichiers de fabrication Gerber ne sont pas publiés dans ce
dépôt ; le PCB nu est proposé à la commande (voir [Commander des cartes](#commander-des-cartes)).

```
hardware/
├── hardware_README.md                    # Notes de fabrication
├── BOM.md                                # Nomenclature (liens Mouser)
├── BOM.csv                               # Nomenclature (importable)
└── main-board/
    ├── schematic_main_board.png          # Schéma électronique
    └── Heating-Control_main-board.jpeg   # Vue de la carte
```

Le schéma et la BOM permettent de reproduire ou d'adapter la carte. Pour obtenir un
PCB prêt à l'emploi, voir la section [Commander des cartes](#commander-des-cartes).

---

## Commander des cartes

Ce projet représente 4 ans de développement, de prototypage et de tests en conditions réelles. Le firmware est open source — si vous souhaitez soutenir le projet ou gagner du temps, les cartes sont disponibles à la commande.

| Option | Contenu | Prix indicatif |
|--------|---------|----------------|
| **PCB nu** | Carte principale | 15€ |
| **Kit d'affichage** | 3 PCB (façade + TM1637/bargraphe + liaison/switches), boîtier DIN 6 modules — voir [`heating-2z-display-board`](https://github.com/Papymakers/heating-2z-display-board) | 15€ |

*Frais de port inclus. Expédition depuis la France.*

> ⚠️ **Rappel** : le PCB est vendu nu, sans composants. Son installation exige un
> **disjoncteur bipolaire 2 A** en amont (non fourni) et le respect des règles de
> [sécurité électrique](#sécurité-électrique). Vendu en l'état, pour intégration par
> une personne compétente.

📧 Commandes et questions : **support@papymakers.com** - https://papymakers.com/

---

## Contact & Support

- **Bug / question technique** → ouvrir une [Issue](../../issues)
- **Commandes** → support@papymakers.com
- **Discussions générales** → onglet [Discussions](../../discussions)
