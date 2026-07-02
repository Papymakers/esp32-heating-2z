# Changelog

Toutes les évolutions notables de ce projet sont documentées dans ce fichier.

Le format s'appuie sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/),
et le projet suit un versionnage de type [SemVer](https://semver.org/lang/fr/)
adapté (suffixe `-2Z` pour la variante 2 zones / TM1637).

## [4.1-2Z] — 2025-07-01

### Ajouté
- **Planificateur hebdomadaire** documenté et activable avec une EEPROM 24C32.
  Profils réutilisables de 48 créneaux (30 min), affectation d'un profil par zone,
  édition à la souris depuis la WebUI. Reste non prioritaire face aux commandes
  manuelles, au délestage et aux règles Tempo.
- Compteurs de jours Tempo consommés sur la saison (rouges / blancs), sauvegardés
  en EEPROM, avec remise à zéro automatique au 1ᵉʳ novembre.
- Topic MQTT `heatingCtrl_v4/tempo/setCounters` pour corriger manuellement les
  compteurs Tempo (`{"red":N,"white":N}`).
- Section `docs/` avec instructions pour la capture d'écran de la WebUI.

### Modifié
- Le README précise désormais que le planificateur nécessite une 24C32
  (remplacement pin-to-pin direct de la 24C02, réf. testée `AT24C32E-PUM`).
- Affichage TM1637 : le digit d'état (WiFi / Linky) suit directement l'état des
  trames TIC (STX/ETX), sans temporisation. Affichage permanent (plus de mise en
  veille), pour que l'état reste toujours visible.
- Gestion des boutons : un bouton par zone (SW1 → zone 1, SW2 → zone 2), avec une
  fenêtre de confirmation non bloquante filtrant les impulsions parasites.
- Documentation corrigée : compteurs Tempo en EEPROM (et non NVS Flash),
  rafraîchissement WebUI par polling HTTP (et non WebSockets).

### Corrigé
- Commandes parasites au basculement HC→HP filtrées par la confirmation du niveau
  bas maintenu sur l'entrée bouton.
- Le délestage n'écrase plus une zone déjà à l'arrêt (STOP).
- La désactivation du mode Tempo restaure correctement la dernière commande
  enregistrée de chaque zone.

## [4.0-2Z] — 2025-05-26

### Ajouté
- Version initiale de la variante 2 zones avec afficheur TM1637.
- Pilotage fil pilote 2 zones (STOP, Hors-Gel, ECO, CONFORT, CM2).
- Lecture trame Linky TIC (mode historique).
- Gestion Tempo EDF (jours Bleu / Blanc / Rouge).
- Délestage automatique sur dépassement de puissance souscrite.
- Interface Web embarquée et communication MQTT.
- Persistance des états en EEPROM I2C (24C02 / 24C32).

[4.1-2Z]: https://github.com/papymakers/esp32-heating-2z/releases/tag/v4.1-2Z
[4.0-2Z]: https://github.com/papymakers/esp32-heating-2z/releases/tag/v4.0-2Z
