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
  // Horloge locale (tick minute du ClockManager, heure NTP valide)
  // Déclenche les index journaliers. La publication périodique des
  // valeurs est cadencée par update() et ne dépend plus de l'heure.
  // ---------------------------------------------------------------------------

  void onClock(uint8_t hh, uint8_t mm, uint8_t day, uint8_t month);
  void onIndexAck(uint8_t ackDay);
  void requestIndexPublish();  

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

  // Supervision : âge de la dernière trame complète, compteurs de qualité
  uint32_t lastFrameAgeMs() const {
    return _lastFrameMs ? (uint32_t)(millis() - _lastFrameMs) : UINT32_MAX;
  }
  bool     isDataFresh()    const { return lastFrameAgeMs() < LINKY_STALE_MS; }
  uint32_t getFrameCount()  const { return _frameCount; }
  uint32_t getBadLineCount() const { return _badLineCount; }
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

  // Qualité de liaison
  uint32_t _frameCount   = 0;
  uint32_t _badLineCount = 0;

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
  uint8_t  _lastHour        = 255;
  uint32_t _lastValuesPubMs = 0;       // cadence de _publishValues()
  static constexpr uint32_t VALUES_PUBLISH_MS = 60000;

  // Publication index journalier
  bool     _indexPublished  = false;  // vrai UNIQUEMENT sur ACK métier reçu
  bool     _awaitingAck     = false;  // un envoi est en attente de confirmation
 
  int8_t   _lastIndexMinute = -1;  
  uint32_t _lastAttemptMs   = 0;       // horodatage du dernier essai (backoff)
  uint8_t  _indexSentDay    = 0;       // jour (1..31) de la trame en attente d'ACK
  uint8_t  _indexAttempts   = 0;
  uint8_t  _indexTries      = 0;       // ← compteur de tentatives (pour le bilan)

  static constexpr uint8_t INDEX_PUBLISH_HOUR = 23;
  static constexpr uint32_t INDEX_ACK_TIMEOUT_MS = 20000;  // 20 s avant retry

  // Anti-doublon PTEC / DEMAIN
  char     _lastPtec[5]   = {0};
  char     _lastDemain[5] = {0};

  // Timeout connexion — déconnecté si pas de trame depuis 10s
  unsigned long _lastFrameMs = 0;
  static constexpr unsigned long LINKY_TIMEOUT_MS = 10000;

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
