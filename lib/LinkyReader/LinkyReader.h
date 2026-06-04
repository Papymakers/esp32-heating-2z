#pragma once
// =============================================================================
// LinkyReader.h — Lecture TIC Linky mode historique
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
//
// Mode historique Linky (consommateur) :
//   - Liaison série 1200 bauds, 7 bits, parité paire, 1 stop bit (7E1)
//   - Trame délimitée par STX (0x02) / ETX (0x03)
//   - Groupes délimités par LF (0x0A) / CR (0x0D)
//   - Format ligne : <étiquette> <SP> <valeur> <SP> <checksum>
//
// Étiquettes utilisées :
//   ISOUSC  → intensité souscrite (A)
//   IINST   → intensité instantanée (A)
//   PTEC    → période tarifaire courante (ex: "HPJB")
//   DEMAIN  → couleur du lendemain ("BLEU","BLAN","ROUG","----")
//   BBRHCJB → index HC Jour Bleu  (Wh)
//   BBRHPJB → index HP Jour Bleu  (Wh)
//   BBRHCJW → index HC Jour Blanc (Wh)
//   BBRHPJW → index HP Jour Blanc (Wh)
//   BBRHCJR → index HC Jour Rouge (Wh)
//   BBRHPJR → index HP Jour Rouge (Wh)
//
// Callbacks vers les autres modules :
//   → TempoManager::onPtecChange()
//   → TempoManager::onDemainChange()
//   → Publisher::publishLinkyValues()
//   → Publisher::publishLinkyIndexes()
//   → OverloadManager::onIinstUpdate()
// =============================================================================

#include <Arduino.h>
#include "types.h"
#include "config.h"

// Forward declarations
class Publisher;
class TempoManager;
class OverloadManager;

// =============================================================================
// SNAPSHOT TRAME LINKY
// Données courantes lues depuis le compteur
// =============================================================================

struct LinkySnapshot {
  uint8_t  iinst;           // Intensité instantanée (A)
  uint8_t  isousc;          // Intensité souscrite (A)
  char     ptec[5];         // Période tarifaire "HPJB" etc.
  char     demain[5];       // Couleur demain "BLEU","BLAN","ROUG","----"
  bool     overload;        // iinst >= isousc

  // Index compteur (Wh)
  long     bbrhcjb;         // HC Jour Bleu
  long     bbrhpjb;         // HP Jour Bleu
  long     bbrhcjw;         // HC Jour Blanc
  long     bbrhpjw;         // HP Jour Blanc
  long     bbrhcjr;         // HC Jour Rouge
  long     bbrhpjr;         // HP Jour Rouge

  bool     frameValid;      // Dernière trame complète valide
};

// =============================================================================
// OVERRIDE DEBUG (injection manuelle via MQTT)
// =============================================================================

struct LinkyOverride {
  uint8_t iinst;
  uint8_t isousc;
  char    ptec[5];
  char    demain[5];
  bool    active;
};

// =============================================================================
// CLASSE LINKYREADER
// =============================================================================

class LinkyReader {
public:

  // ---------------------------------------------------------------------------
  // Initialisation
  // ---------------------------------------------------------------------------

  void begin(Publisher*       publisher,
             TempoManager*    tempoMgr,
             OverloadManager* overloadMgr);

  // ---------------------------------------------------------------------------
  // Loop — à appeler dans loop()
  // ---------------------------------------------------------------------------

  void update();

  // ---------------------------------------------------------------------------
  // Horloge UTC (depuis Publisher/MQTT)
  // Déclenche la publication périodique et les index journaliers
  // ---------------------------------------------------------------------------

  void onClock(uint8_t hh, uint8_t mm, uint8_t day, uint8_t month);

  // ---------------------------------------------------------------------------
  // Override debug (injection manuelle via MQTT)
  // ---------------------------------------------------------------------------

  void setOverride(uint8_t     iinst,
                   uint8_t     isousc,
                   bool        active,
                   const char* ptec,
                   const char* demain);

  void clearOverride();

  // ---------------------------------------------------------------------------
  // Accesseurs
  // ---------------------------------------------------------------------------

  const LinkySnapshot& getSnapshot()  const { return _snap; }
  bool                 isConnected()  const { return _connected; }
  uint8_t              getIinst()     const { return _snap.iinst; }
  uint8_t              getIsousc()    const { return _snap.isousc; }
  bool                 isOverload()   const { return _snap.overload; }
  const char*          getPtec()      const { return _snap.ptec; }
  const char*          getDemain()    const { return _snap.demain; }

  // ---------------------------------------------------------------------------
  // Debug
  // ---------------------------------------------------------------------------

  void dump() const;

private:

  // ---------------------------------------------------------------------------
  // Parser TIC interne
  // ---------------------------------------------------------------------------

  void   _readSerial();
  void   _parseLine();
  void   _processLabel(const char* label, const char* val);
  bool   _validateChecksum(const char* line, int len, char checksum) const;

  // ---------------------------------------------------------------------------
  // Traitement des étiquettes
  // ---------------------------------------------------------------------------

  void _onIsousc(const char* val);
  void _onIinst (const char* val);
  void _onPtec  (const char* val);
  void _onDemain(const char* val);
  void _onIndex (const char* label, const char* val);

  // ---------------------------------------------------------------------------
  // Publication périodique
  // ---------------------------------------------------------------------------

  void _publishValues();          // Chaque minute : Iinst, Isousc, PTEC, DEMAIN
  void _publishIndexes();         // À 23h : index compteur
  void _publishIinst();           // Sur changement Iinst

  // ---------------------------------------------------------------------------
  // Moyenne glissante Iinst
  // ---------------------------------------------------------------------------

  void    _updateIinstAvg(uint8_t newVal);
  uint8_t _getIinstAvg() const;

  // ---------------------------------------------------------------------------
  // Membres privés
  // ---------------------------------------------------------------------------

  LinkySnapshot  _snap      {};
  LinkyOverride  _override  {};

  // Parser TIC
  char           _buf[32]   = {0};
  int            _bufIdx    = 0;
  bool           _inGroup   = false;
  bool           _connected = false;

  // Moyenne glissante Iinst
  static constexpr uint8_t AVG_SAMPLES = 3;
  uint8_t  _iinstSamples[AVG_SAMPLES] = {0};
  uint8_t  _iinstSampleIdx = 0;
  uint8_t  _iinstAvg       = 0;

  // Horloge
  uint8_t  _hh    = 0;
  uint8_t  _mm    = 0;
  uint8_t  _day   = 0;
  uint8_t  _month = 0;
  int8_t   _lastMinute      = -1;
  uint8_t  _lastHour        = 255;

  // Publication index journalier
  bool     _indexPublished  = false;
  uint8_t  _indexAttempts   = 0;
  int8_t   _lastIndexMinute = -1;
  static constexpr uint8_t MAX_INDEX_ATTEMPTS = 3;
  static constexpr uint8_t INDEX_PUBLISH_HOUR = 23;

  // Anti-doublon PTEC / DEMAIN
  char     _lastPtec[5]   = {0};
  char     _lastDemain[5] = {0};

  // Timeout connexion — déconnecté si pas de trame depuis 10s
  unsigned long _lastFrameMs = 0;
  static constexpr unsigned long LINKY_TIMEOUT_MS = 30000; // 30s

  // Dépendances
  Publisher*       _publisher   = nullptr;
  TempoManager*    _tempoMgr    = nullptr;
  OverloadManager* _overloadMgr = nullptr;

  // Table d'index TIC
  struct TicIndexEntry {
    const char* label;
    long*       value;
  };

  TicIndexEntry _indexTable[6] = {
    { "BBRHCJB", &_snap.bbrhcjb },
    { "BBRHPJB", &_snap.bbrhpjb },
    { "BBRHCJW", &_snap.bbrhcjw },
    { "BBRHPJW", &_snap.bbrhpjw },
    { "BBRHCJR", &_snap.bbrhcjr },
    { "BBRHPJR", &_snap.bbrhpjr },
  };
};

// Instance globale
extern LinkyReader linkyReader;
