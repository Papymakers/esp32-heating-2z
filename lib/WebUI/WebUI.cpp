// =============================================================================
// WebUI.cpp — Serveur HTTP + Interface web
// ESP32 Heating Controller v4.0
// Denis Mattera - 2025
// =============================================================================

#include "WebUI.h"
#include "ZoneManager.h"
#include "CommandHandler.h"
#include "TempoManager.h"
#include "ScheduleManager.h"
#include "OverloadManager.h"
#include "StorageManager.h"
#include "DisplayManager.h"
#include "Publisher.h"
#include "LinkyReader.h"
#include <WiFi.h>

// Instance globale
WebUI webUI;

// =============================================================================
// PAGE HTML EMBARQUÉE (PROGMEM)
// Interface complète : dashboard + éditeur calendrier
// =============================================================================

const char WebUI::HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Heating Controller v4</title>
<style>
  :root {
    --bg: #1a1a2e; --card: #16213e; --accent: #0f3460;
    --blue: #4fc3f7; --green: #81c784; --orange: #ffb74d;
    --red: #e57373; --white: #e0e0e0; --dim: #888;
    --radius: 8px; --font: 'Segoe UI', sans-serif;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: var(--bg); color: var(--white);
         font-family: var(--font); min-height: 100vh; }
  h1 { text-align: center; padding: 16px; font-size: 1.2em;
       color: var(--blue); border-bottom: 1px solid var(--accent); }
  h2 { font-size: 0.95em; color: var(--dim); margin-bottom: 8px; }
  .container { max-width: 900px; margin: 0 auto; padding: 12px; }
  .grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
  .grid4 { display: grid; grid-template-columns: repeat(2,1fr); gap: 8px; }
  .card { background: var(--card); border-radius: var(--radius);
          padding: 12px; border: 1px solid var(--accent); }

  /* --- Zones --- */
  .zone-card { text-align: center; cursor: pointer; transition: .2s; }
  .zone-card:hover { border-color: var(--blue); }
  .zone-card.selected { border-color: var(--blue); background: #0d2137; }
  .zone-name { font-size: 0.8em; color: var(--dim); margin-bottom: 4px; }
  .zone-cmd  { font-size: 1.3em; font-weight: bold; margin: 6px 0; }
  .zone-state { font-size: 0.7em; padding: 2px 6px; border-radius: 4px; }
  .zone-inactive { color: var(--dim); }
  .cmd-CONF { color: var(--green); }
  .cmd-ECO  { color: var(--blue); }
  .cmd-HG   { color: var(--orange); }
  .cmd-STOP { color: var(--red); }
  .cmd-CM2  { color: #ce93d8; }
  .state-OVERRIDE { background: #b71c1c; color: #fff; }
  .state-RESTORE  { background: #e65100; color: #fff; }
  .state-LOCKED   { background: #4a148c; color: #fff; }
  .state-NORMAL   { background: #1b5e20; color: #fff; }

  /* --- Boutons commandes --- */
  .cmd-btns { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 8px; }
  .btn { padding: 6px 12px; border: none; border-radius: 6px;
         cursor: pointer; font-size: 0.85em; font-weight: bold;
         transition: .15s; }
  .btn:hover { opacity: .85; }
  .btn-CONF { background: var(--green); color: #000; }
  .btn-ECO  { background: var(--blue);  color: #000; }
  .btn-HG   { background: var(--orange); color: #000; }
  .btn-STOP { background: var(--red);   color: #fff; }
  .btn-CM2  { background: #9c27b0;      color: #fff; }
  .btn-sec  { background: var(--accent); color: var(--white); }

  /* --- Status bar --- */
  .status-bar { display: flex; gap: 12px; flex-wrap: wrap;
                font-size: 0.8em; padding: 8px 0; }
  .badge { padding: 3px 8px; border-radius: 4px; }
  .badge.ok  { background: #1b5e20; }
  .badge.ko  { background: #7f0000; }
  .badge.warn{ background: #e65100; }

  /* --- Toggles --- */
  .toggle-row { display: flex; align-items: center;
                justify-content: space-between; margin: 6px 0; }
  .toggle { position: relative; width: 44px; height: 24px; }
  .toggle input { opacity: 0; width: 0; height: 0; }
  .slider { position: absolute; inset: 0; background: #555;
            border-radius: 24px; cursor: pointer; transition: .3s; }
  .slider:before { content: ''; position: absolute;
                   width: 18px; height: 18px; bottom: 3px; left: 3px;
                   background: #fff; border-radius: 50%; transition: .3s; }
  input:checked + .slider { background: var(--blue); }
  input:checked + .slider:before { transform: translateX(20px); }

  /* --- Calendrier --- */
  #calendar-section { margin-top: 16px; }
  .profile-header { display: flex; gap: 8px; align-items: center;
                    margin-bottom: 8px; flex-wrap: wrap; }
  .profile-select { background: var(--accent); color: var(--white);
                    border: 1px solid var(--blue); border-radius: 6px;
                    padding: 4px 8px; }
  .profile-name-input { background: var(--accent); color: var(--white);
                        border: 1px solid #444; border-radius: 6px;
                        padding: 4px 8px; width: 120px; }
  .grid-wrapper { overflow-x: auto; }
  .slot-grid { display: grid;
               grid-template-columns: 40px repeat(48, 14px);
               gap: 1px; font-size: 10px; }
  .slot-header { background: var(--accent); color: var(--dim);
                 text-align: center; padding: 2px 0; user-select: none; }
  .slot-label { color: var(--dim); display: flex; align-items: center;
                padding-right: 4px; font-size: 9px; }
  .slot { width: 14px; height: 22px; cursor: pointer; border-radius: 2px;
          transition: opacity .1s; }
  .slot:hover { opacity: .75; }
  .slot[data-cmd="CONF"] { background: var(--green); }
  .slot[data-cmd="ECO"]  { background: #1565c0; }
  .slot[data-cmd="HG"]   { background: var(--orange); }
  .slot[data-cmd="STOP"] { background: var(--red); }
  .slot[data-cmd="CM2"]  { background: #7b1fa2; }
  .legend { display: flex; gap: 10px; margin: 8px 0; flex-wrap: wrap;
            font-size: 0.8em; }
  .legend-item { display: flex; align-items: center; gap: 4px; }
  .legend-dot { width: 12px; height: 12px; border-radius: 2px; }

  /* --- Zone/Profil assignation --- */
  .assign-grid { display: grid;
                 grid-template-columns: 60px repeat(7,1fr);
                 gap: 4px; font-size: 0.8em; }
  .assign-day { text-align: center; color: var(--dim); padding: 2px; }
  .assign-zone { color: var(--white); display: flex; align-items: center; }
  .assign-select { width: 100%; background: var(--accent); color: var(--white);
                   border: 1px solid #444; border-radius: 4px; padding: 2px; }

  /* --- Config --- */
  .config-row { display: flex; align-items: center;
                gap: 8px; margin: 6px 0; font-size: 0.85em; }
  .config-label { flex: 1; color: var(--dim); }
  .config-input { width: 70px; background: var(--accent); color: var(--white);
                  border: 1px solid #444; border-radius: 6px;
                  padding: 3px 6px; text-align: right; }

  @media (max-width: 600px) {
    .grid2, .grid4 { grid-template-columns: 1fr 1fr; }
  }
</style>
</head>
<body>
<h1>🔥 Heating Controller v4</h1>
<div class="container">

  <!-- STATUS BAR -->
  <div class="status-bar">
    <span id="badge-wifi"  class="badge ko">WiFi --</span>
    <span id="badge-mqtt"  class="badge ko">MQTT --</span>
    <span id="badge-linky" class="badge ko">Linky --</span>
    <span id="badge-tempo" class="badge">Tempo --</span>
    <span id="badge-ovld"  class="badge">Délestage --</span>
    <span id="fw-ver"      style="margin-left:auto;color:var(--dim)"></span>
  </div>

  <!-- LINKY BAR -->
  <div class="status-bar" id="linky-bar" style="background:var(--card);padding:6px 8px;border-radius:var(--radius);margin-bottom:8px;display:none">
    <span style="color:var(--dim);font-size:0.8em">Linky :</span>
    <span style="font-weight:bold" id="linky-iinst">-- A</span>
    <span style="color:var(--dim);font-size:0.8em">/ <span id="linky-isousc">--</span> A souscrits</span>
    <span style="color:var(--dim);font-size:0.8em;margin-left:8px">Demain : <span id="linky-demain-val">--</span></span>
  </div>

  <!-- ZONES -->
  <div class="card" style="margin-bottom:12px">
    <h2>Zones de chauffage</h2>
    <div class="grid4" id="zone-grid">
      <!-- Généré par JS -->
    </div>
    <div style="margin-top:10px">
      <h2>Commande zone sélectionnée : <span id="selected-zone-label">Z1</span></h2>
      <div class="cmd-btns">
        <button class="btn btn-CONF" onclick="sendCmd('CONF')">CONF</button>
        <button class="btn btn-ECO"  onclick="sendCmd('ECO')">ECO</button>
        <button class="btn btn-HG"   onclick="sendCmd('HG')">HG</button>
        <button class="btn btn-CM2"  onclick="sendCmd('CM2')">CM2</button>
        <button class="btn btn-STOP" onclick="sendCmd('STOP')">STOP</button>
      </div>
    </div>
  </div>

  <!-- CONFIG GLOBALE -->
  <div class="grid2" style="margin-bottom:12px">

    <!-- Tempo -->
    <div class="card">
      <h2>Option TEMPO EDF</h2>
      <div class="toggle-row">
        <span>Activer Tempo</span>
        <label class="toggle">
          <input type="checkbox" id="tempo-enabled" onchange="setTempoEnabled(this.checked)">
          <span class="slider"></span>
        </label>
      </div>
      <div class="toggle-row">
        <span>Blanc HP → HG</span>
        <label class="toggle">
          <input type="checkbox" id="tempo-white-hp" onchange="setTempoRule('whiteHP',this.checked)">
          <span class="slider"></span>
        </label>
      </div>
      <div class="toggle-row">
        <span>Rouge HP → HG</span>
        <label class="toggle">
          <input type="checkbox" id="tempo-red-hp" onchange="setTempoRule('redHP',this.checked)">
          <span class="slider"></span>
        </label>
      </div>
      <div class="toggle-row">
        <span>Rouge HC → HG</span>
        <label class="toggle">
          <input type="checkbox" id="tempo-red-hc" onchange="setTempoRule('redHC',this.checked)">
          <span class="slider"></span>
        </label>
      </div>
      <div id="tempo-info" style="margin-top:8px;font-size:0.8em;color:var(--dim)">--</div>
      <div id="tempo-counters" style="margin-top:6px;font-size:0.85em">
        <span style="color:var(--red)">🔴 <span id="cnt-red">--</span>/22</span>
        &nbsp;&nbsp;
        <span style="color:var(--white)">⚪ <span id="cnt-white">--</span>/43</span>
      </div>
    </div>

    <!-- Délestage -->
    <div class="card">
      <h2>Délestage surcharge</h2>
      <div class="toggle-row">
        <span>Activer délestage</span>
        <label class="toggle">
          <input type="checkbox" id="ovld-enabled" onchange="setOvldEnabled(this.checked)">
          <span class="slider"></span>
        </label>
      </div>
      <div class="config-row">
        <span class="config-label">Seuil détection (s)</span>
        <input class="config-input" type="number" id="ovld-threshold" min="1" max="60" value="10">
      </div>
      <div class="config-row">
        <span class="config-label">Délai restauration (s)</span>
        <input class="config-input" type="number" id="ovld-restore" min="10" max="300" value="30">
      </div>
      <div class="config-row">
        <span class="config-label">Commande fallback</span>
        <select id="ovld-fallback" class="profile-select" style="width:80px">
          <option value="ECO">ECO</option>
          <option value="HG">HG</option>
          <option value="STOP">STOP</option>
        </select>
      </div>
      <button class="btn btn-sec" style="margin-top:8px;width:100%"
              onclick="saveOvldConfig()">Sauvegarder</button>
      <div style="display:flex;gap:6px;margin-top:4px;align-items:center">
        <button class="btn btn-STOP" style="flex:1;background:#7f0000"
                onclick="forceRestore()">Forcer restauration</button>
        <span id="ovld-iinst" style="font-size:0.85em;font-weight:bold;
              color:var(--green);white-space:nowrap">-- A</span>
      </div>
    </div>
  </div>

  <!-- CALENDRIER -->
  <div id="calendar-section" class="card">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
      <h2>Calendrier de chauffage</h2>
      <label class="toggle" title="Activer calendrier">
        <input type="checkbox" id="sched-enabled" onchange="setSchedEnabled(this.checked)">
        <span class="slider"></span>
      </label>
    </div>

    <!-- Éditeur de profil -->
    <div class="profile-header">
      <span style="color:var(--dim);font-size:.85em">Profil :</span>
      <select class="profile-select" id="profile-select" onchange="loadProfile()">
        <!-- Généré par JS -->
      </select>
      <input class="profile-name-input" type="text" id="profile-name" placeholder="Nom profil">
      <button class="btn btn-sec" onclick="saveProfile()">Sauver profil</button>
      <button class="btn btn-sec" onclick="fillProfile('ECO')">Tout ECO</button>
      <button class="btn btn-sec" onclick="fillProfile('CONF')">Tout CONF</button>
    </div>

    <!-- Légende -->
    <div class="legend">
      <div class="legend-item">
        <div class="legend-dot" style="background:var(--green)"></div>CONF
      </div>
      <div class="legend-item">
        <div class="legend-dot" style="background:#1565c0"></div>ECO
      </div>
      <div class="legend-item">
        <div class="legend-dot" style="background:var(--orange)"></div>HG
      </div>
      <div class="legend-item">
        <div class="legend-dot" style="background:var(--red)"></div>STOP
      </div>
      <div class="legend-item">
        <div class="legend-dot" style="background:#7b1fa2"></div>CM2
      </div>
      <div style="color:var(--dim);font-size:.8em;margin-left:auto">
        Clic = cycle · Shift+clic = sélection · Glisser = remplir
      </div>
    </div>

    <!-- Grille slots 48 × 1 ligne -->
    <div class="grid-wrapper">
      <div class="slot-grid" id="slot-grid">
        <!-- Généré par JS -->
      </div>
    </div>

    <!-- Assignation zone/profil -->
    <div style="margin-top:14px">
      <h2 style="margin-bottom:8px">Assignation zones → profils</h2>
      <div class="assign-grid" id="assign-grid">
        <!-- Généré par JS -->
      </div>
      <button class="btn btn-sec" style="margin-top:8px"
              onclick="saveSchedule()">Sauvegarder assignations</button>
    </div>
  </div>

  <!-- ZONES ACTIVES -->
  <div class="card" style="margin-top:12px">
    <h2>Configuration zones</h2>
    <div class="config-row">
      <span class="config-label">Nombre de zones actives</span>
      <select id="zone-count" class="profile-select"
              onchange="setZoneCount(this.value)">
        
        <option value="2" selected>2 zones</option>
      </select>
    </div>
    <button class="btn btn-STOP" style="margin-top:12px;background:#7f0000"
            onclick="eraseEeprom()">⚠ Effacer EEPROM + Redémarrer</button>
  </div>

</div><!-- /container -->

<script>
// =============================================================================
// ÉTAT GLOBAL
// =============================================================================
const CMDS      = ['CONF','ECO','HG','STOP','CM2'];
const DAYS      = ['Lun','Mar','Mer','Jeu','Ven','Sam','Dim'];
const NUM_ZONES = 2;
const NUM_SLOTS = 48;
const NUM_PROFILES = 8;

let state          = {};
let profiles       = Array(NUM_PROFILES).fill(null).map((_,i) => ({
  index: i, name: `Profil ${i}`, active: false,
  slots: Array(NUM_SLOTS).fill('ECO')
}));
let schedule       = { enabled: false, zones: [] };
let selectedZone   = 1;
let selectedCmd    = 'CONF'; // commande à peindre sur la grille
let isDragging     = false;
let dragStartSlot  = -1;
let isShiftSelecting = false;
let shiftStart     = -1;

// Polling toutes les 2s — pause pendant saisie dans les champs
let _pollInterval = null;
let _pollPaused   = false;

function refreshState() {
  fetch('/api/state')
    .then(r => r.json())
    .then(s => applyState(s))
    .catch(() => {});
}

function wsConnect() {
  setTimeout(() => {
    refreshState();
    _pollInterval = setInterval(() => {
      if (!_pollPaused) refreshState();
    }, 2000);
  }, 1000);
}

// =============================================================================
// ÉTAT
// =============================================================================
function applyState(s) {
  if (!s) return;
  state = s;
  updateStatusBar(s);
  if (s.zones) updateZones(s.zones);

  // Cache le calendrier si EEPROM 24C02 (pas assez de place)
  if (s.hasCalendar !== undefined) {
    const cal = document.getElementById('calendar-section');
    if (cal) cal.style.display = s.hasCalendar ? '' : 'none';
  }
}

function updateStatusBar(s) {
  setbadge('badge-wifi',  s.wifiConnected,  s.wifiConnected ? 'WiFi ✓' : 'WiFi ✗');
  setbadge('badge-mqtt',  s.mqttConnected,  s.mqttConnected ? 'MQTT ✓' : 'MQTT ✗');
  setbadge('badge-linky', s.linkyConnected, s.linkyConnected ? 'Linky ✓': 'Linky ✗');

  const tColor = s.tempoColor || '--';
  const tPer   = s.tempoPeriod || '';
  const tForce = s.tempoForceHG ? ' ⚠HG' : '';
  el('badge-tempo').textContent = `${tColor} ${tPer}${tForce}`;
  el('badge-tempo').className   = `badge ${s.tempoForceHG ? 'warn' : ''}`;

  el('badge-ovld').textContent  = s.overload ? '⚠ DÉLESTÉ' : 'Délestage OK';
  el('badge-ovld').className    = `badge ${s.overload ? 'warn' : 'ok'}`;

  if (s.fw) el('fw-ver').textContent = s.fw;

  // Tempo toggles
  if (s.tempoEnabled  !== undefined) el('tempo-enabled').checked = s.tempoEnabled;
  if (s.whiteHP       !== undefined) el('tempo-white-hp').checked = s.whiteHP;
  if (s.redHP         !== undefined) el('tempo-red-hp').checked   = s.redHP;
  if (s.redHC         !== undefined) el('tempo-red-hc').checked   = s.redHC;
  if (s.tempoColor)
    el('tempo-info').textContent =
      `${s.tempoColor} ${s.tempoPeriod} · Demain: ${s.tomorrow||'--'}`;

  // Compteurs jours Tempo
  if (s.tempoRedCount   !== undefined) el('cnt-red').textContent   = s.tempoRedCount;
  if (s.tempoWhiteCount !== undefined) el('cnt-white').textContent = s.tempoWhiteCount;

  // Overload
  if (s.ovldEnabled   !== undefined) el('ovld-enabled').checked = s.ovldEnabled;
  if (s.ovldThreshold !== undefined) el('ovld-threshold').value = Math.round(s.ovldThreshold/1000);
  if (s.ovldRestore   !== undefined) el('ovld-restore').value   = Math.round(s.ovldRestore/1000);
  if (s.ovldFallback  !== undefined) el('ovld-fallback').value  = s.ovldFallback;

  // Schedule
  if (s.schedEnabled  !== undefined) el('sched-enabled').checked = s.schedEnabled;

  // Linky bar
  if (s.Iinst !== undefined) {
    const bar = el('linky-bar');
    if (bar) bar.style.display = s.linkyConnected ? '' : 'none';
    el('linky-iinst').textContent  = `${s.Iinst} A`;
    el('linky-isousc').textContent = s.Isousc || '--';
    el('linky-demain-val').textContent = s.DEMAIN || '--';
    // Couleur Iinst selon charge
    const ratio = s.Isousc > 0 ? s.Iinst / s.Isousc : 0;
    const color = ratio >= 1 ? 'var(--red)' :
                  ratio >= 0.8 ? 'var(--orange)' : 'var(--green)';
    el('linky-iinst').style.color = color;
    // Iinst dupliqué à côté du bouton délestage
    const ovldEl = el('ovld-iinst');
    if (ovldEl) {
      ovldEl.textContent = s.linkyConnected ? `${s.Iinst} A` : '-- A';
      ovldEl.style.color = color;
    }
  }
}

// =============================================================================
// ZONES
// =============================================================================
function buildZoneGrid() {
  const g = el('zone-grid');
  g.innerHTML = '';
  for (let z = 1; z <= NUM_ZONES; z++) {
    const card = document.createElement('div');
    card.className = `zone-card card ${z === selectedZone ? 'selected' : ''}`;
    card.id = `zone-card-${z}`;
    card.onclick = () => selectZone(z);
    card.innerHTML = `
      <div class="zone-name">ZONE ${z}</div>
      <div class="zone-cmd cmd-ECO" id="zone-cmd-${z}">ECO</div>
      <div class="zone-state" id="zone-state-${z}"></div>
      <div style="font-size:0.75em;color:var(--dim);margin-top:4px" id="zone-temp-${z}">--°C</div>`;
    g.appendChild(card);
  }
}

function updateZones(zones) {
  zones.forEach(z => {
    const cmdEl   = el(`zone-cmd-${z.id}`);
    const stateEl = el(`zone-state-${z.id}`);
    const tempEl  = el(`zone-temp-${z.id}`);
    const card    = el(`zone-card-${z.id}`);
    if (!cmdEl) return;
    cmdEl.textContent = z.cmd || '--';
    cmdEl.className   = `zone-cmd cmd-${z.cmd}`;
    stateEl.textContent = z.state !== 'NORMAL' ? z.state : '';
    stateEl.className   = `zone-state state-${z.state}`;
    if (card) card.style.opacity = z.active === false ? '0.4' : '1';
    if (tempEl) tempEl.textContent = z.temp ? `${z.temp}°C` : '--°C';
  });
}

function selectZone(z) {
  selectedZone = z;
  el('selected-zone-label').textContent = `Z${z}`;
  document.querySelectorAll('.zone-card').forEach((c,i) => {
    c.classList.toggle('selected', i+1 === z);
  });
}

function sendCmd(cmd) {
  fetch('/api/zone', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({ zone: selectedZone, cmd: cmd })
  }).catch(e => console.warn('sendCmd error', e));
}

// =============================================================================
// CALENDRIER — GRILLE SLOTS
// =============================================================================
function buildSlotGrid() {
  const grid = el('slot-grid');
  grid.innerHTML = '';

  // Entêtes heures (toutes les 2 colonnes = 1h)
  grid.appendChild(makeEl('div', 'slot-label', ''));
  for (let s = 0; s < NUM_SLOTS; s++) {
    const hh = Math.floor(s/2);
    const mm = (s%2)*30;
    const lbl = makeEl('div', 'slot-header',
                       s%2===0 ? `${String(hh).padStart(2,'0')}` : '');
    grid.appendChild(lbl);
  }

  // Ligne unique de slots (on n'a qu'un profil affiché)
  grid.appendChild(makeEl('div','slot-label','↓'));
  for (let s = 0; s < NUM_SLOTS; s++) {
    const slot = document.createElement('div');
    slot.className = 'slot';
    slot.dataset.slot = s;
    slot.dataset.cmd  = 'ECO';
    slot.title = slotLabel(s);
    // Events souris
    slot.addEventListener('mousedown', e => onSlotDown(e, s));
    slot.addEventListener('mouseenter', e => onSlotEnter(e, s));
    slot.addEventListener('mouseup', e => onSlotUp(e, s));
    grid.appendChild(slot);
  }
  document.addEventListener('mouseup', () => { isDragging = false; });
}

function slotLabel(s) {
  const hh = Math.floor(s/2), mm = (s%2)*30;
  const hh2 = s<47 ? Math.floor((s+1)/2) : 0;
  const mm2 = ((s+1)%2)*30;
  return `${pad(hh)}:${pad(mm)}–${pad(hh2)}:${pad(mm2)}`;
}

function onSlotDown(e, s) {
  e.preventDefault();
  if (e.shiftKey) {
    isShiftSelecting = true;
    shiftStart = s;
  } else {
    isDragging = true;
    dragStartSlot = s;
    // Clic simple = cycle commande
    const cur = getSlotCmd(s);
    const next = CMDS[(CMDS.indexOf(cur)+1) % CMDS.length];
    paintSlot(s, next);
    selectedCmd = next;
  }
}

function onSlotEnter(e, s) {
  if (isDragging)        paintSlot(s, selectedCmd);
  if (isShiftSelecting)  paintRange(shiftStart, s, selectedCmd);
}

function onSlotUp(e, s) {
  if (isShiftSelecting) {
    paintRange(shiftStart, s, selectedCmd);
    isShiftSelecting = false;
    shiftStart = -1;
  }
  isDragging = false;
}

function getSlotCmd(s) {
  const el2 = slotEl(s);
  return el2 ? el2.dataset.cmd : 'ECO';
}

function paintSlot(s, cmd) {
  const el2 = slotEl(s);
  if (!el2) return;
  el2.dataset.cmd = cmd;
}

function paintRange(from, to, cmd) {
  const a = Math.min(from,to), b = Math.max(from,to);
  for (let s = a; s <= b; s++) paintSlot(s, cmd);
}

function fillProfile(cmd) {
  for (let s = 0; s < NUM_SLOTS; s++) paintSlot(s, cmd);
}

function slotEl(s) {
  return document.querySelector(`.slot[data-slot="${s}"]`);
}

// =============================================================================
// PROFILS
// =============================================================================
function buildProfileSelect() {
  const sel = el('profile-select');
  sel.innerHTML = '';
  for (let i = 0; i < NUM_PROFILES; i++) {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = profiles[i].name || `Profil ${i}`;
    sel.appendChild(opt);
  }
}

function loadProfile() {
  const pi = parseInt(el('profile-select').value);
  const p  = profiles[pi];
  el('profile-name').value = p.name;
  for (let s = 0; s < NUM_SLOTS; s++) {
    paintSlot(s, p.slots[s] || 'ECO');
  }
}

function saveProfile() {
  const pi   = parseInt(el('profile-select').value);
  const name = el('profile-name').value.trim() || `Profil ${pi}`;
  const slots = [];
  for (let s = 0; s < NUM_SLOTS; s++) slots.push(getSlotCmd(s));
  profiles[pi] = { index: pi, name, active: true, slots };
  el('profile-select').options[pi].textContent = name;
  buildAssignGrid();

  fetch('/api/profile', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({ index: pi, name, slots })
  }).then(r => r.json()).then(r => console.log('Profile saved:', r));
}

// =============================================================================
// ASSIGNATION ZONE/PROFIL
// =============================================================================
function buildAssignGrid() {
  const g = el('assign-grid');
  g.innerHTML = '';

  // Header
  g.appendChild(makeEl('div','assign-day',''));
  DAYS.forEach(d => g.appendChild(makeEl('div','assign-day',d)));

  // Lignes zones
  for (let z = 1; z <= NUM_ZONES; z++) {
    g.appendChild(makeEl('div','assign-zone',`Z${z}`));
    for (let d = 0; d < 7; d++) {
      const sel = document.createElement('select');
      sel.className = 'assign-select';
      sel.id = `assign-z${z}-d${d}`;
      for (let pi = 0; pi < NUM_PROFILES; pi++) {
        const opt = document.createElement('option');
        opt.value = pi;
        opt.textContent = profiles[pi].name || `P${pi}`;
        sel.appendChild(opt);
      }
      // Valeur courante
      if (schedule.zones && schedule.zones[z-1]) {
        const days = schedule.zones[z-1].days || [];
        sel.value = days[d] ?? 0;
      }
      g.appendChild(sel);
    }
  }
}

function saveSchedule() {
  const zones = [];
  for (let z = 1; z <= NUM_ZONES; z++) {
    const days = [];
    for (let d = 0; d < 7; d++) {
      const sel = el(`assign-z${z}-d${d}`);
      days.push(sel ? parseInt(sel.value) : 0);
    }
    const enabled = el('sched-enabled').checked;
    zones.push({ id: z, enabled, days });
  }
  fetch('/api/schedule', {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify({ enabled: el('sched-enabled').checked, zones })
  }).then(r => r.json()).then(r => console.log('Schedule saved:', r));
}

// =============================================================================
// API CALLS
// =============================================================================
function setTempoEnabled(v) {
  postConfig('/api/config/tempo', { enabled: v });
}
function setTempoRule(key, v) {
  const body = {};
  body[key] = v;
  postConfig('/api/config/tempo', body);
}
function setOvldEnabled(v) {
  postConfig('/api/config/ovld', { enabled: v });
}
function saveOvldConfig() {
  if (!confirm('Sauvegarder la configuration délestage ?')) return;
  postConfig('/api/config/ovld', {
    enabled:    el('ovld-enabled').checked,
    thresholdMs: parseInt(el('ovld-threshold').value) * 1000,
    restoreMs:   parseInt(el('ovld-restore').value)   * 1000,
    fallback:    el('ovld-fallback').value
  });
  _pollPaused = false;
}
function forceRestore() {
  if (!confirm('Forcer la restauration des zones ?\n(Annule le délestage en cours et restaure les commandes normales)')) return;
  postConfig('/api/config/ovld', { forceRestore: true });
}
function setSchedEnabled(v) {
  postConfig('/api/schedule', { enabled: v, zones: [] });
}
function setZoneCount(v) {
  postConfig('/api/config/zones', { count: parseInt(v) });
}
function eraseEeprom() {
  if (!confirm('⚠ Effacer toute la mémoire EEPROM ?\nLes zones reprendront ECO au redémarrage.\n\n⚡ L\'ESP32 va redémarrer automatiquement !')) return;
  postConfig('/api/eeprom/erase', { confirm: true });
}

function postConfig(url, body) {
  fetch(url, {
    method: 'POST',
    headers: {'Content-Type':'application/json'},
    body: JSON.stringify(body)
  }).then(r=>r.json()).then(r=>console.log(url, r))
    .catch(e=>console.error(url, e));
}

// =============================================================================
// INIT + FETCH INITIAL
// =============================================================================
function init() {
  buildZoneGrid();
  buildSlotGrid();
  buildProfileSelect();
  buildAssignGrid();
  loadProfile();

  // Fetch état + profils (silencieux si ESP32 pas encore prêt)
  fetch('/api/state').then(r=>r.json()).then(s => applyState(s)).catch(()=>{});
  fetch('/api/profiles').then(r=>r.json()).then(data => {
    if (data.profiles) {
      profiles = data.profiles;
      buildProfileSelect();
      loadProfile();
    }
  }).catch(()=>{});
  fetch('/api/schedule').then(r=>r.json()).then(data => {
    schedule = data;
    buildAssignGrid();
    if (data.enabled !== undefined) el('sched-enabled').checked = data.enabled;
  }).catch(()=>{});

  wsConnect();

  // Pause polling pendant saisie dans les champs délestage
  // Reprend UNIQUEMENT après clic sur Sauvegarder
  ['ovld-threshold', 'ovld-restore', 'ovld-fallback', 'ovld-enabled'].forEach(id => {
    const inp = el(id);
    if (!inp) return;
    inp.addEventListener('focus',    () => { _pollPaused = true; });
    inp.addEventListener('mousedown',() => { _pollPaused = true; });
  });
}

// =============================================================================
// HELPERS
// =============================================================================
const el       = id => document.getElementById(id);
const pad      = n  => String(n).padStart(2,'0');
function makeEl(tag, cls, text) {
  const e = document.createElement(tag);
  e.className   = cls;
  e.textContent = text;
  return e;
}
function setbadge(id, ok, text) {
  const b = el(id);
  b.textContent = text;
  b.className   = `badge ${ok ? 'ok' : 'ko'}`;
}

window.addEventListener('load', init);
</script>
</body>
</html>
)rawhtml";

// =============================================================================
// INITIALISATION
// =============================================================================

void WebUI::begin(ZoneManager*     zoneMgr,
                  CommandHandler*  cmdHandler,
                  TempoManager*    tempoMgr,
                  ScheduleManager* scheduleMgr,
                  OverloadManager* overloadMgr,
                  StorageManager*  storageMgr,
                  DisplayManager*  displayMgr,
                  Publisher*       publisher) {
  _zoneMgr     = zoneMgr;
  _cmdHandler  = cmdHandler;
  _tempoMgr    = tempoMgr;
  _scheduleMgr = scheduleMgr;
  _overloadMgr = overloadMgr;
  _storageMgr  = storageMgr;
  _displayMgr  = displayMgr;
  _publisher   = publisher;

  _registerRoutes();
  _server.begin();

  LOG(LOG_WEB, "HTTP server started on port 80");
}

// =============================================================================
// LOOP
// =============================================================================

void WebUI::update() {
  _server.handleClient();
}

// =============================================================================
// ROUTES
// =============================================================================

void WebUI::_registerRoutes() {
  _server.on("/",                  HTTP_GET,  [this]{ _handleRoot();            });
  _server.on("/api/state",         HTTP_GET,  [this]{ _handleApiState();        });
  _server.on("/api/profiles",      HTTP_GET,  [this]{ _handleApiProfiles();     });
  _server.on("/api/schedule",      HTTP_GET,  [this]{ _handleApiSchedule();     });
  _server.on("/api/profile",       HTTP_POST, [this]{ _handleApiPostProfile();  });
  _server.on("/api/schedule",      HTTP_POST, [this]{ _handleApiPostSchedule(); });
  _server.on("/api/zone",          HTTP_POST, [this]{ _handleApiPostZone();     });
  _server.on("/api/config/tempo",  HTTP_POST, [this]{ _handleApiConfigTempo();  });
  _server.on("/api/config/ovld",   HTTP_POST, [this]{ _handleApiConfigOverload();});
  _server.on("/api/config/zones",  HTTP_POST, [this]{ _handleApiConfigZones();  });
  _server.on("/api/eeprom/erase",  HTTP_POST, [this]{ _handleApiEepromErase();  });
  _server.on("/api/debug",         HTTP_GET,  [this]{ _handleApiDebug();        });
  _server.onNotFound(              [this]{ _handleNotFound();                   });
}

// =============================================================================
// HANDLERS
// =============================================================================

void WebUI::_handleRoot() {
  _server.send_P(200, "text/html; charset=utf-8", HTML_PAGE);
}

void WebUI::_handleApiState() {
  JsonDocument doc;

  // Header
  char ver[20];
  getVersion(ver, sizeof(ver));
  doc["fw"] = ver;

  // État connexions
  doc["wifiConnected"]  = WiFi.isConnected();
  doc["mqttConnected"]  = _publisher ? _publisher->isMqttConnected() : false;
  // Linky connecté = au moins une trame reçue dans les 30 dernières secondes
  doc["linkyConnected"] = linkyReader.isConnected() ||
                          linkyReader.getSnapshot().frameValid;

  // Zones
  JsonArray zones = doc["zones"].to<JsonArray>();
  if (_zoneMgr) {
    for (uint8_t z = 1; z <= NUM_ZONES; z++) {
      JsonObject zObj = zones.add<JsonObject>();
      const ZoneData& zd = _zoneMgr->getZone(z);
      zObj["id"]     = z;
      zObj["cmd"]    = heatingCmdToStr(zd.currentCmd);
      zObj["state"]  = zoneStateToStr(zd.state);
      zObj["active"] = (z <= _activeZones);
    }
  }

  // Tempo
  if (_tempoMgr) {
    const TempoState& ts = _tempoMgr->getState();
    const TempoConfig& tc = _tempoMgr->getConfig();
    doc["tempoEnabled"]  = tc.enabled;
    doc["tempoColor"]    = tempoColorToStr(ts.color);
    doc["tempoPeriod"]   = (ts.period == TempoPeriod::HP) ? "HP" : "HC";
    doc["tempoForceHG"]  = ts.forceHG;
    doc["tomorrow"]      = tempoColorToStr(_tempoMgr->getTomorrowColor());
    doc["whiteHP"]       = tc.forceHG_WhiteHP;
    doc["redHP"]         = tc.forceHG_RedHP;
    doc["redHC"]         = tc.forceHG_RedHC;
    doc["tempoRedCount"]   = _tempoMgr->getCountRed();
    doc["tempoWhiteCount"] = _tempoMgr->getCountWhite();
  }

  // Overload
  if (_overloadMgr) {
    const OverloadConfig& oc = _overloadMgr->getConfig();
    doc["ovldEnabled"]   = oc.enabled;
    doc["ovldThreshold"] = oc.thresholdMs;
    doc["ovldRestore"]   = oc.restoreDelayMs;
    doc["ovldFallback"]  = heatingCmdToStr(oc.fallbackCmd);
    doc["overload"]      = _overloadMgr->isOverloaded();
  }

  // Capacités hardware — calendrier disponible uniquement avec 24C32
  doc["hasCalendar"] = (bool)EEPROM_ADDR_16;

  // Schedule
  if (_scheduleMgr) {
    doc["schedEnabled"] = _scheduleMgr->isEnabled();
  }

  // Linky — Iinst, Isousc, PTEC, DEMAIN
  doc["Iinst"]  = linkyReader.getIinst();
  doc["Isousc"] = linkyReader.getIsousc();
  doc["PTEC"]   = linkyReader.getPtec();
  doc["DEMAIN"] = linkyReader.getDemain();

  // Températures Ecowitt par zone (dans le tableau zones)
  if (_publisher) {
    JsonArray zonesArr = doc["zones"];
    for (uint8_t i = 0; i < NUM_ZONES && i < zonesArr.size(); i++) {
      zonesArr[i]["temp"] = _publisher->getTemp(i);
    }
  }

  char buf[1024];
  serializeJson(doc, buf, sizeof(buf));
  _sendJson(buf);
}

void WebUI::_handleApiProfiles() {
  if (!_scheduleMgr) { _sendError("No ScheduleManager"); return; }

  JsonDocument doc;
  JsonArray arr = doc["profiles"].to<JsonArray>();

  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    const ScheduleProfile& p = _scheduleMgr->getProfile(i);
    JsonObject pObj = arr.add<JsonObject>();
    pObj["index"]  = i;
    pObj["name"]   = p.name;
    pObj["active"] = p.active;
    JsonArray slots = pObj["slots"].to<JsonArray>();
    for (uint8_t s = 0; s < SLOTS_PER_DAY; s++) {
      slots.add(heatingCmdToStr(p.slots[s]));
    }
  }

  char buf[4096];
  serializeJson(doc, buf, sizeof(buf));
  _sendJson(buf);
}

void WebUI::_handleApiSchedule() {
  if (!_scheduleMgr) { _sendError("No ScheduleManager"); return; }
  char buf[1024];
  _scheduleMgr->scheduleToJson(buf, sizeof(buf));
  _sendJson(buf);
}

void WebUI::_handleApiPostProfile() {
  if (!_scheduleMgr) { _sendError("No ScheduleManager"); return; }

  JsonDocument doc;
  if (!_parseBody(doc)) return;

  uint8_t pi = doc["index"] | 255;
  if (pi >= MAX_PROFILES) { _sendError("Invalid profile index"); return; }

  char jsonBuf[2048];
  serializeJson(doc, jsonBuf, sizeof(jsonBuf));

  if (_scheduleMgr->profileFromJson(pi, jsonBuf)) {
    _sendOk("profile saved");
    if (_publisher) {
      _publisher->publishLog("INFO","WEB","Profile[%d] updated via HTTP", pi);
    }
  } else {
    _sendError("Profile parse failed");
  }
}

void WebUI::_handleApiPostSchedule() {
  if (!_scheduleMgr) { _sendError("No ScheduleManager"); return; }

  JsonDocument doc;
  if (!_parseBody(doc)) return;

  char jsonBuf[1024];
  serializeJson(doc, jsonBuf, sizeof(jsonBuf));

  if (_scheduleMgr->scheduleFromJson(jsonBuf)) {
    _sendOk("schedule saved");
  } else {
    _sendError("Schedule parse failed");
  }
}

void WebUI::_handleApiPostZone() {
  if (!_cmdHandler) { _sendError("No CommandHandler"); return; }

  JsonDocument doc;
  if (!_parseBody(doc)) return;

  int         zone = doc["zone"] | 0;
  const char* cmd  = doc["cmd"]  | "";

  if (zone < 1 || zone > NUM_ZONES || cmd[0] == '\0') {
    _sendError("Invalid zone or cmd");
    return;
  }

  bool ok = _cmdHandler->handle(zone, cmd,
                                 CommandOrigin::USER,
                                 CommandSource::SRC_WS);
  ok ? _sendOk() : _sendError("Command rejected");
}

void WebUI::_handleApiConfigTempo() {
  if (!_tempoMgr) { _sendError("No TempoManager"); return; }

  JsonDocument doc;
  if (!_parseBody(doc)) return;

  if (doc["enabled"].is<bool>())  _tempoMgr->setEnabled(doc["enabled"]);
  if (doc["whiteHP"].is<bool>())  _tempoMgr->setForceHG_WhiteHP(doc["whiteHP"]);
  if (doc["redHP"].is<bool>())    _tempoMgr->setForceHG_RedHP(doc["redHP"]);
  if (doc["redHC"].is<bool>())    _tempoMgr->setForceHG_RedHC(doc["redHC"]);

  // Sauvegarde NVS
  if (_storageMgr) {
    const TempoConfig& tc = _tempoMgr->getConfig();
    _storageMgr->saveTempoConfig(tc.enabled,
                                  tc.forceHG_WhiteHP,
                                  tc.forceHG_RedHP,
                                  tc.forceHG_RedHC);
  }
  _sendOk("tempo config saved");
}

void WebUI::_handleApiConfigOverload() {
  if (!_overloadMgr) { _sendError("No OverloadManager"); return; }

  JsonDocument doc;
  if (!_parseBody(doc)) return;

  if (doc["forceRestore"] | false) {
    _overloadMgr->forceRestore();
    _sendOk("restore forced");
    return;
  }

  if (doc["enabled"].is<bool>())
    _overloadMgr->setEnabled(doc["enabled"]);
  if (doc["thresholdMs"].is<long>())
    _overloadMgr->setThresholdMs(doc["thresholdMs"]);
  if (doc["restoreMs"].is<long>())
    _overloadMgr->setRestoreDelayMs(doc["restoreMs"]);
  if (doc["fallback"].is<const char*>()) {
    HeatingCmd fb = CommandHandler::parseCmd(doc["fallback"] | "ECO");
    if (fb != HeatingCmd::UNKNOWN) _overloadMgr->setFallbackCmd(fb);
  }

  if (_storageMgr) {
    const OverloadConfig& oc = _overloadMgr->getConfig();
    _storageMgr->saveOverloadConfig(oc.enabled,
                                     oc.thresholdMs,
                                     oc.restoreDelayMs,
                                     oc.fallbackCmd);
  }
  _sendOk("overload config saved");
}

void WebUI::_handleApiConfigZones() {
  JsonDocument doc;
  if (!_parseBody(doc)) return;

  uint8_t count = doc["count"] | 4;
  if (count != 2 && count != 4) count = 4;
  _activeZones = count;

  if (_displayMgr) _displayMgr->setActiveZoneCount(count);

  _sendOk("zone count updated");
}

void WebUI::_handleApiEepromErase() {
  JsonDocument doc;
  if (!_parseBody(doc)) return;

  if (!(doc["confirm"] | false)) {
    _sendError("Confirmation required", 400);
    return;
  }

  if (!_storageMgr) { _sendError("No StorageManager"); return; }

  _sendOk("erasing EEPROM — restarting");

  if (_publisher) {
    _publisher->publishLog("WARN", "WEB", "EEPROM erase requested via HTTP");
  }

  delay(300);
  _storageMgr->eraseAll();
  delay(500);
  ESP.restart();
}

void WebUI::_handleApiDebug() {
  String out = "=== DEBUG DUMP ===\n";

  if (_zoneMgr)     { /* zoneMgr->dumpAll() → Serial */ _zoneMgr->dumpAll(); }
  if (_tempoMgr)    { _tempoMgr->dump(); }
  if (_overloadMgr) { _overloadMgr->dump(); }
  if (_scheduleMgr) { _scheduleMgr->dumpAll(); }
  if (_storageMgr)  { _storageMgr->dump(); }

  out += "Dump sent to Serial.";
  _server.send(200, "text/plain; charset=utf-8", out);
}

void WebUI::_handleNotFound() {
  _server.send(404, "text/plain", "Not found");
}

// =============================================================================
// HELPERS
// =============================================================================

void WebUI::_sendJson(const char* json, int code) {
  _server.sendHeader("Access-Control-Allow-Origin", "*");
  _server.send(code, "application/json; charset=utf-8", json);
}

void WebUI::_sendOk(const char* msg) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"msg\":\"%s\"}", msg);
  _sendJson(buf, 200);
}

void WebUI::_sendError(const char* msg, int code) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"msg\":\"%s\"}", msg);
  _sendJson(buf, code);
  LOG(LOG_WEB, "HTTP error: %s", msg);
}

bool WebUI::_parseBody(JsonDocument& doc) {
  if (!_server.hasArg("plain")) {
    _sendError("No body");
    return false;
  }
  DeserializationError err = deserializeJson(doc, _server.arg("plain"));
  if (err) {
    _sendError("JSON parse error");
    return false;
  }
  return true;
}
