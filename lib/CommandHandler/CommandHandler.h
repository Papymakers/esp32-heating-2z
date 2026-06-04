#pragma once
// =============================================================================
// CommandHandler.h — Pipeline de traitement des commandes
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Responsabilités :
//   - Parser les commandes entrantes (string → HeatingCmd)
//   - Résoudre le contexte (priorité, origine, source)
//   - Appliquer les règles métier (Tempo → HG forcé, etc.)
//   - Déléguer l'exécution au ZoneManager
//   - Décider de la sauvegarde EEPROM
//   - Publier l'état après exécution
//
// Pipeline :
//   input (string/cmd) → parse → resolve → rules → execute → store → publish
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations (évite les inclusions circulaires)
class ZoneManager;
class StorageManager;
class Publisher;
class TempoManager;

// =============================================================================
// CLASSE COMMANDHANDLER
// =============================================================================

class CommandHandler {
public:

  // ---------------------------------------------------------------------------
  // Initialisation — injection des dépendances
  // ---------------------------------------------------------------------------

  void begin(ZoneManager*    zoneMgr,
             StorageManager* storageMgr,
             Publisher*      publisher,
             TempoManager*   tempoMgr);

  // ---------------------------------------------------------------------------
  // Point d'entrée principal (string)
  // Utilisé par MQTT, WebSocket, switch physique
  // ---------------------------------------------------------------------------

  // Traite une commande string pour une zone donnée
  // Retourne true si la commande a été acceptée et exécutée
  bool handle(uint8_t         zone,
              const char*     cmdStr,
              CommandOrigin   origin,
              CommandSource   source);

  // ---------------------------------------------------------------------------
  // Point d'entrée principal (HeatingCmd)
  // Utilisé en interne (TempoManager, ScheduleManager, OverloadManager)
  // ---------------------------------------------------------------------------

  bool handleCmd(uint8_t          zone,
                 HeatingCmd       cmd,
                 CommandOrigin    origin,
                 CommandSource    source);

  // ---------------------------------------------------------------------------
  // Commandes système spéciales
  // ---------------------------------------------------------------------------

  // Réinitialise toutes les zones (reboot / EEPROM erase)
  void handleReset();

  // Applique la commande de boot par défaut sur toutes les zones
  void handleBootDefault();

  // Restaure les zones depuis l'EEPROM (après boot)
  void handleRecovery();

  // ---------------------------------------------------------------------------
  // Parsing
  // ---------------------------------------------------------------------------

  // Convertit une string en HeatingCmd
  // Retourne HeatingCmd::UNKNOWN si non reconnue
  static HeatingCmd parseCmd(const char* cmdStr);

  // Convertit HeatingCmd en string (alias vers heatingCmdToStr)
  static const char* cmdToStr(HeatingCmd cmd);

  // Valide qu'une commande est applicable à une zone
  static bool isValidCmd(HeatingCmd cmd);

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dumpContext(const CommandContext& ctx) const;

private:

  // Pointeurs vers les modules dépendants (injectés via begin())
  ZoneManager*    _zoneMgr    = nullptr;
  StorageManager* _storageMgr = nullptr;
  Publisher*      _publisher  = nullptr;
  TempoManager*   _tempoMgr   = nullptr;

  // ---------------------------------------------------------------------------
  // Pipeline interne
  // ---------------------------------------------------------------------------

  // 1. Résolution du contexte complet depuis les paramètres bruts
  CommandContext _resolve(uint8_t       zone,
                          HeatingCmd    cmd,
                          CommandOrigin origin,
                          CommandSource source) const;

  // 2. Application des règles métier (Tempo, surcharge, etc.)
  //    Peut modifier ctx.cmd (ex: forcer HG si Tempo rouge/blanc + HP)
  void _applyRules(CommandContext& ctx) const;

  // 3. Exécution via ZoneManager
  bool _execute(CommandContext& ctx);

  // 4. Sauvegarde EEPROM si autorisée
  bool _store(const CommandContext& ctx);

  // 5. Publication MQTT + WebSocket
  void _publish(const CommandContext& ctx, bool stored);

  // ---------------------------------------------------------------------------
  // Règles métier internes
  // ---------------------------------------------------------------------------

  // Calcule la priorité selon origin/source
  CommandPriority _resolvePriority(CommandOrigin   origin,
                                   CommandSource   source) const;

  // Détermine si la commande doit être sauvegardée en EEPROM
  bool _shouldStore(const CommandContext& ctx) const;

  // Détermine si la commande est persistante (survit au reboot)
  bool _isPersistent(const CommandContext& ctx) const;

  // Détermine si la commande est temporaire (pas de sauvegarde)
  bool _isTemporary(const CommandContext& ctx) const;
};

// Instance globale
extern CommandHandler commandHandler;
