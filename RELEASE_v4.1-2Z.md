# v4.1-2Z — Planificateur hebdomadaire

Cette version documente et rend accessible le **planificateur hebdomadaire** du
gestionnaire de chauffage 2 zones, et regroupe plusieurs améliorations de fiabilité
issues des tests en conditions réelles.

## 📅 Planificateur hebdomadaire (option 24C32)

Le planificateur permet de programmer automatiquement les ordres de chauffage de
chaque zone au fil de la journée, par tranches de 30 minutes. Il repose sur des
profils réutilisables que l'on affecte à chaque zone, et s'édite directement à la
souris depuis l'interface web.

Il est **désactivé sur la 24C02** (mémoire insuffisante) et s'active en installant
une **EEPROM 24C32**, remplacement pin-to-pin direct de la 24C02 (même boîtier 8
broches, même adresse I2C 0x50). Référence testée : **Microchip AT24C32E** (PDIP-8 :
`AT24C32E-PUM`).

Le planificateur reste **non prioritaire** : une commande manuelle (bouton, web ou
MQTT), un délestage ou une règle Tempo prennent toujours le pas sur l'ordre programmé.

## ✨ Autres nouveautés

- Compteurs de jours Tempo de la saison (rouges / blancs), sauvegardés en EEPROM,
  avec remise à zéro automatique au 1ᵉʳ novembre.
- Topic MQTT `heatingCtrl_v4/tempo/setCounters` pour corriger manuellement les
  compteurs.

## 🛠️ Fiabilité

- Filtrage des commandes parasites au basculement HC→HP (confirmation du niveau bas
  sur l'entrée bouton).
- Le délestage n'écrase plus une zone déjà à l'arrêt.
- Affichage TM1637 permanent, avec LED d'état WiFi/Linky suivant les trames TIC.
- Un bouton par zone (SW1 → zone 1, SW2 → zone 2).

## ⬆️ Mise à jour

Firmware seul (sans le planificateur) : récupérez le code, adaptez `include/config.h`
puis flashez.

```bash
pio run --target upload
```

Pour activer le planificateur : remplacez la 24C02 par une 24C32, basculez le
`#define` dans `config.h` :

```cpp
// #define EEPROM_TYPE_24C02
#define EEPROM_TYPE_24C32
```

puis recompilez, flashez, et effacez l'EEPROM au premier démarrage (bouton BOOT
maintenu, ou depuis la page web) pour initialiser le nouveau plan mémoire.

## 📋 Détails

Voir le [CHANGELOG](CHANGELOG.md) pour la liste complète des modifications.
