// =============================================================================
// LinkyReader.cpp — Lecture TIC Linky mode historique
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include "LinkyReader.h"
#include "Publisher.h"
#include "TempoManager.h"
#include "OverloadManager.h"
#include "DisplayManager.h"

// Instance globale
LinkyReader linkyReader;

// =============================================================================
// INITIALISATION
// =============================================================================

void LinkyReader::begin(Publisher *publisher,
                        TempoManager *tempoMgr,
                        OverloadManager *overloadMgr)
{
  _publisher = publisher;
  _tempoMgr = tempoMgr;
  _overloadMgr = overloadMgr;

  // Serial1 : 1200 bauds, 7 bits, parité paire, 1 stop bit
  // Buffer RX agrandi AVANT begin() : 256 octets par défaut ne couvrent
  // que ~2 s de TIC ; 1024 octets absorbent un blocage de loop() de ~8 s.
  Serial1.setRxBufferSize(LINKY_RX_BUFFER);
  Serial1.begin(1200, SERIAL_7E1, PIN_LINKY_RX, PIN_LINKY_TX);

  // Init snapshot
  _snap = {};
  _snap.frameValid = false;
  _connected = false;

  memset(_iinstSamples, 0, sizeof(_iinstSamples));

  LOG(LOG_LINKY, "LinkyReader ready (RX=%d TX=%d 1200/7E1)",
      PIN_LINKY_RX, PIN_LINKY_TX);
}

// =============================================================================
// LOOP
// =============================================================================

void LinkyReader::update()
{
  // Priorité : override debug si actif
  if (_override.active)
  {
    _snap.iinst = _override.iinst;
    _snap.isousc = _override.isousc;
    _snap.overload = (_snap.iinst >= _snap.isousc);

    // Mise à jour PTEC si différent
    if (strcmp(_snap.ptec, _override.ptec) != 0)
    {
      strncpy(_snap.ptec, _override.ptec, sizeof(_snap.ptec) - 1);
      _snap.ptec[sizeof(_snap.ptec) - 1] = '\0';
      if (_tempoMgr)
        _tempoMgr->onPtecChange(_snap.ptec);
    }
    if (strcmp(_snap.demain, _override.demain) != 0)
    {
      strncpy(_snap.demain, _override.demain, sizeof(_snap.demain) - 1);
      _snap.demain[sizeof(_snap.demain) - 1] = '\0';
      if (_tempoMgr)
        _tempoMgr->onDemainChange(_snap.demain);
    }

    _override.active = false; // Consomme l'override (one-shot)

    if (_overloadMgr)
    {
      _overloadMgr->onIinstUpdate(_snap.iinst,
                                  _snap.isousc,
                                  _snap.overload);
    }
    return;
  }

  // Lecture Serial TIC
  if (Serial1.available() > 0)
  {
    _readSerial();
  }

  // Publication périodique des valeurs (v5.1 : indépendante de l'heure,
  // auparavant déclenchée par le message MQTT utcClock — sans horloge
  // maître, rien n'était publié)
  if (millis() - _lastValuesPubMs >= VALUES_PUBLISH_MS)
  {
    _lastValuesPubMs = millis();
    _publishValues();
  }

  // Timeout connexion Linky
  if (_connected && _lastFrameMs > 0 &&
      millis() - _lastFrameMs > LINKY_TIMEOUT_MS)
  {
    _connected = false;
    displayManager.setLinkyStatus(false);
    LOG_W(LOG_LINKY, "Linky timeout — disconnected");
  }
}

// =============================================================================
// HORLOGE LOCALE (tick minute NTP)
// =============================================================================

void LinkyReader::onClock(uint8_t hh, uint8_t mm, uint8_t day, uint8_t month){

  _hh = hh;
  _mm = mm;
  _day = day;
  _month = month;

  // ── Changement d'heure ────────────────────────────────────────────────────
  if (hh != _lastHour)
  {
    _lastHour = hh;

    if (hh != INDEX_PUBLISH_HOUR)
    {
      // Bilan d'échec si la fenêtre de 23h se ferme sans confirmation
      if (_awaitingAck && !_indexPublished && _indexTries > 0)
      {
        LOG_W(LOG_LINKY, "⚠️ Index NON confirmé jour %u après %u tentatives", _indexSentDay, _indexTries);
        if (_publisher)
        {
          _publisher->publishLog("WARN", "LINKY",
                                 "Index NON confirme jour %u apres %u tentatives", _indexSentDay, _indexTries);
        }
      }
      _indexPublished = false;
      _awaitingAck = false;
      _lastIndexMinute = -1;
      _indexTries = 0;
    }
  }

  // ── Publication index journalier à 23hxx (avec ACK métier) ──────────────
  if (hh == INDEX_PUBLISH_HOUR && !_indexPublished)
  {

    // Nouvelle tentative autorisée si on n'attend pas d'ACK,
    // ou si le délai d'attente de l'ACK précédent est écoulé.
    bool peutRetenter = !_awaitingAck || (millis() - _lastAttemptMs) > INDEX_ACK_TIMEOUT_MS;

    // Au plus une tentative par minute distincte, tant que l'ACK n'est pas venu.
    // Pas de plafond : la fenêtre se ferme d'elle-même à minuit (reset des drapeaux).
    if (peutRetenter && mm != _lastIndexMinute)
    {
      _lastIndexMinute = mm;
      _indexTries++;
      LOG_D(LOG_LINKY, "Index publish attempt #%u at %02d:%02d", _indexTries, hh, mm);
      _publishIndexes();
    }
  }
}
  // =============================================================================
  // OVERRIDE DEBUG
  // =============================================================================

  void LinkyReader::setOverride(uint8_t iinst,
                                uint8_t isousc,
                                bool active,
                                const char *ptec,
                                const char *demain)
  {
    _override.iinst = iinst;
    _override.isousc = isousc;
    _override.active = active;

    strncpy(_override.ptec, ptec ? ptec : "", sizeof(_override.ptec) - 1);
    strncpy(_override.demain, demain ? demain : "", sizeof(_override.demain) - 1);
    _override.ptec[sizeof(_override.ptec) - 1] = '\0';
    _override.demain[sizeof(_override.demain) - 1] = '\0';

    LOG(LOG_LINKY, "Override set: Iinst=%u Isousc=%u ptec=%s demain=%s",
        iinst, isousc, _override.ptec, _override.demain);
  }

  void LinkyReader::clearOverride()
  {
    _override.active = false;
    LOG(LOG_LINKY, "Override cleared");
  }

  // =============================================================================
  // PARSER TIC INTERNE
  // =============================================================================

  void LinkyReader::_readSerial()
  {
    while (Serial1.available() > 0)
    {
      int v = Serial1.read();

      // ── STX : début de trame ────────────────────────────────────────────────
      if (v == 0x02)
      {
        _inGroup = false;
        _connected = false; // Trame en cours → LED éteinte
        displayManager.setLinkyStatus(false);
        continue;
      }

      // ── ETX : fin de trame ──────────────────────────────────────────────────
      if (v == 0x03)
      {
        _connected = true; // Trame terminée → LED allumée
        _snap.frameValid = true;
        _lastFrameMs = millis();
        _frameCount++;
        displayManager.setLinkyStatus(true);
        continue;
      }

      // ── LF : début de groupe ────────────────────────────────────────────────
      if (v == 0x0A)
      {
        _inGroup = true;
        _bufIdx = 0;
        continue;
      }

      // ── CR : fin de groupe → parse ──────────────────────────────────────────
      if (v == 0x0D && _inGroup)
      {
        _inGroup = false;
        _buf[_bufIdx] = '\0';
        _parseLine();
        _bufIdx = 0;
        continue;
      }

      // ── Caractères de la ligne ───────────────────────────────────────────────
      if (_inGroup && _bufIdx < (int)sizeof(_buf) - 1)
      {
        _buf[_bufIdx++] = (char)v;
      }
    }
  }

  void LinkyReader::_parseLine()
  {
    // Format mode historique : "<étiquette> SP <valeur> SP <checksum>"
    // Le checksum est TOUJOURS le dernier caractère, précédé d'un SP.
    // Il peut lui-même valoir ' ' (0x20) : on ne peut donc pas découper
    // la ligne sur les espaces pour le trouver.
    int len = strlen(_buf);
    if (len < 5 || _buf[len - 2] != ' ')
    {
      _badLineCount++;
      return;
    }

    char chk = _buf[len - 1];
    int dataLen = len - 2;   // étiquette + SP + valeur (sans le SP final)

    if (!_validateChecksum(_buf, dataLen, chk))
    {
      _badLineCount++;
      LOG_W(LOG_LINKY, "Bad checksum on '%.*s'", dataLen, _buf);
      return;
    }

    // Découpage étiquette / valeur sur le premier espace
    const char *sp = (const char *)memchr(_buf, ' ', dataLen);
    if (!sp)
    {
      _badLineCount++;
      return;
    }

    char label[12] = {0};
    char val[14] = {0};
    int labelLen = sp - _buf;
    int valLen = dataLen - labelLen - 1;
    if (labelLen <= 0 || valLen <= 0 ||
        labelLen >= (int)sizeof(label) || valLen >= (int)sizeof(val))
    {
      _badLineCount++;
      return;
    }
    memcpy(label, _buf, labelLen);
    memcpy(val, sp + 1, valLen);

    _processLabel(label, val);
  }

  void LinkyReader::_processLabel(const char *label, const char *val)
  {
    if (strcmp(label, "ISOUSC") == 0)
    {
      _onIsousc(val);
      return;
    }
    if (strcmp(label, "IINST") == 0)
    {
      _onIinst(val);
      return;
    }
    if (strcmp(label, "PTEC") == 0)
    {
      _onPtec(val);
      return;
    }
    if (strcmp(label, "DEMAIN") == 0)
    {
      _onDemain(val);
      return;
    }

    // Index compteur (table de correspondance)
    _onIndex(label, val);
  }

  bool LinkyReader::_validateChecksum(const char *line,
                                      int len,
                                      char expected) const
  {
    // Checksum TIC mode historique (méthode 1) :
    //   (somme ASCII de étiquette + SP + valeur) & 0x3F + 0x20
    // Certains compteurs incluent aussi le SP final (méthode 2) :
    // on accepte les deux, ce qui garde la détection des corruptions.
    unsigned sum = 0;
    for (int i = 0; i < len; i++)
    {
      sum += (uint8_t)line[i];
    }
    char m1 = (char)((sum & 0x3F) + 0x20);
    char m2 = (char)(((sum + ' ') & 0x3F) + 0x20);
    return expected == m1 || expected == m2;
  }

  // =============================================================================
  // TRAITEMENT DES ÉTIQUETTES
  // =============================================================================

  void LinkyReader::_onIsousc(const char *val)
  {
    uint8_t newVal = (uint8_t)atoi(val);
    if (newVal == _snap.isousc)
      return;

    _snap.isousc = newVal;
    LOG_D(LOG_LINKY, "ISOUSC=%u A", newVal);
  }

  void LinkyReader::_onIinst(const char *val)
  {
    uint8_t newVal = (uint8_t)atoi(val);

    // Moyenne glissante sur AVG_SAMPLES mesures
    _updateIinstAvg(newVal);
    uint8_t avg = _getIinstAvg();

    // Isousc encore inconnu (0) → pas de décision de surcharge possible
    bool newOverload = (_snap.isousc > 0) && (avg >= _snap.isousc);
    bool changed = (newVal != _snap.iinst) ||
                   (newOverload != _snap.overload);

    _snap.iinst = newVal;
    _snap.overload = newOverload;

    if (changed)
    {
      _publishIinst();

      if (_overloadMgr)
      {
        _overloadMgr->onIinstUpdate(newVal, _snap.isousc, newOverload);
      }
    }
  }

  void LinkyReader::_onPtec(const char *val)
  {
    if (!val || strlen(val) < 4)
      return;
    if (strncmp(_lastPtec, val, sizeof(_lastPtec) - 1) == 0)
      return;

    strncpy(_lastPtec, val, sizeof(_lastPtec) - 1);
    strncpy(_snap.ptec, val, sizeof(_snap.ptec) - 1);
    _lastPtec[sizeof(_lastPtec) - 1] = '\0';
    _snap.ptec[sizeof(_snap.ptec) - 1] = '\0';

    LOG_D(LOG_LINKY, "PTEC='%s'", _snap.ptec);

    if (_tempoMgr)
    {
      _tempoMgr->onPtecChange(_snap.ptec);
    }
  }

  void LinkyReader::_onDemain(const char *val)
  {
    if (!val || val[0] == '\0')
      return;
    if (strncmp(_lastDemain, val, sizeof(_lastDemain) - 1) == 0)
      return;

    strncpy(_lastDemain, val, sizeof(_lastDemain) - 1);
    strncpy(_snap.demain, val, sizeof(_snap.demain) - 1);
    _lastDemain[sizeof(_lastDemain) - 1] = '\0';
    _snap.demain[sizeof(_snap.demain) - 1] = '\0';

    LOG_D(LOG_LINKY, "DEMAIN='%s'", _snap.demain);

    if (_tempoMgr)
    {
      _tempoMgr->onDemainChange(_snap.demain);
    }
  }

  void LinkyReader::_onIndex(const char *label, const char *val)
  {
    if (!val || !*val)
      return;
    long v = atol(val);

    for (uint8_t i = 0; i < 6; i++)
    {
      if (strcmp(label, _indexTable[i].label) == 0)
      {
        *(_indexTable[i].value) = v;
        return;
      }
    }
  }

  void LinkyReader::onIndexAck(uint8_t ackDay) {
  if (_awaitingAck && ackDay == _indexSentDay) {
    _indexPublished = true;
    _awaitingAck    = false;
    LOG(LOG_LINKY, "✅ ACK reçu pour jour %u (après %u tentative(s))", ackDay, _indexTries);
    if (_indexTries > 1 && _publisher) {
      _publisher->publishLog("INFO", "LINKY",
        "Index confirme jour %u apres %u tentatives", ackDay, _indexTries);
    }
  }
}

  // =============================================================================
  // PUBLICATIONS
  // =============================================================================
  void LinkyReader::requestIndexPublish()
  {
    if (_awaitingAck)
    {
      LOG(LOG_LINKY, "Forçage ignoré : une publication attend déjà son ACK");
      return;
    }
    _indexTries = 0;
    LOG(LOG_LINKY, "Forçage : publication unique des index");
    _publishIndexes(); // publie une fois, arme _awaitingAck pour recevoir l'ACK
  }

  void LinkyReader::_publishValues()
  {
    if (!_publisher)
      return;
    if (!_publisher->isMqttConnected())
    {
      LOG_D(LOG_LINKY, "MQTT not connected — skip publishValues");
      return;
    }

    _publisher->publishLinkyValues(_snap.iinst,
                                   _snap.isousc,
                                   _snap.ptec,
                                   _snap.demain,
                                   _snap.overload);

    LOG_D(LOG_LINKY, "Values published: Iinst=%u Isousc=%u PTEC=%s DEMAIN=%s",
        _snap.iinst, _snap.isousc, _snap.ptec, _snap.demain);
  }

  void LinkyReader::_publishIndexes()
  {
    if (!_publisher)
      return;
    if (!_publisher->isMqttConnected())
    {
      LOG_D(LOG_LINKY, "MQTT not connected — skip publishIndexes");
      // _indexPublished = false;  // Retry autorisé
      return;
    }

    _publisher->publishLinkyIndexes(_snap.bbrhcjb, _snap.bbrhpjb,
                                    _snap.bbrhcjw, _snap.bbrhpjw,
                                    _snap.bbrhcjr, _snap.bbrhpjr);

    _awaitingAck = true;
    _lastAttemptMs = millis();
    _indexSentDay = _day; // le jour courant (adapte au nom de ta variable de quantième)

    LOG_D(LOG_LINKY, "Index published at %02d:%02d — awaiting ACK for day %u", _hh, _mm, _indexSentDay);
    // _indexPublished = true;
  }

  void LinkyReader::_publishIinst()
  {
    if (!_publisher)
      return;

    // Publication légère Iinst uniquement (pas de broadcast complet)
    _publisher->publishLinkyValues(_snap.iinst,
                                   _snap.isousc,
                                   _snap.ptec,
                                   _snap.demain,
                                   _snap.overload);
  }

  // =============================================================================
  // MOYENNE GLISSANTE IINST
  // =============================================================================

  void LinkyReader::_updateIinstAvg(uint8_t newVal)
  {
    _iinstSamples[_iinstSampleIdx] = newVal;
    _iinstSampleIdx = (_iinstSampleIdx + 1) % AVG_SAMPLES;

    uint16_t sum = 0;
    for (uint8_t i = 0; i < AVG_SAMPLES; i++)
    {
      sum += _iinstSamples[i];
    }
    _iinstAvg = (uint8_t)(sum / AVG_SAMPLES);
  }

  uint8_t LinkyReader::_getIinstAvg() const
  {
    return _iinstAvg;
  }

  // =============================================================================
  // DEBUG
  // =============================================================================

  void LinkyReader::dump() const
  {
  if (!LOG_RAW_ENABLED()) return;   // dump série = niveau DEBUG
    Serial.printf(
        "%s === Linky Snapshot ===\n"
        "%s   connected : %d  frameValid: %d\n"
        "%s   Iinst     : %u A (avg: %u A)\n"
        "%s   Isousc    : %u A\n"
        "%s   overload  : %d\n"
        "%s   PTEC      : '%s'\n"
        "%s   DEMAIN    : '%s'\n"
        "%s   BBRHCJB   : %ld  BBRHPJB: %ld\n"
        "%s   BBRHCJW   : %ld  BBRHPJW: %ld\n"
        "%s   BBRHCJR   : %ld  BBRHPJR: %ld\n"
        "%s ====================\n",
        LOG_LINKY,
        LOG_LINKY, _connected, _snap.frameValid,
        LOG_LINKY, _snap.iinst, _iinstAvg,
        LOG_LINKY, _snap.isousc,
        LOG_LINKY, _snap.overload,
        LOG_LINKY, _snap.ptec[0] ? _snap.ptec : "—",
        LOG_LINKY, _snap.demain[0] ? _snap.demain : "—",
        LOG_LINKY, _snap.bbrhcjb, _snap.bbrhpjb,
        LOG_LINKY, _snap.bbrhcjw, _snap.bbrhpjw,
        LOG_LINKY, _snap.bbrhcjr, _snap.bbrhpjr,
        LOG_LINKY);
  }