# Changelog

Toutes les évolutions notables de ce projet sont documentées dans ce fichier.

Le format s'appuie sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/),
et le projet suit un versionnage de type [SemVer](https://semver.org/lang/fr/)
adapté (suffixe `-2Z` pour la variante 2 zones).

## [5.1.6-2Z] — 2026-10-21

Refonte du firmware depuis la 4.1-2Z. Notes techniques détaillées (supervision,
carte mémoire EEPROM, afficheurs, NTP, historique des révisions 5.0 à 5.1.6) :
[docs/firmware-v5.md](docs/firmware-v5.md).

> **Incompatible avec la 4.x** : nouvelle carte mémoire EEPROM (en-tête, enregistrements
> alignés sur 16 octets). Au premier démarrage, l'EEPROM est formatée : commandes de
> zone à refaire, compteurs Tempo à remettre par `heatingCtrl_v4/tempo/setCounters`.
> La 24C02 n'est plus supportée (adressage 8 bits) : EEPROM 24C32 minimum.

### Ajouté
- **Supervisor** : boucle coopérative ordonnée par priorité, compteurs par tâche
  (appels, travaux, échecs, durées, charge CPU, dépassements de budget), santé des
  sous-systèmes Linky / EEPROM / WiFi / MQTT avec modes de repli, journal
  d'événements. Accès : MQTT `heatingCtrl_v4/health` (60 s), `GET /api/health`,
  `GET /api/trace`. Watchdog sur la boucle, cause du dernier redémarrage publiée.
- **Logger** : niveaux ERROR / WARN / INFO / DEBUG pour la série et le MQTT, réglables
  à la compilation (`include/log_config.h`) et à chaud (`heatingCtrl_v4/log/set`,
  `POST /api/config/log`), réglage conservé en NVS.
- **Heure par NTP** (fuseau Europe/Paris, heure d'été gérée) : date, heure et jour de
  semaine pour le calendrier, l'index journalier et les compteurs Tempo.
- **Afficheur OLED SH1106 1,3"** en plus du TM1637, **détecté au démarrage** (OLED
  prioritaire, fonctionnement sans afficheur possible) ; veille de l'OLED après 60 s,
  premier appui = réveil seul, réveil au début d'un délestage.
- **Index Linky confirmé par ACK** du collecteur (`newHeating/IdxCompteur/ack`) avec
  nouvelle tentative chaque minute jusqu'à minuit, et forçage manuel
  (`newHeating/IdxCompteur/request`) — voir Papymakers/energy-index-logger.
- **Calendrier : dérogation manuelle** — une commande manuelle (switch, WebUI, MQTT)
  tient jusqu'au prochain changement de commande du calendrier pour la zone.
- WebUI : bouton **↻ Redémarrer** (`POST /api/reboot`) et **messages de confirmation**
  (enregistrements, activations, refus, absence de réponse de l'ESP32).
- `/api/state` : type d'afficheur, puce EEPROM, identifiant MQTT, état NTP, heure.
- Lecture facultative du n° de série et de l'ID fabricant (24CS512) : traçabilité et
  détection d'un changement de module.

### Modifié
- **EEPROM** : toute puce à adressage 16 bits de la **24C32 à la 24C512** (24LC32A,
  AT24C32, 24CS512, 24LC512, AT24C512…) ; formatage automatique d'une puce vierge ou
  étrangère ; écriture par pages de 32 octets avec attente de fin de cycle (ACK polling).
- **LED RGB** (GPIO8) réservée à l'état de l'EEPROM : verte = OK, bleue quelques
  secondes = formatée ou module changé, rouge clignotante = absente ou en erreur.
- **Identifiant client MQTT** dérivé de l'adresse MAC (`naucrate-2z-xxxxxx`) au lieu de
  `heatingCtrl_v4` fixe ; les topics ne changent pas.
- **Calendrier** : il n'est plus seulement « non prioritaire face aux commandes
  manuelles » (voir dérogation) ; activation mémorisée en EEPROM, nouvelles
  affectations appliquées immédiatement.
- Compteurs Tempo remis à zéro au **1ᵉʳ septembre** (début de l'année Tempo) au lieu
  du 1ᵉʳ novembre.
- Délestage : seules les zones qui chauffent plus que la commande de repli sont
  délestées (extension de la correction 4.1 sur les zones à l'arrêt).
- Publication Linky périodique indépendante de l'heure.
- Environnement PlatformIO unique : `naucrate-2z` (ajout de la bibliothèque U8g2).
- Identifiants WiFi déplacés dans `include/secrets.h` (exclu de Git), modèle
  `include/secrets.example.h`.

### Corrigé
- Zones bloquées en hors-gel après la fin d'une restriction Tempo.
- Séquence Confort −2 °C (CM2) inversée (le radiateur restait en pratique en Éco) ;
  cycle CM2 relancé après un délestage.
- Indicateur de surcharge figé en cas de perte de la TIC ; délestage permanent si
  ISOUSC n'était pas encore reçu.
- Checksum des lignes TIC de nouveau vérifié (il était désactivé).
- Boucle bloquée plusieurs secondes par les tentatives de connexion MQTT (risque de
  débordement de la réception TIC) : délais courts et reconnexion espacée.
- Zones non restaurées au démarrage en l'absence de broker MQTT.
- Calendrier désactivé après chaque redémarrage (activation jamais enregistrée).
- Caractères parasites à la place des noms de profils vides dans la WebUI.
- Jour de semaine inconnu (le profil du lundi était appliqué tous les jours).
- Divers : version firmware tronquée, tampons JSON sur la pile, JSON de log invalide
  avec guillemets, messages MQTT trop longs traités vides.

### Supprimé
- Abonnement au topic `utcClock` (horloge maître) : remplacé par NTP.
- Support de la 24C02.
- Signalisation de l'état réseau / Linky par la LED RGB (voir OLED, logs, `/api/health`).

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

[5.1.6-2Z]: https://github.com/papymakers/esp32-heating-2z/releases/tag/v5.1.6-2Z
[4.1-2Z]: https://github.com/papymakers/esp32-heating-2z/releases/tag/v4.1-2Z
[4.0-2Z]: https://github.com/papymakers/esp32-heating-2z/releases/tag/v4.0-2Z
