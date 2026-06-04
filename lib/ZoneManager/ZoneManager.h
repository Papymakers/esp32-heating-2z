#pragma once
// =============================================================================
// ZoneManager.h — Machine à états par zone (x4)
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Responsabilités :
//   - Maintenir l'état FSM de chaque zone
//   - Appliquer les commandes sur les sorties MOC3041
//   - Gérer le timer CM2 (alternance 7s/293s)
//   - Gérer les transitions OVERRIDE / RESTORE
//   - Respecter les priorités de commande
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// =============================================================================
// CLASSE ZONEMANAGER
// =============================================================================

class ZoneManager {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  // Initialise toutes les zones (pinMode, état par défaut)
  void begin();

  // Initialise une zone spécifique
  void initZone(uint8_t zoneId);

  // ---------------------------------------------------------------------------
  // Commandes
  // ---------------------------------------------------------------------------

  // Point d'entrée principal : applique une commande sur une zone
  // Retourne true si la commande a été acceptée (priorité suffisante)
  bool applyCommand(const CommandContext& ctx);

  // Applique directement une commande hardware (sans vérif priorité)
  // Utilisé en interne et par le boot/recovery
  void applyHardware(uint8_t zoneIndex, HeatingCmd cmd);

  // Commande toutes les zones (ex: FAULT → STOP toutes zones)
  void applyAll(HeatingCmd cmd, CommandOrigin origin, CommandSource source);

  // ---------------------------------------------------------------------------
  // FSM Loop — à appeler dans loop()
  // ---------------------------------------------------------------------------

  void update();

  // ---------------------------------------------------------------------------
  // Accesseurs état
  // ---------------------------------------------------------------------------

  const ZoneData& getZone(uint8_t zoneId) const;
  ZoneData&       getZoneMutable(uint8_t zoneId);
  ZoneState       getZoneState(uint8_t zoneId) const;
  HeatingCmd      getCurrentCmd(uint8_t zoneId) const;
  bool            isOverrideActive(uint8_t zoneId) const;

  // ---------------------------------------------------------------------------
  // Overload management
  // ---------------------------------------------------------------------------

  // Déclenche un override sur une zone (surcharge)
  void startOverride(uint8_t zoneId, HeatingCmd fallbackCmd);

  // Restaure la commande sauvegardée après override
  void endOverride(uint8_t zoneId);

  // ---------------------------------------------------------------------------
  // FAULT système
  // ---------------------------------------------------------------------------

  void lockAllZones();    // FAULT : verrouille toutes les zones
  void unlockAllZones();  // Sortie FAULT : déverrouille

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dumpZone(uint8_t zoneId) const;
  void dumpAll() const;

private:

  ZoneData _zones[NUM_ZONES];

  // ---------------------------------------------------------------------------
  // FSM transitions internes
  // ---------------------------------------------------------------------------

  void _handleNormal (uint8_t zi);   // Traitement état NORMAL
  void _handleOverride(uint8_t zi);  // Traitement état OVERRIDE
  void _handleRestore (uint8_t zi);  // Traitement état RESTORE
  void _handleLocked  (uint8_t zi);  // Traitement état LOCKED

  // ---------------------------------------------------------------------------
  // Timer CM2
  // ---------------------------------------------------------------------------

  void _updateCm2Timer(uint8_t zi);

  // ---------------------------------------------------------------------------
  // Hardware
  // ---------------------------------------------------------------------------

  // Applique pos/neg sur les MOC3041 selon la commande
  // MOC actif bas : OUTPUT LOW = MOC passant
  void _setMocOutputs(uint8_t zi, bool pos, bool neg);

  // Résout pos/neg depuis HeatingCmd
  void _cmdToMoc(HeatingCmd cmd, bool& pos, bool& neg) const;

  // Vérifie si une nouvelle commande peut remplacer l'active (priorité)
  bool _canOverride(uint8_t zi, CommandPriority newPriority) const;
};

// Instance globale (extern pour accès depuis les autres modules)
extern ZoneManager zoneManager;
