# Naucrate 2 zones — notes techniques du firmware v5

Ce document détaille les évolutions résumées dans l'entrée `[5.1.6-2Z]` du [CHANGELOG](../CHANGELOG.md) : état actuel, puis historique des révisions internes 5.0 à 5.1.6 (non publiées séparément), de la plus récente à la plus ancienne.

## État actuel : v5.1.6-2Z

Gestionnaire de chauffage fil pilote 2 zones sur ESP32-C6-DevKitC-1-N8 : lecture TIC Linky, Tempo EDF, délestage, calendrier, MQTT et interface web.

| | |
|---|---|
| EEPROM I2C | **24C32 à 24C512** (adressage 16 bits, page déclarée 32 o) : 24LC32A, AT24C32, 24C64…24C256, 24CS512, 24LC512, AT24C512. N° de série et ID fabricant facultatifs. **24C02 non compatible.** |
| Afficheur | OLED SH1106 1,3" (I2C 0x3C/0x3D) ou TM1637, détecté au démarrage ; fonctionnement sans afficheur possible |
| Heure | NTP, fuseau Europe/Paris |
| Calendrier | 8 profils, créneaux de 30 min, activation mémorisée ; dérogation manuelle jusqu'au prochain changement du calendrier |
| Linky | index journalier confirmé par ACK du collecteur + forçage manuel (`newHeating/IdxCompteur/ack` et `/request`, cf. Papymakers/energy-index-logger) |
| Supervision | Supervisor (`heatingCtrl_v4/health`, `/api/health`, `/api/trace`) et Logger réglable à chaud (`heatingCtrl_v4/log/set`) |
| MQTT | identifiant `naucrate-2z-xxxxxx` (MAC) ; topics `heatingCtrl_v4/...` et `newHeating/...` inchangés |
| Environnement PlatformIO | `naucrate-2z` |

**Validé sur site** (carte 2 zones, EEPROM 24LC32A neuve) : formatage et relecture de l'EEPROM, synchronisation NTP, connexion MQTT, santé du Supervisor (tous sous-systèmes OK), calendrier (activation, application immédiate du créneau, commande STOP appliquée), messages de confirmation de la WebUI.

**Reste à observer en fonctionnement** : séquence Confort −2 (CM2) sur radiateur, délestage réel, sortie d'une journée rouge Tempo, publication des index à 23 h, fin de dérogation manuelle, afficheur OLED.

Les mentions « statut » et « à valider » des sections suivantes sont celles de la date de chaque révision.

---

# Naucrate 2 zones — v5.1.6-2Z

Calendrier : **dérogation manuelle** jusqu'au prochain changement du calendrier.

**Avant.** Une commande manuelle (switch, WebUI, MQTT) donnait à la zone la priorité « utilisateur » ; le calendrier, de priorité inférieure, était ensuite refusé sur cette zone jusqu'à un redémarrage ou un délestage.

**Maintenant.**
- La commande manuelle tient jusqu'à ce que le calendrier **change de commande** pour la zone. Exemple avec un profil STOP de 6 h à 22 h puis ECO : un CONF manuel à 18 h tient jusqu'à 22 h, où la zone repasse en ECO. Les créneaux de 30 min à commande identique ne lèvent pas la dérogation.
- Une application forcée lève aussi les dérogations : activation du calendrier, ou enregistrement des assignations.
- Tempo (HG forcé) et délestage, de priorité supérieure, ne sont jamais levés par le calendrier.
- Fin de dérogation signalée sur `heatingCtrl_v4/log` : « Z2 : fin de dérogation manuelle (CONF), retour au calendrier : ECO ». La zone passe alors à `"prio":1` sur `zone/N/cmd`.
- Inchangé : la commande manuelle reste mémorisée en EEPROM ; après un redémarrage, elle est restaurée puis le calendrier reprend la main au premier tick d'horloge.

**À valider sur carte.** Mettre manuellement une zone pilotée par le calendrier dans un autre mode : `prio` 2 jusqu'au prochain changement de commande du profil, puis message de fin de dérogation et `prio` 1.

---

# Naucrate 2 zones — v5.1.5-2Z

WebUI : **messages de confirmation** (bandeau en bas de page, 3 s ; 5 s pour une erreur).

- Vert ✓ quand l'ESP32 a accepté : profil enregistré (« Profil « Normal » enregistré »), assignations enregistrées, calendrier / Tempo / délestage activé ou désactivé, règle Tempo, configuration délestage, restauration forcée, nombre de zones.
- Rouge ✗ si l'ESP32 refuse (message renvoyé par l'ESP32) ou ne répond pas (« Pas de réponse de l'ESP32 »).
- Bleu : « Redémarrage en cours… » ; effacement EEPROM : « EEPROM effacée — redémarrage » ; la page se recharge seule au bout de 8 s.
- Commande de zone (boutons STOP / HG / ECO / CONF / CM2) : rien si elle est acceptée (l'état affiché change), message rouge si elle est refusée (priorité Tempo / délestage, zone verrouillée).
- Les principaux messages d'erreur renvoyés par l'ESP32 sont traduits en français.

Une seule fonction `apiPost()` remplace les `fetch` dispersés ; syntaxe JavaScript de la page vérifiée, logique des messages testée (succès, refus, absence de réponse).

---

# Naucrate 2 zones — v5.1.4-2Z

Corrections du **calendrier** (défauts présents depuis la v4, relevés en test sur site).

1. **Calendrier inactif après chaque redémarrage.** La case « calendrier actif » de la WebUI n'était jamais enregistrée : `saveSysFlags()` n'était appelée nulle part, et le drapeau relu au démarrage valait toujours « inactif ». Désormais, chaque enregistrement depuis la WebUI mémorise ce seul bit en EEPROM (`saveScheduleFlag()`, lecture-modification-écriture, pas d'écriture si inchangé). Tempo et délestage gardent leur configuration propre ; leurs bits dans l'octet de flags restent neutres.
2. **Nouvelles affectations sans effet avant le créneau suivant.** L'activation était traitée avant les affectations zone/jour, et rien n'était réappliqué si le calendrier était déjà actif. Les affectations sont maintenant enregistrées d'abord, puis le créneau en cours est appliqué immédiatement.
3. **Application au démarrage avant l'heure connue.** Le calendrier activé au démarrage appliquait le créneau « lundi 00:00 » avant la synchronisation NTP, ce qui pouvait faire refuser la restauration des zones depuis l'EEPROM. L'application attend maintenant le premier tick d'horloge.

**Inchangé (à décider).** Une commande manuelle (switch, WebUI, MQTT) bloque le calendrier sur la zone jusqu'au redémarrage ou à un délestage. Avec la correction 1, un redémarrage rend bien la main au calendrier.

**À valider sur carte.** Cocher le calendrier et enregistrer, redémarrer avec le bouton « ↻ Redémarrer » : la case reste cochée, et chaque zone affectée reçoit sa commande du créneau en cours (`zone/N/cmd` avec `source: SCHEDULE`) dans la minute qui suit la synchronisation NTP.

---

# Naucrate 2 zones — v5.1.3-2Z

Ajout : **bouton « ↻ Redémarrer »** dans la WebUI (carte de configuration, au-dessus de « Effacer EEPROM + Redémarrer »).

- Redémarrage simple de l'ESP32 après confirmation, sans toucher à l'EEPROM ; la page se recharge seule au bout de 8 s.
- Route HTTP `POST /api/reboot` avec le corps `{"confirm":true}` (refus 400 sans confirmation) ; log MQTT `WARN WEB Reboot requested via HTTP`.
- Utile notamment pour rendre la main au calendrier après une commande manuelle (voir ci-dessous).

**Comportement connu (existant depuis la v4, non modifié ici).** Une commande manuelle (switch, WebUI, MQTT) donne à la zone la priorité « utilisateur » ; le calendrier, de priorité inférieure, est ensuite refusé sur cette zone jusqu'à un redémarrage ou un délestage (log `Zone N priority too low (1 <= 2) … refused`). Une dérogation limitée (jusqu'au prochain changement du calendrier, ou durée fixe) est à décider.

---

# Naucrate 2 zones — v5.1.2-2Z

Correction : **caractères parasites dans le calendrier** sur une EEPROM neuve ou formatée.

**Cause.** `StorageManager::loadProfile()` lisait le nom du profil directement dans la structure du profil, *avant* de vérifier si l'emplacement était vide. Sur une EEPROM formatée (0xFF), le nom par défaut « ProfileN » était remplacé par 11 octets 0xFF, que la WebUI affichait comme une suite de caractères parasites. Défaut présent depuis la v4 (avec une 24C32) ; l'EEPROM elle-même n'est pas en cause, aucun formatage n'est nécessaire.

**Corrections.**
- Le nom est lu dans un tampon local et n'est recopié que s'il est valide (pas de caractère de contrôle ni d'octet 0xFF). Un emplacement vide garde le nom par défaut « Profile0 » … « Profile7 », profil inactif.
- Les noms accentués saisis dans la WebUI restent acceptés (UTF-8).
- Saisie d'un nom : la troncature à 11 octets ne coupe plus un caractère accentué en deux ; un nom vide devient « ProfileN » (un nom vide aurait été relu comme un emplacement libre, donc perdu au redémarrage).

**À valider sur carte :** après téléversement, le calendrier affiche Profile0 … Profile7 ; créer un profil au nom accentué (« Séjour »), redémarrer, vérifier qu'il est relu à l'identique.

---

# Naucrate 2 zones — v5.1.1-2Z

Révision de la v5.1-2Z : **EEPROM 24C32 acceptée** (et toute la famille à adressage 16 bits jusqu'à la 24C512). Aucun autre changement de comportement.

| | v5.1-2Z | v5.1.1-2Z |
|---|---|---|
| EEPROM acceptées | 24CS512, 24LC512, AT24C512 | **24C32** (24LC32A, AT24C32…) à 24C512 |
| Page déclarée (`EEPROM_PAGE`) | 128 o | **32 o** |
| Taille annoncée (`EEPROM_SIZE`) | 65 536 o | 4 096 o (minimum requis, la puce peut être plus grande) |
| Attente fin d'écriture (`_waitReady`) | ≤ 10 ms | ≤ 20 ms (anciennes AT24C32) |
| Calendrier | oui | oui, dès la 24C32 (carte mémoire = 1 Ko) |

**Pourquoi.** Une 24C32 a des pages de 32 octets. Avec une page déclarée de 128 octets, le formatage (blocs de 64 octets) aurait franchi des limites de page et rebouclé dans la page, laissant une partie de la zone non formatée. `_write()` découpe à la limite de page déclarée : 32 octets convient à toute la famille, les puces à pages de 64 ou 128 octets écrivent simplement en plus de fois (formatage ~0,2 s au lieu de ~0,05 s, une seule fois).

**Libellés.** `chipName()` renvoie « EEPROM (sans ID) » ou « EEPROM (ID inconnu) » au lieu de « 512 Kbit (…) » ; le log de démarrage annonce « >= 4096 bytes ».

**Non compatible :** la 24C02 (adressage 8 bits).

**Premier démarrage** sur une 24C32 déjà utilisée par la v4 : contenu non reconnu → formatage (LED bleue quelques secondes). Les commandes de zone sont à refaire aux switches, les compteurs Tempo à remettre par `heatingCtrl_v4/tempo/setCounters`.

**À valider sur carte** (en plus du §8 de la v5.1) : formatage d'une 24C32 neuve, puis lecture après redémarrage des commandes de zone et d'un profil de calendrier ; LED verte ; `eepromChip` = « EEPROM (sans ID) » dans `/api/state`.

---

# Naucrate 2 zones — ESP32 Heating Controller v5.1-2Z

Gestionnaire de chauffage fil pilote 2 zones, ESP32-C6-DevKitC-1-N8, EEPROM I2C 512 Kbit (étendu à la 24C32 en v5.1.1), afficheur **OLED SH1106 ou TM1637 détecté au démarrage**, heure par **NTP**.

Cette version succède à la v5.0-2Z (détail plus bas). Un seul binaire sert désormais les deux cartes d'affichage, la carte sans afficheur, et les trois références d'EEPROM 512 Kbit. L'heure ne dépend plus d'une horloge maître sur le réseau.

> **Statut** : le code compile sans avertissement sur les sources du projet, y compris avec `-Wall -Wextra` (core Arduino-ESP32 3.0.1 Tasmota, GCC RISC-V 12.2). RAM 41 012 o (+832 o), flash 728 090 o (+17,8 Ko, essentiellement U8g2 et deux polices). Il n'a **pas encore été testé sur carte** : voir §8.

---

## 1. Compilation : un seul environnement

| | v5.0-2Z | v5.1-2Z |
|---|---|---|
| Environnement PlatformIO | `esp32-c6-devkitc-1` (TM1637 seul) | **`naucrate-2z`** (OLED ou TM1637, détection) |
| Bibliothèques | PubSubClient, ArduinoJson, ErriezTM1637 | + **U8g2** (`olikraus/U8g2 @ ^2.35.30`) |
| Option de compilation | — | `-DU8X8_DO_NOT_SET_WIRE_CLOCK` |

L'option U8g2 empêche la bibliothèque de reprogrammer l'horloge I2C à chaque transfert : le bus est partagé avec l'EEPROM et déjà réglé à 400 kHz par `Wire.begin()`.

## 2. Afficheur détecté au démarrage (`lib/DisplayManager`)

Ordre de détection, une seule fois au démarrage (brancher un afficheur à chaud demande un redémarrage) :

1. **OLED SH1106** 1,3" 128×64 sur le bus I2C de l'EEPROM (SDA 18 / SCL 19), adresse 0x3C puis 0x3D.
2. **TM1637** (CLK 23 / DIO 22) : envoi de la commande 0x40 et lecture de l'acquittement du 9e coup d'horloge, DIO relâchée avec le pull-up interne. Sans TM1637, la ligne reste haute.
3. **Aucun** : fonctionnement sans afficheur, toutes les méthodes d'affichage sont inertes.

Si les deux répondent, l'OLED est prioritaire. Le type détecté figure dans le log de démarrage MQTT et dans `/api/state` (`display`).

### 2.1 OLED (U8g2, tampon d'une page, rendu page par page)

| Ligne | Contenu |
|---|---|
| L1 | `WiFi OK/-- MQTT OK/--` + heure `HH:MM` (`--:--` tant que NTP n'a pas répondu) |
| L2 | `Z1` + commande réellement appliquée + repères |
| L3 | `Z2` + commande réellement appliquée + repères |
| L4 | couleur Tempo + période + Iinst (`--A` si la TIC est muette) ; en délestage : `DELEST xxA` en vidéo inverse clignotante (500 ms) |

Commandes affichées : ARRET, HORS-GEL, ECO, CONFORT, CONF -2 (BLOQUEE en défaut système). Repères alignés à droite : **T** = HG forcé par Tempo (priorité `PRIO_TEMPO`), **D** = zone délestée (état OVERRIDE ou RESTORE), **~** = Confort −2 (CM2).

La couleur et la période viennent du PTEC et s'affichent même si l'option Tempo (HG forcé) est désactivée.

**Veille** après `OLED_SLEEP_SEC` = 60 s (`setPowerSave`), comme l'AstroClock. Réveil :
- par un switch : **le premier appui ne fait que réveiller l'écran**, sans changer de commande ;
- au début d'un délestage. L'écran ne se met pas en veille pendant un délestage ; le délai de 60 s repart à sa fin.

Un rendu complet prend environ 25 ms ; il est limité à un toutes les 200 ms et n'a lieu que si une donnée affichée a changé.

### 2.2 TM1637

Rendu identique à la v5.0 (une LED par commande sur les digits 0/1, WiFi + Linky sur le digit 4). Veille désactivée par défaut (`TM1637_SLEEP_SEC` = 0), comme avant.

### 2.3 Switches

Inchangé : SW1 = zone 1, SW2 = zone 2, avec les deux afficheurs. Seul ajout : l'appui de réveil décrit au §2.1.

## 3. Heure par NTP (nouveau module `lib/ClockManager`)

**Avant** : l'heure arrivait par le topic MQTT `utcClock` (horloge maître). Sans ce message, le calendrier, l'index journalier de 23 h, le comptage Tempo **et la publication Linky de chaque minute** ne fonctionnaient pas.
**Après** : SNTP (`configTzTime`) avec la règle POSIX Europe/Paris `CET-1CEST,M3.5.0,M10.5.0/3` : date, heure légale et jour de semaine, changements été/hiver gérés par la libc. Serveurs `fr.pool.ntp.org` puis `pool.ntp.org`, modifiables dans `config.h` (`NTP_SERVER1` peut pointer vers un serveur NTP local).

- L'abonnement `utcClock` et son traitement sont supprimés.
- ClockManager génère un tick par minute vers `LinkyReader::onClock` (index 23 h), `ScheduleManager` (créneaux, jour de semaine) et `TempoManager` (compteurs à minuit, saison au 1er septembre).
- Une fois synchronisée, l'heure continue de tourner sur le quartz même si Internet disparaît ; lwIP resynchronise toutes les heures. Après un redémarrage sans Internet, aucun tick n'est émis tant que l'heure est inconnue : calendrier, index et comptage Tempo attendent la première synchronisation.
- Le champ `ts` des publications MQTT garde sa signification : secondes depuis minuit, heure locale.
- Nouvelle tâche supervisée `clock` (budget 30 ms).

## 4. Publication Linky indépendante de l'heure

`_publishValues()` est cadencée par `millis()` toutes les 60 s dans `LinkyReader::update()`. Auparavant elle n'était appelée que depuis `onClock()`, donc uniquement si `utcClock` arrivait.

## 5. EEPROM 512 Kbit générique

> Depuis la v5.1.1, toute EEPROM de la 24C32 à la 24C512 est acceptée (page déclarée 32 o) et les libellés « 512 Kbit (…) » deviennent « EEPROM (…) ».


- Taille fixe 64 Ko, page 128 o, adressage 16 bits, **sans détection de taille**. Carte mémoire v1 inchangée : une EEPROM écrite en v5.0 est relue telle quelle.
- Références acceptées : **24CS512**, **24LC512**, **AT24C512**.
- L'ID fabricant (0x7C) et le n° de série (registre 0x58) sont **facultatifs** : lus s'ils existent, jamais exigés. Une 24LC512 n'est plus signalée comme « puce inattendue », et l'absence de registre de sécurité n'incrémente plus le compteur d'erreurs I2C.
- Sans n° de série, le remplacement du module n'est pas détectable (le formatage d'une puce vierge, lui, l'est toujours).
- Nouveau `StorageManager::chipName()` : « 24CS512 », « 24CSxxx », « 512 Kbit (ID inconnu) » ou « 512 Kbit (sans ID) » ; publié dans le log de démarrage et `/api/state` (`eepromChip`).
- L'option de compilation 24C32 est retirée.

## 6. LED RGB (GPIO8) réservée à l'EEPROM

| État | LED |
|---|---|
| Démarrage | éteinte |
| EEPROM OK | verte fixe |
| EEPROM formatée au démarrage ou module changé (n° de série) | bleue `LED_INFO_MS` = 4 s, puis verte |
| EEPROM absente ou en erreur (supervision DEGRADED) | rouge clignotante (500 ms) |

La supervision réseau et Linky ne passe plus par la LED (l'ancien orange « MQTT absent » et le rouge « Linky muet » sont supprimés) : ligne 1 de l'OLED, logs MQTT et `/api/health`.

## 7. MQTT et API

- **Identifiant client** : `naucrate-2z-xxxxxx` (3 derniers octets de la MAC WiFi), au lieu de `heatingCtrl_v4` fixe. Deux modules sur le même broker ne se déconnectent plus mutuellement.
- **Topics inchangés** (`heatingCtrl_v4/...`, `heatingCmd`…) pour ne pas casser les tableaux de bord existants.
- Log de démarrage enrichi : `Boot fw=… reset=… eeprom=<puce>/<n° série> display=<type> id=<client>`.
- `/api/state` : nouveaux champs `eepromChip`, `display`, `mqttId`, `ntpSynced`, `time`.

## 8. Points à valider sur carte

1. Détection OLED 0x3C et 0x3D, puis TM1637, puis aucun afficheur (même binaire).
2. Bus I2C partagé : écriture EEPROM (appui switch) pendant un rendu OLED, aucune erreur I2C dans `/api/health`.
3. Durée réelle de la tâche `screen` avec l'OLED (budget 40 ms).
4. Veille OLED à 60 s ; premier appui = réveil seul ; réveil au début d'un délestage ; bandeau `DELEST` clignotant.
5. Première synchronisation NTP avec l'IP fixe et le DNS 192.168.1.20 (log `[CLK] NTP first sync`).
6. Redémarrage sans Internet : pas de tick, `--:--` sur l'OLED, publication Linky maintenue.
7. Passage à l'heure d'hiver (25/10/2026) : créneaux du calendrier et index de 23 h corrects.
8. LED : verte avec une 24CS512, une 24LC512 et une AT24C512 ; bleue avec une puce vierge ; rouge clignotante module retiré.
9. Identifiant MQTT visible dans le log du broker (`mosquitto`).

**Côté installation** : `utcClock` n'est plus lu par ce firmware. Le script `astro_clock.py` reste utile aux autres clients (AstroClock, CLOCK SAVER).

---

# ESP32 Heating Controller — v5.0-2Z

Gestionnaire de chauffage fil pilote 2 zones, ESP32-C6-DevKitC-1-N8, afficheur TM1637, EEPROM I2C **24CS512**.

Cette version succède à la v4.0-2Z (EEPROM 24C02). Elle corrige plusieurs défauts de fonctionnement, ajoute une supervision des tâches avec trace, centralise la gestion des traces série et MQTT, et passe à l'EEPROM 24CS512 avec exploitation de son numéro de série.

> **Statut** : le code compile sans avertissement avec la chaîne de la plateforme (core Arduino-ESP32 3.0.1 Tasmota, GCC RISC-V 12.2). Il n'a **pas encore été testé sur carte**. Les points à valider sont listés en fin de document.

---

## 1. Matériel et compatibilité

| | v4.0-2Z | v5.0-2Z |
|---|---|---|
| EEPROM | 24C02 (256 o, adresse 8 bits) | **24CS512** (64 Ko, adresse 16 bits, page 128 o, ECC, n° de série) |
| Alternative | — | 24C32 (4 Ko) via `config.h` |
| Calendrier persistant | non (désactivé) | **oui** |
| Version firmware | `4.0-3Z` (erronée) | `5.0-2Z` |

**Incompatibilité volontaire** : la carte mémoire EEPROM change (enregistrements alignés sur 16 octets, en-tête de format). Une carte équipée d'une 24C02 doit rester en v4 : la compilation v5 avec une 24C02 s'arrête sur un `#error` explicite.

**Inchangé** : brochage, topics MQTT existants (`heatingCtrl_v4/...`, `newHeating/...`), client MQTT `heatingCtrl_v4`, routes HTTP existantes, page web.

---

## 2. Corrections de défauts

### 2.1 Zones bloquées en HG après une période Tempo (critique)
**Avant** : au passage HP → HC (ou jour rouge → bleu), `_applyTempoRules()` testait `_state.forceHG` alors qu'il venait d'être remis à `false` : la restauration EEPROM n'était jamais lancée. Les zones restaient en HG avec la priorité `PRIO_TEMPO`, et toutes les commandes utilisateur suivantes étaient refusées (« priority too low »).
**Après** : l'état précédent (`wasForced`) est transmis ; à la levée de la restriction, `_releaseZones()` rend la priorité des zones tenues par Tempo puis `handleRecovery()` restaure la commande EEPROM.

### 2.2 Désactivation de Tempo : zones en délestage cassées
**Avant** : `setEnabled(false)` forçait toutes les zones en `NORMAL` / `PRIO_DEFAULT`, y compris une zone en cours de délestage ou verrouillée.
**Après** : seules les zones tenues par Tempo sont libérées. Une zone en délestage dont la commande sauvegardée est le HG imposé par Tempo reçoit la commande EEPROM à restaurer.

### 2.3 Séquence Confort −2 °C (CM2) inversée — *à confirmer sur radiateur*
Norme fil pilote 6 ordres : Confort −2 °C = **7 s d'alternance complète** puis **293 s sans signal**.
**Avant** : 7 s CONF (sans signal) puis 293 s ECO (alternance complète) — le radiateur était en pratique en Éco.
**Après** : séquence conforme. Les commentaires de `types.h` qui inversaient ECO et CONF sont corrigés (le câblage et le pilotage des MOC étaient, eux, corrects).

### 2.4 CM2 perdu après un délestage
**Avant** : la restauration appliquait `applyHardware(CM2)` → ECO fixe, timer non relancé.
**Après** : `_startCmd()` commun à l'application et à la restauration relance le timer CM2.

### 2.5 Délestage qui augmente la consommation
**Avant** : toute zone non STOP passait au fallback (ECO par défaut), y compris une zone en HG.
**Après** : seules les zones qui chauffent plus que le fallback sont délestées (ordre STOP < HG < ECO < CM2 < CONF).

### 2.6 Perte de la TIC pendant une surcharge
**Avant** : le flag de surcharge restait figé : cycle délestage / restauration forcée toutes les 5 min, indéfiniment.
**Après** : la supervision détecte l'absence de trames et appelle `OverloadManager::onDataLost()`. Au retour des trames, le délestage est resynchronisé sur la mesure courante.

### 2.7 Checksum TIC désactivé
**Avant** : validation commentée ; un parasite pouvait produire un faux PTEC (jour rouge) ou un faux IINST (délestage).
**Après** : checksum vérifié (méthode 1 du mode historique, méthode 2 tolérée), y compris le cas où le checksum vaut un espace (0x20), que l'ancien découpage sur les espaces ne gérait pas. Les lignes rejetées sont comptées (`getBadLineCount()`).
Garde supplémentaire : pas de décision de surcharge tant que `ISOUSC` n'est pas connu (valait 0 → surcharge immédiate).

### 2.8 Blocages de la boucle par MQTT
**Avant** : délais par défaut de PubSubClient / WiFiClient : 3 s de connexion TCP + jusqu'à 15 s d'attente CONNACK, retentés toutes les 5 s avec un broker absent. Le buffer UART de la TIC (256 o ≈ 2 s à 1200 bauds) débordait.
**Après** : connexion TCP limitée à 1,5 s, attente CONNACK à 2 s, reconnexion à backoff exponentiel 5 s → 60 s, buffer RX TIC porté à 1024 o (≈ 8 s).

### 2.9 Zones non restaurées sans broker au démarrage
**Avant** : `handleRecovery()` n'était appelée qu'à la première connexion MQTT ; broker absent → zones en ECO indéfiniment.
**Après** : restauration EEPROM dans `setup()`. L'état complet est publié à **chaque** (re)connexion MQTT.

### 2.10 Divers
- Buffers JSON de 1 à 4 Ko sur la pile de `loopTask` (8 Ko) → heap (`String`) ou statique ; pile de la boucle portée à 12 Ko (`SET_LOOP_TASK_STACK_SIZE`).
- Version firmware tronquée (buffers de 20 octets pour 21 caractères) → 32 octets.
- Afficheur : il montrait STOP pendant un délestage (fallback ECO) et gardait HG si Tempo était désactivé en pleine restriction → il affiche la commande réellement appliquée.
- Saison Tempo remise à zéro au 1er novembre → **1er septembre** (année Tempo : 1/09 → 31/08).
- Champ `lastDebounce` écrit en ISR désormais `volatile`.
- Écriture EEPROM : attente de fin de cycle par *ACK polling* au lieu d'un `delay(6)` fixe ; erreurs I2C comptées.
- `publishLog` : guillemets et antislashs neutralisés (JSON invalide auparavant).
- Messages MQTT > 255 octets : rejetés avec trace au lieu d'être traités vides.
- Identifiants WiFi sortis de `config.h` vers `include/secrets.h` (exclu de Git) ; modèle `secrets.example.h`.

---

## 3. Supervision des tâches (nouveau module `lib/Supervisor`)

### 3.1 Boucle ordonnée par priorité
```
1. LINKY → OVERLOAD → ZONES → SYSFSM   mesure et sécurité électrique
2. INPUTS                               interrupteurs, bouton BOOT
3. SCREEN → WEB                         interface locale
4. WIFI → MQTT                          réseau (peut être lent, en dernier)
```
Chaque tâche est exécutée par `supervisor.run(tâche, lambda)` et renvoie :

| Résultat | Signification | Exemples |
|---|---|---|
| `IDLE` | rien à faire à ce tour | pas d'octet TIC reçu |
| `DONE` | travail effectué | trame complète, commande appliquée, page servie |
| `FAIL` | échec | checksum TIC faux, reconnexion MQTT ratée, commande refusée, erreur EEPROM, WiFi absent |

### 3.2 Trace par tâche
Pour chaque tâche : appels, travaux, échecs, temps cumulé, moyenne, maximum, dépassements de budget, charge CPU (‰). Deux jeux de compteurs : cumul depuis le démarrage et fenêtre de 60 s.

Budgets (durée normale maximale d'un appel) : linky 5 ms, overload/zones/sysfsm/wifi 2 ms, screen 5 ms, inputs 25 ms, mqtt 30 ms, web 80 ms. Un dépassement est un indicateur, pas une erreur. Un blocage > 1 s est remonté en log MQTT (au plus une fois par minute).

En série (niveau DEBUG), tableau toutes les 60 s (valeurs d'exemple) :
```
[SUP]  ---- tâches sur 60s : 412337 tours, boucle max 41250 us ----
[SUP]  tâche       appels   trav échec   moy µs   max µs  ‰ CPU
[SUP]  linky       412337     42     0        6      812      41
...
```

### 3.3 Santé des sous-systèmes et modes de repli

| Sous-système | Contrôle | Dégradé après | Repli |
|---|---|---|---|
| Linky | trame complète < 15 s | 2 contrôles (≈ 10 s) | délestage suspendu, LED rouge, log ERROR |
| EEPROM | sonde I2C + absence d'erreur, toutes les 60 s | 2 contrôles | zones maintenues en RAM, log ERROR |
| WiFi | association | 6 contrôles (≈ 30 s) | fonctionnement local |
| MQTT | connexion broker | 6 contrôles (≈ 30 s) | fonctionnement local, LED orange |

Retour automatique à OK au premier contrôle réussi.

**LED RGB** : vert = en ligne et sain ; orange = fonctionnement local (MQTT absent) ; rouge = Linky ou EEPROM dégradé.

### 3.4 Journal d'événements
Tampon circulaire des 64 derniers événements : démarrage (avec raison du reset), échecs et dépassements de tâches (1 par tâche / 5 s max), passages DEGRADED / OK, module EEPROM remplacé, EEPROM formatée, connexions MQTT.

### 3.5 Filet de sécurité
Task Watchdog sur `loopTask` (`enableLoopWDT()`, 5 s, panic → redémarrage). La raison du redémarrage (`POWERON`, `TASK_WDT`, `PANIC`, `BROWNOUT`…) est publiée au boot et figure dans `/health`.

### 3.6 Accès

| Canal | Contenu |
|---|---|
| MQTT `heatingCtrl_v4/health` (60 s) | santé, heap, pile libre min, boucle, tâches sur la dernière fenêtre |
| `GET /api/health` | idem |
| `GET /api/trace` | journal + cumuls par tâche depuis le boot |

Format des tâches dans `/health` : `"linky":{"n":appels,"ok":travaux,"ko":échecs,"avg":µs,"max":µs,"load":‰,"ovr":dépassements}`.

---

## 4. Gestion des traces (série et MQTT)

### 4.1 Fichiers
- `include/log_config.h` — **seul fichier à modifier** pour la production.
- `include/Logger.h` — mécanique (macros, état, fonctions).

### 4.2 Niveaux
`ERROR` < `WARN` < `INFO` < `DEBUG`. Les quelque 185 traces existantes ont été classées : erreurs d'accès (ERROR), situations anormales tolérées (WARN), événements de fonctionnement (INFO), détail des trames, horloge, lectures EEPROM et dumps (DEBUG).

Macros : `LOG_E`, `LOG_W`, `LOG` (INFO, compatible v4), `LOG_D`.

### 4.3 Trois niveaux de contrôle
1. **Compilation** (`log_config.h`) : `LOG_SERIAL_COMPILED 0` / `LOG_MQTT_COMPILED 0` suppriment entièrement le code des traces.
2. **État au démarrage** (`log_config.h`) : `LOG_SERIAL_DEFAULT`, `LOG_MQTT_DEFAULT`, `LOG_LEVEL_SERIAL`, `LOG_LEVEL_MQTT`.
3. **À chaud, par appel de fonction** :
   ```cpp
   logSetSerial(false);
   logSetMqtt(true);
   logSetSerialLevel(LogLevel::WARN);
   logSetMqttLevel(LogLevel::INFO);
   logSetProduction();    // série OFF, MQTT ≥ WARN
   logSetDevelopment();   // série DEBUG, MQTT ≥ INFO
   ```
   ou à distance :
   ```
   mosquitto_pub -t heatingCtrl_v4/log/set -m '{"serial":false,"mqttLevel":"WARN"}'
   mosquitto_pub -t heatingCtrl_v4/log/set -m '{"profile":"production"}'
   curl -X POST http://192.168.1.50/api/config/log -d '{"level":"DEBUG"}'
   ```
   Le réglage à chaud est sauvegardé en NVS et rechargé au démarrage. Il est visible dans `GET /api/state` (objet `log`).

`publishLog()` ne formate plus rien si ni la série ni le MQTT ne sont actifs pour le niveau demandé. Le journal de commandes (`publishCommandLog`) est traité comme un log de niveau INFO. Les dumps série (`dump()`) ne s'exécutent qu'au niveau DEBUG.

---

## 5. EEPROM 24CS512

### 5.1 Carte mémoire v1
| Adresse | Taille | Contenu |
|---|---|---|
| 0x0000 | 16 o | en-tête : `NAUC` + version de layout |
| 0x0010 | 4 × 16 o | commandes zones (chaîne ≤ 10 car.) |
| 0x0050 | 1 o | flags système |
| 0x0054 / 0x0055 | 1 + 1 o | compteurs Tempo rouge / blanc |
| 0x0100 | 8 × 64 o | profils calendrier (nom 12 o + 48 créneaux) |
| 0x0300 | 4 × 16 o | associations zone / jour |
| 0x0400 | — | fin de la zone utilisée (63 Ko libres) |

Enregistrements alignés sur 16 octets : l'ECC du 24CS512 travaille par mots de 4 octets et réécrit le mot entier à chaque écriture partielle ; l'alignement évite qu'un enregistrement en use un autre.

### 5.2 Formatage automatique
Au démarrage : en-tête absent (puce neuve, toute à 0xFF) ou étranger → effacement de la zone utilisée et écriture de l'en-tête. Une erreur de lecture I2C ne déclenche **jamais** de formatage. `eraseAll()` (bouton BOOT 5 s, `POST /api/eeprom/erase`) n'efface plus que la zone utilisée (≈ 80 ms au lieu de ≈ 2,5 s pour 64 Ko, compatible avec le watchdog) puis réécrit l'en-tête.

### 5.3 Écritures
Découpage par blocs de 64 octets maximum (buffer Wire de 128 octets moins 2 octets d'adresse), sans jamais franchir une page physique de 128 octets ; attente de fin de cycle par ACK polling. Lectures en lecture aléatoire avec START répété.

### 5.4 Numéro de série — intérêt et usage retenu
Le 24CS512 contient un numéro de série de 128 bits, unique et en lecture seule (registre de sécurité, adresse I2C 0x58, mot 0x0800), et un identifiant fabricant (0x00D0C8 = Microchip, 512 Kbit).

**Ce qu'il apporte ici :**
- **Vérification de la puce** au démarrage (ID fabricant attendu, sinon avertissement).
- **Traçabilité** : n° de série publié au boot (log MQTT), visible dans `/api/state` (`eepromSerial`) et le journal. Utile pour le SAV d'un module vendu séparément.
- **Détection d'un changement de module** : le n° de série est mémorisé en NVS côté ESP32. S'il change, un avertissement est publié (les données du module viennent d'une autre carte ou d'une puce neuve). Rien n'est effacé automatiquement.

**Ce qu'il n'apporte pas :**
- Ce n'est **pas une protection** : n'importe quel programme peut le lire ; il ne sécurise ni le firmware ni les données.
- Ce n'est pas le bon identifiant d'appareil pour MQTT : le module EEPROM est remplaçable et peut être absent. Pour distinguer plusieurs gestionnaires sur un même broker, l'adresse MAC de l'ESP32 est préférable (voir § 8).

**Page ID utilisateur** (128 o, 0x0880, verrouillable) : lecture disponible (`readIdPage()`), destinée à un usage futur — par exemple inscrire en fabrication la révision de carte, le nombre de zones et la date, puis verrouiller. L'écriture et le verrouillage ne sont volontairement pas implémentés (verrouillage irréversible).

---

## 6. Calendrier

Le calendrier devient utilisable (EEPROM suffisante). Le jour de la semaine, jamais renseigné en v4 (toujours lundi), est désormais pris dans le message `utcClock` :
- champ `dow` ou `weekday` (0 = lundi … 6 = dimanche), sinon
- calcul à partir de `year`, `month`, `day` s'ils sont présents.

Sans l'un ou l'autre, un avertissement unique est publié et le profil du lundi est appliqué.

---

## 7. Fichiers

| Fichier | Statut |
|---|---|
| `include/log_config.h` | **nouveau** — réglage des traces |
| `include/Logger.h` | **nouveau** — mécanique des traces |
| `include/secrets.h` / `secrets.example.h` | **nouveau** — identifiants WiFi (secrets.h exclu de Git) |
| `lib/Supervisor/` | **nouveau** — supervision, trace, journal |
| `CHANGELOG.md` | **nouveau** — notes de version (devenues `docs/firmware-v5.md`) |
| `include/config.h` | EEPROM 24CS512, carte mémoire, constantes de supervision, préfixes de traces |
| `include/types.h` | commentaires fil pilote corrigés, `volatile` |
| `src/main.cpp` | boucle supervisée, santé, LED, watchdog, restauration au boot |
| `lib/StorageManager/` | 24CS512, n° de série, en-tête, ACK polling, persistance des traces |
| `lib/TempoManager/` | restauration fin de restriction, libération ciblée, saison |
| `lib/ZoneManager/` | CM2 conforme, relance CM2 après délestage |
| `lib/OverloadManager/` | délestage vers le bas uniquement, perte de données Linky |
| `lib/LinkyReader/` | checksum, buffer RX, Isousc, compteurs |
| `lib/Publisher/` | timeouts, backoff, niveaux de logs, `log/set`, jour de semaine |
| `lib/WebUI/` | `/api/health`, `/api/trace`, `/api/config/log`, heap |
| `lib/DisplayManager/` | affichage de la commande réelle |
| `lib/CommandHandler/`, `lib/ScheduleManager/` | compteurs, jour de semaine, niveaux de traces |

---

## 8. Points à valider sur carte et pistes

**À tester en priorité :**
1. Séquence CM2 sur un radiateur réel (§ 2.3).
2. Lecture du n° de série et de l'ID fabricant (START répété du core Arduino 3.0.1 avec les adresses 0x58 et 0x7C).
3. Formatage d'une 24CS512 neuve puis redémarrage (en-tête reconnu, pas de second formatage).
4. Fin de période Tempo blanche ou rouge : retour des zones à leur commande EEPROM.
5. Broker arrêté : boucle non bloquée (tâche `mqtt` < 2 s dans `/api/health`), LED orange.
6. Câble TIC débranché : passage Linky DEGRADED en 15 à 25 s, délestage suspendu, LED rouge.
7. Charge réelle des tâches et marge de pile (`stackFree`) après 24 h.

**À vérifier côté installation :**
- `utcClock` est-il en heure UTC ou locale ? En UTC, les créneaux du calendrier, le comptage Tempo à minuit et la publication des index « à 23 h » sont décalés de 1 à 2 h.
- Convention du champ `dow` s'il est publié (0 = lundi attendu).

**Pistes pour une version commerciale multi-appareils :**
- Identifiant d'appareil dérivé de la MAC ESP32 dans le client MQTT et la racine des topics (aujourd'hui `heatingCtrl_v4`, identique sur toutes les cartes).
- Authentification de l'API HTTP (`/api/eeprom/erase`, `/api/config/*` sont ouverts sur le réseau local).
