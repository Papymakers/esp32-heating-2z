// =============================================================================
// CommandHandler.cpp — Pipeline de traitement des commandes
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Pipeline complet :
//   handle(zone, cmdStr, origin, source)
//     └─► parseCmd()         → HeatingCmd
//     └─► _resolve()         → CommandContext complet
//     └─► _applyRules()      → Modification éventuelle (Tempo → HG)
//     └─► _execute()         → ZoneManager::applyCommand()
//     └─► _store()           → StorageManager (si autorisé)
//     └─► _publish()         → Publisher (MQTT + WebSocket)
// =============================================================================

#include "CommandHandler.h"
#include "ZoneManager.h"
#include "StorageManager.h"
#include "Publisher.h"
#include "TempoManager.h"

// Instance globale
CommandHandler commandHandler;

// =============================================================================
// INITIALISATION
// =============================================================================

void CommandHandler::begin(ZoneManager*    zoneMgr,
                           StorageManager* storageMgr,
                           Publisher*      publisher,
                           TempoManager*   tempoMgr) {
  _zoneMgr    = zoneMgr;
  _storageMgr = storageMgr;
  _publisher  = publisher;
  _tempoMgr   = tempoMgr;

  LOG(LOG_CMD, "CommandHandler ready.");
}

// =============================================================================
// POINTS D'ENTRÉE PUBLICS
// =============================================================================

bool CommandHandler::handle(uint8_t       zone,
                             const char*   cmdStr,
                             CommandOrigin origin,
                             CommandSource source) {
  if (!cmdStr) {
    LOG(LOG_CMD, "Null command string for zone %d", zone);
    return false;
  }

  HeatingCmd cmd = parseCmd(cmdStr);

  if (cmd == HeatingCmd::UNKNOWN) {
    LOG(LOG_CMD, "Unknown command '%s' for zone %d", cmdStr, zone);
    return false;
  }

  return handleCmd(zone, cmd, origin, source);
}

bool CommandHandler::handleCmd(uint8_t       zone,
                                HeatingCmd    cmd,
                                CommandOrigin origin,
                                CommandSource source) {
  if (zone < 1 || zone > NUM_ZONES) {
    LOG(LOG_CMD, "Invalid zone %d", zone);
    return false;
  }

  if (!isValidCmd(cmd)) {
    LOG(LOG_CMD, "Invalid command %s for zone %d",
        heatingCmdToStr(cmd), zone);
    return false;
  }

  // 1. Résolution du contexte
  CommandContext ctx = _resolve(zone, cmd, origin, source);

  LOG(LOG_CMD, "Z%d IN  cmd=%-5s orig=%s src=%s prio=%d",
      zone,
      heatingCmdToStr(ctx.inputCmd),
      originToStr(ctx.origin),
      sourceToStr(ctx.source),
      (uint8_t)ctx.priority);

  // 2. Application des règles métier
  _applyRules(ctx);

  if (ctx.cmd != ctx.inputCmd) {
    LOG(LOG_CMD, "Z%d RULE cmd=%s → %s (Tempo/Override)",
        zone,
        heatingCmdToStr(ctx.inputCmd),
        heatingCmdToStr(ctx.cmd));
  }

  LOG(LOG_CMD, "Z%d OUT cmd=%-5s persist=%d temp=%d stop=%d",
      zone,
      heatingCmdToStr(ctx.cmd),
      ctx.isPersistent,
      ctx.isTemporary,
      ctx.isStop);

  // 3. Exécution
  bool executed = _execute(ctx);
  if (!executed) {
    LOG(LOG_CMD, "Z%d command %s REJECTED (priority)",
        zone, heatingCmdToStr(ctx.cmd));
    return false;
  }

  // 4. Sauvegarde EEPROM
  bool stored = _store(ctx);

  // 5. Publication
  _publish(ctx, stored);

  return true;
}

// =============================================================================
// COMMANDES SYSTÈME SPÉCIALES
// =============================================================================

void CommandHandler::handleReset() {
  LOG(LOG_CMD, "RESET — restarting module...");

  if (_zoneMgr) {
    _zoneMgr->applyAll(HeatingCmd::ECO,
                       CommandOrigin::SYSTEM,
                       CommandSource::SRC_DEFAULT);
  }
  delay(500);
  ESP.restart();
}

void CommandHandler::handleBootDefault() {
  LOG(LOG_CMD, "Boot default — applying ECO to all zones");

  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    handleCmd(z,
              HeatingCmd::ECO,
              CommandOrigin::SYSTEM,
              CommandSource::SRC_DEFAULT);
  }
}

void CommandHandler::handleRecovery() {
  if (!_storageMgr) {
    LOG(LOG_CMD, "Recovery skipped — no StorageManager");
    handleBootDefault();
    return;
  }

  LOG(LOG_CMD, "Recovery — loading zones from EEPROM...");

  for (uint8_t z = 1; z <= NUM_ZONES; z++) {
    char cmdStr[10] = {0};
    bool ok = _storageMgr->loadZoneCmd(z, cmdStr, sizeof(cmdStr));

    if (!ok || cmdStr[0] == '\0') {
      LOG(LOG_CMD, "Z%d no EEPROM data → ECO default", z);
      handleCmd(z,
                HeatingCmd::ECO,
                CommandOrigin::SYSTEM,
                CommandSource::SRC_DEFAULT);
      continue;
    }

    HeatingCmd cmd = parseCmd(cmdStr);
    if (cmd == HeatingCmd::UNKNOWN) {
      LOG(LOG_CMD, "Z%d EEPROM invalid '%s' → ECO", z, cmdStr);
      handleCmd(z,
                HeatingCmd::ECO,
                CommandOrigin::SYSTEM,
                CommandSource::SRC_DEFAULT);
      continue;
    }

    LOG(LOG_CMD, "Z%d EEPROM → %s", z, cmdStr);
    handleCmd(z,
              cmd,
              CommandOrigin::SYSTEM,
              CommandSource::SRC_RECOVERY);
  }

  LOG(LOG_CMD, "Recovery complete.");
}

// =============================================================================
// PARSING
// =============================================================================

HeatingCmd CommandHandler::parseCmd(const char* cmdStr) {
  if (!cmdStr) return HeatingCmd::UNKNOWN;

  // Comparaison insensible à la casse
  if (strcasecmp(cmdStr, CMD_STOP) == 0) return HeatingCmd::STOP;
  if (strcasecmp(cmdStr, CMD_HG)   == 0) return HeatingCmd::HG;
  if (strcasecmp(cmdStr, CMD_ECO)  == 0) return HeatingCmd::ECO;
  if (strcasecmp(cmdStr, CMD_CONF) == 0) return HeatingCmd::CONF;
  if (strcasecmp(cmdStr, CMD_CM2)  == 0) return HeatingCmd::CM2;

  return HeatingCmd::UNKNOWN;
}

const char* CommandHandler::cmdToStr(HeatingCmd cmd) {
  return heatingCmdToStr(cmd);
}

bool CommandHandler::isValidCmd(HeatingCmd cmd) {
  return cmd != HeatingCmd::UNKNOWN;
}

// =============================================================================
// PIPELINE INTERNE
// =============================================================================

// 1. RÉSOLUTION DU CONTEXTE
// -----------------------------------------------------------------------------
CommandContext CommandHandler::_resolve(uint8_t       zone,
                                        HeatingCmd    cmd,
                                        CommandOrigin origin,
                                        CommandSource source) const {
  CommandContext ctx{};

  ctx.zone      = zone;
  ctx.cmd       = cmd;
  ctx.inputCmd  = cmd;
  ctx.origin    = origin;
  ctx.source    = source;
  ctx.priority  = _resolvePriority(origin, source);
  ctx.isPersistent = _isPersistent(ctx);
  ctx.isTemporary  = _isTemporary(ctx);
  ctx.isStop       = (cmd == HeatingCmd::STOP);

  return ctx;
}

// 2. RÈGLES MÉTIER
// -----------------------------------------------------------------------------
void CommandHandler::_applyRules(CommandContext& ctx) const {

  // ── Règle 1 : TEMPO ────────────────────────────────────────────────────────
  // Tempo suggère HG sur les commandes SYSTEM (recovery, calendrier)
  // Une commande manuelle USER (SW, WS, MQTT) désactive Tempo automatiquement
  if (_tempoMgr && _tempoMgr->isActive() && _tempoMgr->isForceHG()) {
    if (ctx.origin == CommandOrigin::USER &&
        ctx.source != CommandSource::SRC_TEMPO) {
      // Désactivation automatique Tempo — l'utilisateur reprend la main
      LOG(LOG_CMD, "Z%d USER cmd — auto-disabling Tempo", ctx.zone);
      _tempoMgr->setEnabled(false);
      if (_storageMgr) {
        const TempoConfig& tc = _tempoMgr->getConfig();
        _storageMgr->saveTempoConfig(tc.enabled,
                                      tc.forceHG_WhiteHP,
                                      tc.forceHG_RedHP,
                                      tc.forceHG_RedHC);
      }
    } else if (ctx.origin == CommandOrigin::SYSTEM &&
               ctx.source != CommandSource::SRC_TEMPO &&
               ctx.source != CommandSource::SRC_OVERLOAD) {
      // Commande SYSTEM (recovery, calendrier) → Tempo suggère HG
      ctx.cmd          = HeatingCmd::HG;
      ctx.priority     = CommandPriority::PRIO_USER;
      ctx.isTemporary  = true;
      ctx.isPersistent = false;
    }
  }

  // ── Règle 2 : Source TIMER → toujours temporaire ───────────────────────────
  if (ctx.source == CommandSource::SRC_TIMER) {
    ctx.isTemporary  = true;
    ctx.isPersistent = false;
  }

  // ── Règle 3 : Source RECOVERY → persistant (restauration boot) ─────────────
  if (ctx.source == CommandSource::SRC_RECOVERY) {
    ctx.isPersistent = true;
    ctx.isTemporary  = false;
  }

  // ── Règle 4 : Source OVERLOAD → temporaire, priorité haute ─────────────────
  if (ctx.source == CommandSource::SRC_OVERLOAD) {
    ctx.isTemporary  = true;
    ctx.isPersistent = false;
    ctx.priority     = CommandPriority::PRIO_OVERLOAD;
  }

  // ── Règle 5 : STOP → toujours persistant si commande USER ──────────────────
  if (ctx.cmd == HeatingCmd::STOP &&
      ctx.origin == CommandOrigin::USER) {
    ctx.isPersistent = true;
    ctx.isStop       = true;
  }
}

// 3. EXÉCUTION
// -----------------------------------------------------------------------------
bool CommandHandler::_execute(CommandContext& ctx) {
  if (!_zoneMgr) return false;
  return _zoneMgr->applyCommand(ctx);
}

// 4. SAUVEGARDE EEPROM
// -----------------------------------------------------------------------------
bool CommandHandler::_store(const CommandContext& ctx) {
  if (!_storageMgr) return false;
  if (!_shouldStore(ctx)) {
    LOG(LOG_CMD, "Z%d EEPROM skip (temp=%d persist=%d orig=%s)",
        ctx.zone,
        ctx.isTemporary,
        ctx.isPersistent,
        originToStr(ctx.origin));
    return false;
  }

  // On sauvegarde la commande d'entrée (pas la commande résolue)
  // pour restaurer l'intention utilisateur au reboot
  bool ok = _storageMgr->saveZoneCmd(ctx.zone,
                                      heatingCmdToStr(ctx.inputCmd));
  if (ok) {
    LOG(LOG_CMD, "Z%d EEPROM saved: %s",
        ctx.zone, heatingCmdToStr(ctx.inputCmd));
  } else {
    LOG(LOG_CMD, "Z%d EEPROM write FAILED", ctx.zone);
  }
  return ok;
}

// 5. PUBLICATION
// -----------------------------------------------------------------------------
void CommandHandler::_publish(const CommandContext& ctx, bool stored) {
  if (!_publisher) return;
  _publisher->publishCommand(ctx, stored);
}

// =============================================================================
// HELPERS PRIVÉS
// =============================================================================

CommandPriority CommandHandler::_resolvePriority(CommandOrigin origin,
                                                  CommandSource source) const {
  if (origin == CommandOrigin::USER) {
    return CommandPriority::PRIO_USER;
  }

  // SYSTEM
  switch (source) {
    case CommandSource::SRC_TEMPO:    return CommandPriority::PRIO_TEMPO;
    case CommandSource::SRC_OVERLOAD: return CommandPriority::PRIO_OVERLOAD;
    case CommandSource::SRC_SCHEDULE: return CommandPriority::PRIO_SCHEDULE;
    case CommandSource::SRC_TIMER:    return CommandPriority::PRIO_USER;
                                      // Timer CM2 = même prio que user
                                      // (c'est une sous-commande d'un CONF user)
    default:                          return CommandPriority::PRIO_DEFAULT;
  }
}

bool CommandHandler::_shouldStore(const CommandContext& ctx) const {
  // Sauvegarde EEPROM uniquement si :
  // - Commande utilisateur (USER)
  // - Persistante
  // - Pas temporaire
  // - Pas une commande TEMPO forcée (on veut préserver l'intention user)
  return (ctx.origin    == CommandOrigin::USER) &&
         ctx.isPersistent &&
         !ctx.isTemporary &&
         (ctx.source != CommandSource::SRC_TIMER);
}

bool CommandHandler::_isPersistent(const CommandContext& ctx) const {
  // Persistant = survit au reboot → sauvegardé en EEPROM
  if (ctx.origin == CommandOrigin::USER) return true;

  switch (ctx.source) {
    case CommandSource::SRC_DEFAULT:
    case CommandSource::SRC_RECOVERY:
    case CommandSource::SRC_OVERLOAD:
      return true;
    default:
      return false;
  }
}

bool CommandHandler::_isTemporary(const CommandContext& ctx) const {
  // Temporaire = ne doit pas être sauvegardé
  switch (ctx.source) {
    case CommandSource::SRC_TEMPO:
    case CommandSource::SRC_TIMER:
    case CommandSource::SRC_OVERLOAD:
      return true;
    default:
      return false;
  }
}

// =============================================================================
// DEBUG
// =============================================================================

void CommandHandler::dumpContext(const CommandContext& ctx) const {
  Serial.printf(
    "%s --- CommandContext ---\n"
    "%s   zone       : %d\n"
    "%s   inputCmd   : %s\n"
    "%s   finalCmd   : %s\n"
    "%s   origin     : %s\n"
    "%s   source     : %s\n"
    "%s   priority   : %d\n"
    "%s   isPersist  : %d\n"
    "%s   isTemp     : %d\n"
    "%s   isStop     : %d\n"
    "%s ---------------------\n",
    LOG_CMD,
    LOG_CMD, ctx.zone,
    LOG_CMD, heatingCmdToStr(ctx.inputCmd),
    LOG_CMD, heatingCmdToStr(ctx.cmd),
    LOG_CMD, originToStr(ctx.origin),
    LOG_CMD, sourceToStr(ctx.source),
    LOG_CMD, (uint8_t)ctx.priority,
    LOG_CMD, ctx.isPersistent,
    LOG_CMD, ctx.isTemporary,
    LOG_CMD, ctx.isStop,
    LOG_CMD
  );
}
