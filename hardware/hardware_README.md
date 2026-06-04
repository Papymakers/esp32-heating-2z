# Hardware — Notes de fabrication

## Cartes

### main-board
Carte principale ESP32-C6 — contrôleur 2 zones fil pilote, lecture Linky TIC, EEPROM I2C, switches physiques, LED RGB.

### display-board
Afficheur déporté TM1637 4 digits — reçoit l'état des zones via MQTT et affiche le mode courant.

---

## Fabrication JLCPCB

| Paramètre | Valeur |
|-----------|--------|
| Layers | 2 |
| PCB Thickness | 1.6mm |
| Surface Finish | HASL (lead-free) |
| Copper Weight | 1oz |
| Min Hole Size | 0.3mm |
| Color | Green |

Uploader le fichier `gerbers.zip` de chaque carte directement sur [jlcpcb.com](https://jlcpcb.com).

---

## Fichiers disponibles

| Fichier | Description |
|---------|-------------|
| `schematic.svg` | Schéma électronique — lisible directement dans un navigateur |
| `pcb_top.svg` | Vue du PCB face composants |
| `gerbers.zip` | Fichiers Gerber pour fabrication JLCPCB |
| `BOM.csv` | Liste des composants compatible import JLCPCB |
| `schematic.json` | Source EasyEDA — importer via File → Import |

---

## Ouvrir les sources EasyEDA

1. Ouvrir [easyeda.com](https://easyeda.com)
2. **File → Import → EasyEDA**
3. Sélectionner le fichier `.json`

---

## Commander des cartes toutes faites

Voir la section [Commander des cartes](../README.md#commander-des-cartes) dans le README principal.  
📧 support@papymakers.com
