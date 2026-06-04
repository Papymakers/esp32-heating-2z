// =============================================================================
// ZoneManager.cpp — Machine à états par zone (x4)
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include "ZoneManager.h"

// Instance globale
ZoneManager zoneManager;

// =============================================================================
// INITIALISATION
// =============================================================================

void ZoneManager::begin() {
  LOG(LOG_ZONE, "Initializing %d zones...", NUM_ZONES);

  for (uint8_t i = 0; i < NUM_ZONES; i++) {
    initZone(i + 1);  // zones 1-based
  }

  LOG(LOG_ZONE, "All zones initialized.");
}

void ZoneManager::initZone(uint8_t zoneId) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;

  // Config hardware
  pinMode(PIN_MOC_POS[zi], OUTPUT);
  pinMode(PIN_MOC_NEG[zi], OUTPUT);

  // MOC actif bas : HIGH = MOC bloqué (état sûr au démarrage = ECO)
  digitalWrite(PIN_MOC_POS[zi], LOW);
  digitalWrite(PIN_MOC_NEG[zi], LOW);

  // Init structure
  _zones[zi] = {};
  _zones[zi].id             = zoneId;
  _zones[zi].state          = ZoneState::IDLE;
  _zones[zi].currentCmd     = HeatingCmd::ECO;
  _zones[zi].savedCmd       = HeatingCmd::ECO;
  _zones[zi].activePriority = CommandPriority::PRIO_DEFAULT;
  _zones[zi].posState       = false;
  _zones[zi].negState       = false;
  _zones[zi].timerActive    = false;
  _zones[zi].cm2Mode        = Cm2Mode::MODE_7S;
  _zones[zi].timerStart     = 0;
  _zones[zi].overloadStart  = 0;
  _zones[zi].restoreTimer   = 0;

  LOG(LOG_ZONE, "Zone %d initialized (ECO default).", zoneId);
}

// =============================================================================
// COMMANDES
// =============================================================================

bool ZoneManager::applyCommand(const CommandContext& ctx) {
  if (ctx.zone < 1 || ctx.zone > NUM_ZONES) {
    LOG(LOG_ZONE, "Invalid zone %d", ctx.zone);
    return false;
  }

  uint8_t zi = ctx.zone - 1;
  ZoneData& z = _zones[zi];

  // Zone verrouillée → refus sauf FAULT
  if (z.state == ZoneState::LOCKED &&
      ctx.priority != CommandPriority::PRIO_FAULT) {
    LOG(LOG_ZONE, "Zone %d LOCKED — command %s refused.",
        ctx.zone, heatingCmdToStr(ctx.cmd));
    return false;
  }

  // Vérification priorité
  if (!_canOverride(zi, ctx.priority)) {
    LOG(LOG_ZONE, "Zone %d priority too low (%d <= %d) — %s refused.",
        ctx.zone,
        (uint8_t)ctx.priority,
        (uint8_t)z.activePriority,
        heatingCmdToStr(ctx.cmd));
    return false;
  }

  LOG(LOG_ZONE, "Zone %d [%s] %s → %s (prio=%d src=%s)",
      ctx.zone,
      zoneStateToStr(z.state),
      heatingCmdToStr(z.currentCmd),
      heatingCmdToStr(ctx.cmd),
      (uint8_t)ctx.priority,
      sourceToStr(ctx.source));

  // Sauvegarde commande précédente si on entre en OVERRIDE
  if (ctx.source == CommandSource::SRC_OVERLOAD &&
      z.state == ZoneState::NORMAL) {
    z.savedCmd = z.currentCmd;
    z.state    = ZoneState::OVERRIDE;
    z.overloadStart = millis();
    LOG(LOG_ZONE, "Zone %d → OVERRIDE (saved: %s)",
        ctx.zone, heatingCmdToStr(z.savedCmd));
  }
  else if (z.state == ZoneState::IDLE ||
           z.state == ZoneState::RESTORE) {
    z.state = ZoneState::NORMAL;
  }

  // Mise à jour état
  z.currentCmd     = ctx.cmd;
  z.activePriority = ctx.priority;

  // Gestion timer CM2
  if (ctx.cmd == HeatingCmd::CM2) {
    z.timerActive = true;
    z.cm2Mode     = Cm2Mode::MODE_7S;
    z.timerStart  = millis();
    // CM2 démarre en phase 7s (CONF)
    applyHardware(zi, HeatingCmd::CONF);
    LOG(LOG_ZONE, "Zone %d CM2 timer started (7s phase).", ctx.zone);
  } else {
    z.timerActive = false;
    applyHardware(zi, ctx.cmd);
  }

  return true;
}

void ZoneManager::applyHardware(uint8_t zoneIndex, HeatingCmd cmd) {
  if (zoneIndex >= NUM_ZONES) return;

  bool pos, neg;
  _cmdToMoc(cmd, pos, neg);
  _setMocOutputs(zoneIndex, pos, neg);

  _zones[zoneIndex].posState = pos;
  _zones[zoneIndex].negState = neg;
}

void ZoneManager::applyAll(HeatingCmd cmd,
                            CommandOrigin origin,
                            CommandSource source) {
  for (uint8_t i = 1; i <= NUM_ZONES; i++) {
    CommandContext ctx{};
    ctx.zone     = i;
    ctx.cmd      = cmd;
    ctx.inputCmd = cmd;
    ctx.origin   = origin;
    ctx.source   = source;
    ctx.priority = (source == CommandSource::SRC_OVERLOAD)
                   ? CommandPriority::PRIO_OVERLOAD
                   : CommandPriority::PRIO_DEFAULT;
    applyCommand(ctx);
  }
}

// =============================================================================
// FSM LOOP
// =============================================================================

void ZoneManager::update() {
  for (uint8_t zi = 0; zi < NUM_ZONES; zi++) {
    switch (_zones[zi].state) {
      case ZoneState::NORMAL:   _handleNormal(zi);   break;
      case ZoneState::OVERRIDE: _handleOverride(zi); break;
      case ZoneState::RESTORE:  _handleRestore(zi);  break;
      case ZoneState::LOCKED:   _handleLocked(zi);   break;
      case ZoneState::IDLE:     break;  // Rien à faire
    }
  }
}

// =============================================================================
// HANDLERS FSM INTERNES
// =============================================================================

void ZoneManager::_handleNormal(uint8_t zi) {
  // En état NORMAL, on gère uniquement le timer CM2
  if (_zones[zi].timerActive) {
    _updateCm2Timer(zi);
  }
}

void ZoneManager::_handleOverride(uint8_t zi) {
  ZoneData& z = _zones[zi];

  // Timer CM2 actif pendant override ?
  if (z.timerActive) {
    _updateCm2Timer(zi);
  }

  // Vérification durée max override (FALLBACK_DURATION_MS)
  if (millis() - z.overloadStart >= FALLBACK_DURATION_MS) {
    LOG(LOG_ZONE, "Zone %d OVERRIDE timeout → RESTORE", z.id);
    z.state       = ZoneState::RESTORE;
    z.restoreTimer = millis();
  }
}

void ZoneManager::_handleRestore(uint8_t zi) {
  ZoneData& z = _zones[zi];

  // Après un court délai de restauration, on repasse en NORMAL
  constexpr unsigned long RESTORE_DELAY_MS = 2000;

  if (millis() - z.restoreTimer >= RESTORE_DELAY_MS) {
    LOG(LOG_ZONE, "Zone %d RESTORE → NORMAL (cmd: %s)",
        z.id, heatingCmdToStr(z.savedCmd));

    // Restauration commande sauvegardée
    z.currentCmd     = z.savedCmd;
    z.activePriority = CommandPriority::PRIO_DEFAULT;
    z.state          = ZoneState::NORMAL;
    z.timerActive    = false;

    applyHardware(zi, z.savedCmd);
  }
}

void ZoneManager::_handleLocked(uint8_t zi) {
  // En état LOCKED : on s'assure que les sorties restent en ECO (sécurité)
  ZoneData& z = _zones[zi];
  if (z.currentCmd != HeatingCmd::ECO) {
    z.currentCmd = HeatingCmd::ECO;
    applyHardware(zi, HeatingCmd::ECO);
    LOG(LOG_ZONE, "Zone %d LOCKED — forced ECO.", z.id);
  }
}

// =============================================================================
// TIMER CM2 (Confort -2°C)
// Alternance : 7s en CONF puis 293s en ECO, en boucle
// =============================================================================

void ZoneManager::_updateCm2Timer(uint8_t zi) {
  ZoneData& z = _zones[zi];
  unsigned long elapsed = millis() - z.timerStart;

  if (z.cm2Mode == Cm2Mode::MODE_7S &&
      elapsed >= CM2_INTERVAL_7S) {
    // Fin phase 7s → passe en 293s (ECO)
    z.cm2Mode    = Cm2Mode::MODE_293S;
    z.timerStart = millis();
    applyHardware(zi, HeatingCmd::ECO);
    LOG(LOG_ZONE, "Zone %d CM2 → 293s (ECO phase)", z.id);
  }
  else if (z.cm2Mode == Cm2Mode::MODE_293S &&
           elapsed >= CM2_INTERVAL_293S) {
    // Fin phase 293s → repasse en 7s (CONF)
    z.cm2Mode    = Cm2Mode::MODE_7S;
    z.timerStart = millis();
    applyHardware(zi, HeatingCmd::CONF);
    LOG(LOG_ZONE, "Zone %d CM2 → 7s (CONF phase)", z.id);
  }
}

// =============================================================================
// OVERLOAD
// =============================================================================

void ZoneManager::startOverride(uint8_t zoneId, HeatingCmd fallbackCmd) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;
  ZoneData& z = _zones[zi];

  if (z.state == ZoneState::OVERRIDE) return;  // Déjà en override

  z.savedCmd     = z.currentCmd;
  z.state        = ZoneState::OVERRIDE;
  z.overloadStart = millis();
  z.timerActive  = false;

  CommandContext ctx{};
  ctx.zone     = zoneId;
  ctx.cmd      = fallbackCmd;
  ctx.inputCmd = fallbackCmd;
  ctx.origin   = CommandOrigin::SYSTEM;
  ctx.source   = CommandSource::SRC_OVERLOAD;
  ctx.priority = CommandPriority::PRIO_OVERLOAD;

  applyHardware(zi, fallbackCmd);
  z.currentCmd     = fallbackCmd;
  z.activePriority = CommandPriority::PRIO_OVERLOAD;

  LOG(LOG_ZONE, "Zone %d OVERRIDE start → %s (was: %s)",
      zoneId,
      heatingCmdToStr(fallbackCmd),
      heatingCmdToStr(z.savedCmd));
}

void ZoneManager::endOverride(uint8_t zoneId) {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  uint8_t zi = zoneId - 1;
  ZoneData& z = _zones[zi];

  if (z.state != ZoneState::OVERRIDE) return;

  LOG(LOG_ZONE, "Zone %d OVERRIDE end → RESTORE (→ %s)",
      zoneId, heatingCmdToStr(z.savedCmd));

  z.state        = ZoneState::RESTORE;
  z.restoreTimer = millis();
}

// =============================================================================
// FAULT
// =============================================================================

void ZoneManager::lockAllZones() {
  LOG(LOG_ZONE, "FAULT: locking all zones → ECO");
  for (uint8_t zi = 0; zi < NUM_ZONES; zi++) {
    _zones[zi].state      = ZoneState::LOCKED;
    _zones[zi].timerActive = false;
    applyHardware(zi, HeatingCmd::ECO);
  }
}

void ZoneManager::unlockAllZones() {
  LOG(LOG_ZONE, "FAULT cleared: unlocking all zones");
  for (uint8_t zi = 0; zi < NUM_ZONES; zi++) {
    _zones[zi].state          = ZoneState::NORMAL;
    _zones[zi].activePriority = CommandPriority::PRIO_DEFAULT;
  }
}

// =============================================================================
// ACCESSEURS
// =============================================================================

const ZoneData& ZoneManager::getZone(uint8_t zoneId) const {
  return _zones[constrain(zoneId - 1, 0, NUM_ZONES - 1)];
}

ZoneData& ZoneManager::getZoneMutable(uint8_t zoneId) {
  return _zones[constrain(zoneId - 1, 0, NUM_ZONES - 1)];
}

ZoneState ZoneManager::getZoneState(uint8_t zoneId) const {
  return _zones[constrain(zoneId - 1, 0, NUM_ZONES - 1)].state;
}

HeatingCmd ZoneManager::getCurrentCmd(uint8_t zoneId) const {
  return _zones[constrain(zoneId - 1, 0, NUM_ZONES - 1)].currentCmd;
}

bool ZoneManager::isOverrideActive(uint8_t zoneId) const {
  return _zones[constrain(zoneId - 1, 0, NUM_ZONES - 1)].state
         == ZoneState::OVERRIDE;
}

// =============================================================================
// HARDWARE PRIVÉ
// =============================================================================

void ZoneManager::_cmdToMoc(HeatingCmd cmd, bool& pos, bool& neg) const {
  // MOC3041 actif bas : true = sortie HIGH (MOC bloqué), false = LOW (MOC passant)
  // Fil pilote 230V :
  //   STOP  → demi-onde négative  : pos=LOW(passant),  neg=HIGH(bloqué)  → L/H
  //   HG    → demi-onde positive  : pos=HIGH(bloqué),  neg=LOW(passant)  → H/L
  //   ECO   → pas de signal       : pos=LOW(passant),  neg=LOW(passant)  → L/L
  //   CONF  → signal complet      : pos=HIGH(bloqué),  neg=HIGH(bloqué) → H/H
  switch (cmd) {
    case HeatingCmd::STOP: pos = false; neg = true;  break;
    case HeatingCmd::HG:   pos = true;  neg = false; break;
    case HeatingCmd::ECO:  pos = false; neg = false; break;
    case HeatingCmd::CONF: pos = true;  neg = true;  break;
    default:               pos = false; neg = false; break; // ECO par défaut
  }
}

void ZoneManager::_setMocOutputs(uint8_t zi, bool pos, bool neg) {
  // MOC actif bas : on inverse la logique booléenne
  // pos=true  → pin HIGH (MOC bloqué)
  // pos=false → pin LOW  (MOC passant)
  digitalWrite(PIN_MOC_POS[zi], pos ? HIGH : LOW);
  digitalWrite(PIN_MOC_NEG[zi], neg ? HIGH : LOW);
}

bool ZoneManager::_canOverride(uint8_t zi,
                                CommandPriority newPriority) const {
  // STOP USER est toujours accepté — priorité absolue sur Tempo/Overload
  // (cas fenêtres ouvertes : on veut pouvoir éteindre même en période rouge)
  return (uint8_t)newPriority >= (uint8_t)_zones[zi].activePriority;
}

// =============================================================================
// DEBUG
// =============================================================================

void ZoneManager::dumpZone(uint8_t zoneId) const {
  if (zoneId < 1 || zoneId > NUM_ZONES) return;
  const ZoneData& z = _zones[zoneId - 1];

  Serial.printf(
    "%s Zone %d | state=%-8s cmd=%-5s prio=%d pos=%d neg=%d timer=%d cm2=%s\n",
    LOG_ZONE,
    z.id,
    zoneStateToStr(z.state),
    heatingCmdToStr(z.currentCmd),
    (uint8_t)z.activePriority,
    z.posState,
    z.negState,
    z.timerActive,
    z.cm2Mode == Cm2Mode::MODE_7S ? "7S" : "293S"
  );
}

void ZoneManager::dumpAll() const {
  Serial.printf("%s === Zone Status ===\n", LOG_ZONE);
  for (uint8_t i = 1; i <= NUM_ZONES; i++) {
    dumpZone(i);
  }
}
