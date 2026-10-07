#pragma once
#include <Arduino.h>
// Generated from data/ by scripts/embed_web.py.

const char WEB_HTML[] PROGMEM = R"OVEN_ASSET_2026(<!DOCTYPE html>
<html lang="it">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Oven Controller · Test hardware</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header class="topbar">
    <div>
      <div class="eyebrow">ESP32-S3 · TEST HARDWARE</div>
      <h1>Oven Controller</h1>
    </div>
    <div class="status-wrap"><span id="statusDot" class="status-dot idle"></span><span id="statusText">Connessione…</span></div>
  </header>

  <nav class="view-tabs" aria-label="Sezioni">
    <button id="tabDashboard" class="active" type="button" aria-selected="true">Dashboard</button>
    <button id="tabLogs" type="button" aria-selected="false">Log</button>
  </nav>

  <main id="dashboardView" class="layout">
    <section class="panel dashboard-panel">
      <div class="panel-title-row">
        <div><h2>Dashboard</h2><p id="dashboardSubtitle">Lettura delle due PT100 reali</p></div>
        <div class="clock-box"><span>Tempo acceso</span><strong id="uptime">—</strong></div>
      </div>
      <div class="hero-grid">
        <article class="metric hero"><span>Temperatura media</span><strong><span id="avgTemp">—</span><small> °C</small></strong></article>
        <article class="metric hero target"><span>Target fisso</span><strong><span id="targetTemp">35.0</span><small> °C</small></strong></article>
        <article class="metric"><span>Uscita relè</span><strong id="relayState">SPENTA</strong><small id="relayHint">LED rosso spento</small><small id="pidInfo">PID 0%</small></article>
        <article class="metric"><span>ΔT sonde</span><strong><span id="deltaTemp">—</span><small> °C</small></strong><small>Limite 10 °C</small></article>
      </div>
      <div class="sensor-grid two">
        <article class="sensor-card"><span id="sensorLabel1">PT100 #1 · CS GPIO14</span><strong id="t1">—</strong><small id="fault1">—</small></article>
        <article class="sensor-card"><span id="sensorLabel2">PT100 #2 · CS GPIO10</span><strong id="t2">—</strong><small id="fault2">—</small></article>
      </div>
      <div class="chart-card">
        <div class="chart-head"><div><h3>Temperature nel tempo</h3><small>Traccia live circa ogni 0,5 s; storico sull'ESP ogni 10 s fino a 6 ore</small></div>
          <div class="legend"><span><i class="legend-line target-line"></i>Target</span><span><i class="legend-line actual-line"></i>PT100</span><span><i class="legend-line simulated-line"></i>Simulata</span></div>
          <button id="exportSamples" class="ghost">Esporta CSV</button></div>
        <canvas id="tempChart" width="1200" height="420" aria-label="Grafico temperature"></canvas>
        <div class="chart-navigation">
          <button id="chartBack" type="button">← Indietro</button>
          <input id="chartPosition" type="range" min="0" max="0" value="0" aria-label="Scorri lo storico del grafico">
          <button id="chartForward" type="button">Avanti →</button>
          <button id="chartNow" type="button">Adesso</button>
          <select id="chartSpan" aria-label="Durata visibile nel grafico">
            <option value="900000">15 min</option><option value="1800000" selected>30 min</option>
            <option value="3600000">1 ora</option><option value="all">Tutto</option>
          </select>
        </div>
        <p id="chartRangeLabel" class="chart-range-label">Caricamento storico…</p>
      </div>
    </section>

    <aside class="panel control-panel">
      <h2>Test sonde e relè</h2>
      <p>Target fisso 35 °C. Senza PT100 scegli le sonde simulate; con le PT100 puoi scaldarle con le mani. A 36 °C su una lettura il relè si spegne e si riabilita a 35,5 °C o meno.</p>
      <label class="field"><span>Origine delle temperature</span>
        <select id="sensorMode"><option value="real">PT100 reali</option><option value="simulated">Sonde simulate</option></select>
      </label>
      <div id="simBanner" class="sim-banner" hidden>SIMULAZIONE ATTIVA · I valori non vengono dalle PT100. Il GPIO4 può attivare il relè: usa soltanto un carico di prova a bassa tensione.</div>
      <div id="simControls" class="buttons sim-buttons" hidden>
        <button id="simDownBtn" type="button">−1 °C finto</button><button id="simUpBtn" type="button">+1 °C finto</button>
      </div>
      <div class="buttons test-buttons"><button id="startBtn" class="primary">AVVIA TEST</button><button id="stopBtn" class="danger">STOP</button></div>
      <button id="resetBtn" class="ghost full">Azzera allarme</button>
      <div id="alarmBox" class="alarm hidden"><strong>Attenzione</strong><span id="alarmText"></span></div>
      <div id="messageBox" class="notice hidden"></div>
      <hr>
      <h2>Accesso alla dashboard</h2>
      <div class="network-box">
        <div class="network-line"><span>Wi-Fi capannone</span><strong id="workshopState">Verifica connessione…</strong></div>
        <a id="workshopAddress" class="network-address" href="#" hidden></a>
        <a id="localAddress" class="network-address" href="#" hidden></a>
        <p id="workshopHint">Collega telefono o PC alla stessa rete del capannone per aprire l'indirizzo indicato.</p>
        <div class="network-line"><span>Rete diretta OvenController-Test</span><strong id="apState">Verifica…</strong></div>
        <span id="apAddress" class="network-address"></span>
      </div>
      <hr>
      <h2>Alimentazione e ripartenza</h2>
      <div class="recipe-summary power-summary">
        <div><span>Numero avvii</span><strong id="bootCount">—</strong></div>
        <div><span>Ultimo reset</span><strong id="resetReason">—</strong></div>
        <div><span>Test interrotto</span><strong id="interrupted">—</strong></div>
        <div><span>Ora rete</span><strong id="timeStatus">—</strong></div>
        <div><span>Temperatura interna ESP</span><strong id="chipTemperature">—</strong></div>
      </div>
      <p id="outageInfo" class="hint">Lettura stato…</p>
      <p class="hint">Con Wi-Fi e NTP: intervallo massimo dall'ultima ora salvata al riavvio. Senza NTP: durata sconosciuta. Il relè resta spento dopo un reset.</p>
      <p class="hint">La temperatura interna ESP è indicativa: non misura il regolatore 3,3 V né l'aria attorno al modulo.</p>
      <hr>
      <h2>Indicatori</h2>
      <p class="hint">Verde fisso: ESP e server avviati senza allarmi. La dashboard indica se le sonde reali sono disponibili. Rosso acceso: GPIO4 sta comandando il driver del relè. In allarme l'uscita si spegne.</p>
    </aside>
  </main>

  <section id="logsView" class="logs-view panel" hidden>
    <div class="panel-title-row">
      <div><div class="eyebrow">DIAGNOSTICA</div><h2>Log del controllore</h2>
        <p>Azioni del relè, calcoli PID, cambi modalità, avvii, arresti e allarmi.</p></div>
      <div class="log-actions"><button id="refreshLogs" class="ghost">Aggiorna</button><button id="exportLogs" class="ghost">Esporta JSON</button></div>
    </div>
    <div id="logSummary" class="log-summary">Connessione al controllore…</div>
    <p id="logRetention" class="hint">Caricamento eventi…</p>
    <div id="logList" class="log-list">Caricamento…</div>
  </section>
  <footer>Modalità test · Solo carico a bassa tensione · ESP32-S3 + MAX31865</footer>
  <script src="/app.js"></script>
</body>
</html>
)OVEN_ASSET_2026";

const char WEB_CSS[] PROGMEM = R"OVEN_ASSET_2026(:root {
  --bg: #0b0f14;
  --panel: #111821;
  --panel-2: #151f2a;
  --border: #243140;
  --text: #edf2f7;
  --muted: #8fa2b6;
  --accent: #36c58c;
  --accent-2: #4ba3ff;
  --danger: #ff5e6c;
  --warning: #f6b94f;
  --shadow: 0 16px 50px rgba(0,0,0,.25);
}

* { box-sizing: border-box; }

body {
  margin: 0;
  font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  background: radial-gradient(circle at top left, #12202d 0, var(--bg) 38%);
  color: var(--text);
  min-height: 100vh;
}

.topbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 28px 34px 18px;
  max-width: 1600px;
  margin: 0 auto;
}

.eyebrow {
  font-size: 12px;
  letter-spacing: .14em;
  color: var(--accent);
  font-weight: 700;
}

h1 { margin: 5px 0 0; font-size: 30px; }
h2 { margin: 0 0 4px; font-size: 18px; }
h3 { margin: 0 0 4px; font-size: 15px; }
p { color: var(--muted); margin: 0; }

.status-wrap {
  display: flex;
  align-items: center;
  gap: 10px;
  background: rgba(255,255,255,.04);
  border: 1px solid var(--border);
  border-radius: 999px;
  padding: 10px 16px;
  font-size: 13px;
  font-weight: 800;
  letter-spacing: .08em;
}

.status-dot {
  width: 10px; height: 10px; border-radius: 50%;
  background: #5c6672;
  box-shadow: 0 0 0 5px rgba(92,102,114,.12);
}
.status-dot.running { background: var(--accent); box-shadow: 0 0 0 5px rgba(54,197,140,.12); }
.status-dot.paused { background: var(--warning); box-shadow: 0 0 0 5px rgba(246,185,79,.12); }
.status-dot.fault { background: var(--danger); box-shadow: 0 0 0 5px rgba(255,94,108,.12); }

.layout {
  max-width: 1600px;
  margin: 0 auto;
  padding: 0 34px 34px;
  display: grid;
  grid-template-columns: minmax(0, 2fr) minmax(360px, 0.8fr);
  gap: 20px;
}

.panel {
  background: linear-gradient(180deg, rgba(22,31,42,.96), rgba(14,21,29,.96));
  border: 1px solid var(--border);
  border-radius: 22px;
  box-shadow: var(--shadow);
}

.dashboard-panel { padding: 24px; }
.control-panel { padding: 22px; align-self: start; position: sticky; top: 18px; }

.panel-title-row {
  display: flex;
  justify-content: space-between;
  gap: 18px;
  align-items: center;
}
.panel-title-row.compact { align-items: flex-start; }

.clock-box {
  text-align: right;
  background: var(--panel-2);
  border: 1px solid var(--border);
  border-radius: 14px;
  padding: 10px 14px;
}
.clock-box span, .metric > span, .sensor-card span, .recipe-summary span, .field > span {
  display: block;
  color: var(--muted);
  font-size: 12px;
}
.clock-box strong { font-size: 18px; }

.hero-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 12px;
  margin-top: 20px;
}

.metric {
  min-height: 125px;
  background: var(--panel-2);
  border: 1px solid var(--border);
  border-radius: 16px;
  padding: 17px;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}
.metric strong { font-size: 29px; }
.metric strong small { font-size: 15px; color: var(--muted); }
.metric.hero strong { font-size: 38px; }
.metric.target { border-color: rgba(75,163,255,.45); }

.bar {
  height: 7px; width: 100%;
  background: #0b1118; border-radius: 999px; overflow: hidden;
}
.bar > div { height: 100%; width: 0; background: var(--accent); transition: width .2s ease; }

.sensor-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
  margin-top: 12px;
}
.sensor-grid.two { grid-template-columns: repeat(2, 1fr); }
.sensor-card {
  background: rgba(255,255,255,.025);
  border: 1px solid var(--border);
  border-radius: 14px;
  padding: 14px 16px;
}
.sensor-card strong { display: block; margin-top: 5px; font-size: 20px; }

.chart-card {
  margin-top: 12px;
  background: #0d141c;
  border: 1px solid var(--border);
  border-radius: 17px;
  padding: 16px;
}
.chart-head {
  display: flex; justify-content: space-between; gap: 12px; align-items: center;
  margin-bottom: 10px;
}
.legend { display: flex; gap: 14px; color: var(--muted); font-size: 12px; }
.legend-line {
  display: inline-block; width: 22px; height: 3px; border-radius: 4px; vertical-align: middle; margin-right: 6px;
}
.target-line { background: var(--accent-2); }
.actual-line { background: var(--accent); }
.simulated-line { background: var(--warning); }

canvas { width: 100%; height: 360px; display: block; }
.chart-navigation { display: grid; grid-template-columns: auto minmax(90px, 1fr) auto auto auto; align-items: center; gap: 8px; margin-top: 12px; }
.chart-navigation input { min-width: 0; margin: 0; padding: 0; accent-color: var(--accent); }
.chart-navigation select { width: auto; margin: 0; }
.chart-range-label { margin-top: 8px; font-size: 12px; color: var(--muted); }

.field { display: block; margin-top: 14px; }
.field input, .field select, select, input {
  width: 100%;
  margin-top: 7px;
  background: #0e151d;
  border: 1px solid var(--border);
  color: var(--text);
  border-radius: 10px;
  padding: 11px 12px;
  outline: none;
}
.field input:focus, .field select:focus { border-color: var(--accent-2); }

.recipe-summary {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  margin: 14px 0;
}
.recipe-summary > div {
  background: #0d141c;
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 11px;
}
.recipe-summary strong { display: block; margin-top: 5px; font-size: 13px; }

.buttons {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  margin-top: 14px;
}
.buttons.secondary { grid-template-columns: repeat(3, 1fr); }

button, .import-label {
  appearance: none;
  border: 1px solid var(--border);
  background: #17212c;
  color: var(--text);
  border-radius: 10px;
  padding: 11px 12px;
  font-weight: 800;
  cursor: pointer;
  text-align: center;
  font-size: 12px;
}
button:hover, .import-label:hover { filter: brightness(1.15); }
button.primary { background: var(--accent); color: #06130e; border-color: transparent; }
button.danger { background: rgba(255,94,108,.12); border-color: rgba(255,94,108,.35); color: #ff8e98; }
button.ghost, .import-label { background: transparent; }
button.full { width: 100%; margin-top: 9px; }

hr { border: 0; border-top: 1px solid var(--border); margin: 22px 0; }

.steps-editor { margin-top: 12px; display: grid; gap: 10px; }

.step-row {
  border: 1px solid var(--border);
  background: #0d141c;
  border-radius: 13px;
  padding: 11px;
}
.step-row-head {
  display: flex; justify-content: space-between; align-items: center; margin-bottom: 9px;
}
.step-row-head strong { font-size: 13px; }
.step-remove {
  width: auto; padding: 5px 8px; color: #ff8993; background: transparent; border-color: rgba(255,94,108,.25);
}
.step-fields {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
}
.step-fields .wide { grid-column: 1 / -1; }
.step-fields label span { font-size: 10px; color: var(--muted); }
.step-fields input, .step-fields select { margin-top: 4px; padding: 8px; font-size: 12px; }

.alarm {
  margin-top: 14px;
  padding: 12px;
  background: rgba(246,185,79,.11);
  border: 1px solid rgba(246,185,79,.35);
  color: #ffd68a;
  border-radius: 12px;
  font-size: 12px;
}
.alarm strong, .alarm span { display: block; }
.alarm span { margin-top: 3px; }
.hidden { display: none; }
[hidden] { display: none !important; }

.hint {
  margin-top: 12px;
  font-size: 12px;
  line-height: 1.55;
}
.network-box { margin-top: 12px; padding: 14px; border: 1px solid var(--border); border-radius: 12px; background: #0d141c; }
.network-line { display: flex; justify-content: space-between; gap: 12px; align-items: baseline; margin-top: 9px; font-size: 12px; }
.network-line:first-child { margin-top: 0; }
.network-line span, .network-box p { color: var(--muted); }
.network-line strong { text-align: right; }
.network-address { display: block; margin-top: 7px; color: var(--accent-2); font-size: 13px; font-weight: 700; overflow-wrap: anywhere; }
.network-box p { margin-top: 8px; font-size: 12px; line-height: 1.5; }
.test-buttons { grid-template-columns: 2fr 1fr; }
.sim-buttons { grid-template-columns: repeat(2, 1fr); }
.sim-banner { margin-top: 14px; padding: 12px; border: 1px solid rgba(246,185,79,.5); border-radius: 12px; background: rgba(246,185,79,.13); color: #ffd68a; font-size: 12px; line-height: 1.5; font-weight: 700; }
.view-tabs { max-width: 1600px; margin: 0 auto 18px; padding: 0 34px; display: flex; gap: 8px; }
.view-tabs button { min-width: 130px; font-size: 14px; }
.view-tabs button.active { border-color: var(--accent); color: var(--accent); background: rgba(54,197,140,.11); }
.power-summary { grid-template-columns: repeat(3, 1fr); }
.heating { color: var(--danger); }
.notice { margin-top: 12px; padding: 11px; border-radius: 10px; background: rgba(54,197,140,.13); color: #9de6c2; font-size: 12px; }
.notice.error { background: rgba(255,94,108,.13); color: #ff9ca5; }
button:disabled { cursor: not-allowed; opacity: .45; }
.logs-view { max-width: 1532px; margin: 0 auto 34px; padding: 24px; }
.log-actions { display: flex; gap: 8px; flex-wrap: wrap; }
.log-summary { margin-top: 18px; padding: 14px; border: 1px solid var(--border); border-radius: 12px; background: #0d141c; font-size: 13px; line-height: 1.5; }
.log-list { display: grid; gap: 8px; margin-top: 14px; }
.log-row { display: grid; grid-template-columns: 155px 95px 1fr; gap: 10px; align-items: start; padding: 11px 12px; border-radius: 10px; background: #0d141c; border: 1px solid var(--border); font-size: 12px; }
.log-time { color: var(--muted); }
.log-source { color: var(--muted); font-size: 11px; }
.log-warning { color: var(--warning); }
.log-danger { color: var(--danger); }

footer {
  max-width: 1600px;
  margin: 0 auto;
  padding: 0 34px 28px;
  color: var(--muted);
  font-size: 12px;
}

@media (max-width: 1100px) {
  .layout { grid-template-columns: 1fr; }
  .control-panel { position: static; }
  .hero-grid { grid-template-columns: repeat(2, 1fr); }
}

@media (max-width: 680px) {
  .topbar, .layout, .view-tabs, footer { padding-left: 16px; padding-right: 16px; }
  .logs-view { margin-left: 16px; margin-right: 16px; padding: 16px; }
  .topbar { align-items: flex-start; }
  .hero-grid, .sensor-grid { grid-template-columns: 1fr; }
  .buttons, .buttons.secondary { grid-template-columns: 1fr; }
  .test-buttons { grid-template-columns: 2fr 1fr; }
  .sim-buttons { grid-template-columns: repeat(2, 1fr); }
  .sensor-grid.two { grid-template-columns: 1fr; }
  .power-summary { grid-template-columns: 1fr; }
  .log-row { grid-template-columns: 1fr; gap: 3px; }
  .recipe-summary { grid-template-columns: 1fr; }
  canvas { height: 280px; }
  .chart-head { flex-wrap: wrap; }
  .chart-navigation { grid-template-columns: 1fr 1fr 1fr; }
  .chart-navigation input { grid-column: 1 / -1; grid-row: 1; width: 100%; }
  .chart-navigation select { grid-column: 1 / -1; width: 100%; }
}
)OVEN_ASSET_2026";

const char WEB_JS[] PROGMEM = R"OVEN_ASSET_2026(const $ = id => document.getElementById(id);
function setText(id, value) {
  const node = $(id), next = String(value);
  if (node.textContent !== next) node.textContent = next;
}
let history = [];
let livePoints = [];
const LIVE_SPAN_MS = 2 * 60 * 1000;
let historyBoot = null;
let historyBusy = false;
let historyMoreBefore = false;
let backfillBusy = false;
let currentBoot = null;
let deviceClockOffset = Date.now();
let chartAtEnd = true;
let chartStart = 0;
let chartFrame = 0;
let requestBusy = false;
let connected = false;
let currentMode = null;
let lastLogs = [];
let lastLogsKey = "";
let logsBusy = false;
const resetNames = {
  1: "Accensione", 2: "Reset esterno", 3: "Reset software", 4: "Watchdog panic",
  5: "Watchdog interrupt", 6: "Watchdog task", 7: "Watchdog altro",
  8: "Deep sleep", 9: "Brownout", 10: "Reset SDIO"
};

function formatTime(seconds) {
  const n = Math.max(0, Math.floor(seconds || 0));
  return [Math.floor(n / 3600), Math.floor(n / 60) % 60, n % 60]
    .map(x => String(x).padStart(2, "0")).join(":");
}

function message(text, error = false) {
  const box = $("messageBox");
  box.textContent = text;
  box.className = error ? "notice error" : "notice";
  setTimeout(() => { if (box.textContent === text) box.classList.add("hidden"); }, 6000);
}

async function api(path, payload) {
  const response = await fetch(path, payload === undefined ? { cache: "no-store" } : {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload)
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `Errore HTTP ${response.status}`);
  return data;
}

function displayStatus(s) {
  deviceClockOffset = Date.now() - Number(s.uptimeMs);
  if (currentBoot !== null && currentBoot !== s.bootCount) {
    history = []; livePoints = []; historyBoot = null; historyMoreBefore = false; chartAtEnd = true; chartStart = 0;
    lastLogsKey = "";
    drawChart();
  }
  currentBoot = s.bootCount;
  currentMode = s.sensorMode;
  const simulated = s.sensorMode === "simulated";
  $("sensorMode").value = s.sensorMode;
  $("sensorMode").disabled = s.running;
  $("simBanner").hidden = !simulated;
  $("simControls").hidden = !simulated;
  setText("dashboardSubtitle", simulated ? "Temperature SIMULATE · nessuna PT100 richiesta" : "Lettura delle due PT100 reali");
  setText("sensorLabel1", simulated ? "Sonda finta #1" : "PT100 #1 · CS GPIO14");
  setText("sensorLabel2", simulated ? "Sonda finta #2" : "PT100 #2 · CS GPIO10");
  const t = s.sensors.map(item => item.valid ? item.c : null);
  const avg = t.every(Number.isFinite) ? (t[0] + t[1]) / 2 : null;
  setText("avgTemp", avg === null ? "—" : avg.toFixed(1));
  setText("targetTemp", Number(s.target).toFixed(1));
  setText("deltaTemp", avg === null ? "—" : Math.abs(t[0] - t[1]).toFixed(1));
  [0, 1].forEach(i => {
    setText(`t${i + 1}`, t[i] === null ? "Non valida" : `${t[i].toFixed(1)} °C`);
    setText(`fault${i + 1}`, simulated ? "Valore simulato" : s.sensors[i].valid ? "Lettura OK" : `Fault MAX31865: 0x${Number(s.sensors[i].faultCode).toString(16).padStart(2, "0")}`);
  });
  setText("relayState", s.relay ? "ACCESA" : "SPENTA");
  $("relayState").classList.toggle("heating", s.relay);
  setText("relayHint", s.relay ? "LED rosso acceso · GPIO4 alto" : "LED rosso spento · GPIO4 basso");
  setText("pidInfo", `PID ${Math.round(s.pidDuty)}% · finestra 5 s`);
  setText("uptime", formatTime(s.uptimeSec));
  setText("bootCount", s.bootCount);
  setText("resetReason", resetNames[s.resetReason] || `Codice ${s.resetReason}`);
  setText("interrupted", s.interrupted ? "Sì" : "No");
  setText("timeStatus", s.ntpSynced ? "NTP OK" : s.workshopConnected ? "In attesa NTP" : "Non disponibile");
  setText("chipTemperature", s.chipTemperatureC === null ? "—" : `${Number(s.chipTemperatureC).toFixed(1)} °C`);
  setText("workshopState", s.workshopConnected ? "Connesso" : s.workshopConfigured ? "In attesa della rete" : "Non configurato");
  const workshopAddress = $("workshopAddress");
  workshopAddress.hidden = !s.workshopConnected || !s.stationIp;
  if (!workshopAddress.hidden) {
    const url = `http://${s.stationIp}/`;
    if (workshopAddress.href !== url) workshopAddress.href = url;
    setText("workshopAddress", url);
  }
  const localAddress = $("localAddress");
  localAddress.hidden = !s.workshopConnected || !s.localName;
  if (!localAddress.hidden) {
    const url = `http://${s.localName}/`;
    if (localAddress.href !== url) localAddress.href = url;
    setText("localAddress", `${url} (se mDNS disponibile)`);
  }
  setText("workshopHint", s.workshopConnected
    ? "Apri l'indirizzo dal telefono o PC collegato al Wi-Fi del capannone. Se il nome .local non funziona, usa l'IP."
    : s.workshopConfigured ? "Connessione in corso: la dashboard resta disponibile sulla rete diretta dell'ESP."
      : "Configura il Wi-Fi del capannone nel firmware per accedere senza collegarti alla rete dell'ESP.");
  setText("apState", s.apIp ? "Attiva" : "Non disponibile");
  setText("apAddress", s.apIp ? `http://${s.apIp}/` : "");

  const dot = $("statusDot");
  const dotClass = "status-dot " + (s.fault ? "fault" : s.running ? "running" : "idle");
  if (dot.className !== dotClass) dot.className = dotClass;
  setText("statusText", s.fault ? "ALLARME" : s.cutoff ? "SOGLIA 36 °C" : s.running ? (simulated ? "TEST SIMULATO" : "TEST REALE") : simulated ? "SIMULAZIONE PRONTA" : s.ready ? "SONDE REALI PRONTE" : "SONDE NON DISPONIBILI");
  const alarm = $("alarmBox");
  alarm.classList.toggle("hidden", !s.fault && !s.interrupted && !s.cutoff);
  setText("alarmText", s.fault || (s.cutoff ? "Una sonda ha raggiunto 36 °C: relè spento. Il PID riprende quando entrambe sono a 35,5 °C o meno." : s.interrupted ? "Il test precedente si è interrotto per un reset o per la perdita di alimentazione. Il relè è rimasto spento." : ""));
  $("startBtn").disabled = s.running || !s.ready;
  $("stopBtn").disabled = !s.running;
  setText("logSummary", `${simulated ? "SIMULAZIONE" : "SONDE REALI"} · ${avg === null ? "Temperatura non disponibile" : `T1 ${t[0].toFixed(1)} °C · T2 ${t[1].toFixed(1)} °C · media ${avg.toFixed(1)} °C`} · PID ${Math.round(s.pidDuty)}% · relè ${s.relay ? "ACCESO" : "SPENTO"}${s.fault ? ` · ALLARME: ${s.fault}` : ""}`);

  if (s.interrupted) {
    setText("outageInfo", s.outageUpperBoundSec > 0
      ? `Test precedente interrotto; relè spento. Dall'ultima ora salvata al riavvio: ${formatTime(s.outageUpperBoundSec)} al massimo. La durata effettiva senza corrente è minore e non è nota con precisione.`
      : "Test precedente interrotto; relè spento. Durata senza corrente sconosciuta: manca un'ora NTP salvata o non è ancora tornata la sincronizzazione.");
  } else {
    setText("outageInfo", "Nessun test interrotto registrato all'ultimo riavvio.");
  }
  const liveMs = Number(s.sampleAtMs);
  if (Number.isFinite(liveMs) && liveMs > 0 && liveMs > (livePoints.at(-1)?.ms ?? -1)) {
    livePoints.push({ ms: liveMs, actual: avg, sensor1: t[0], sensor2: t[1],
      target: Number(s.target), pidDuty: s.pidDuty, relay: s.relay, mode: s.sensorMode });
    while (livePoints.length > 1 && livePoints[0].ms < liveMs - LIVE_SPAN_MS) livePoints.shift();
    scheduleChartDraw();
  }
}

function formatChartTime(deviceMs) {
  return new Date(deviceClockOffset + deviceMs).toLocaleTimeString("it-IT", { hour: "2-digit", minute: "2-digit", second: "2-digit" });
}

function scheduleChartDraw() {
  if (chartFrame) return;
  chartFrame = requestAnimationFrame(() => { chartFrame = 0; drawChart(); });
}

function drawChart() {
  const canvas = $("tempChart"), ctx = canvas.getContext("2d");
  const W = canvas.width, H = canvas.height, p = { l: 60, r: 18, t: 18, b: 38 };
  ctx.fillStyle = "#0d141c"; ctx.fillRect(0, 0, W, H);
  const firstLiveMs = livePoints.length ? livePoints[0].ms : Infinity;
  const plotPoints = history.filter(point => point.ms < firstLiveMs).concat(livePoints);
  if (!plotPoints.length) {
    $("chartRangeLabel").textContent = historyBusy ? "Caricamento dei dati recenti…" : "Nessun campione ancora disponibile sull'ESP.";
    for (const id of ["chartBack", "chartForward", "chartNow", "chartPosition"]) $(id).disabled = true;
    return;
  }
  const oldest = plotPoints[0].ms, latest = plotPoints.at(-1).ms;
  const selected = $("chartSpan").value;
  const span = selected === "all" ? Math.max(60000, latest - oldest) : Math.max(60000, Math.min(Number(selected), Math.max(60000, latest - oldest)));
  const maxStart = Math.max(oldest, latest - span);
  if (maxStart === oldest) chartAtEnd = true;
  if (chartAtEnd) chartStart = maxStart;
  else chartStart = Math.max(oldest, Math.min(chartStart, maxStart));
  const end = chartStart + span;
  const visible = plotPoints.filter(point => point.ms >= chartStart && point.ms <= end);
  const values = visible.map(point => point.actual).filter(Number.isFinite);
  const low = Math.floor((Math.min(35, ...values) - 2) / 5) * 5;
  const high = Math.max(low + 10, Math.ceil((Math.max(35, ...values) + 2) / 5) * 5);
  const y = c => p.t + (H - p.t - p.b) * (1 - (c - low) / (high - low));
  const x = ms => p.l + (ms - chartStart) / span * (W - p.l - p.r);
  ctx.font = "17px system-ui"; ctx.lineWidth = 1;
  for (let c = low; c <= high; c += 5) {
    const py = y(c);
    ctx.strokeStyle = "#20303e"; ctx.beginPath(); ctx.moveTo(p.l, py); ctx.lineTo(W - p.r, py); ctx.stroke();
    ctx.fillStyle = "#74879a"; ctx.fillText(`${c}°`, 8, py + 6);
  }
  ctx.strokeStyle = "#4ba3ff"; ctx.lineWidth = 3; ctx.beginPath();
  ctx.moveTo(p.l, y(35)); ctx.lineTo(W - p.r, y(35)); ctx.stroke();
  for (const [mode, color] of [["real", "#36c58c"], ["simulated", "#f6b94f"]]) {
    let drawing = false;
    ctx.strokeStyle = color; ctx.lineWidth = 4; ctx.beginPath();
    for (const point of visible) {
      if (point.mode !== mode || !Number.isFinite(point.actual)) { drawing = false; continue; }
      if (drawing) ctx.lineTo(x(point.ms), y(point.actual));
      else ctx.moveTo(x(point.ms), y(point.actual));
      drawing = true;
    }
    ctx.stroke();
    if (visible.length <= 240) {
      ctx.fillStyle = color;
      for (const point of visible) {
        if (point.mode !== mode || !Number.isFinite(point.actual)) continue;
        ctx.beginPath(); ctx.arc(x(point.ms), y(point.actual), 2.5, 0, Math.PI * 2); ctx.fill();
      }
    }
  }
  ctx.fillStyle = "#8fa2b6"; ctx.font = "15px system-ui";
  ctx.fillText(formatChartTime(chartStart), p.l, H - 9);
  const middle = formatChartTime(chartStart + span / 2);
  ctx.fillText(middle, (W - ctx.measureText(middle).width) / 2, H - 9);
  const endText = formatChartTime(end);
  ctx.fillText(endText, W - p.r - ctx.measureText(endText).width, H - 9);
  const slider = $("chartPosition");
  slider.min = String(oldest); slider.max = String(maxStart); slider.step = "1000";
  slider.value = String(chartStart); slider.disabled = maxStart === oldest;
  $("chartBack").disabled = selected === "all" || (chartStart <= oldest && !historyMoreBefore);
  $("chartForward").disabled = chartStart >= maxStart;
  $("chartNow").disabled = chartAtEnd || maxStart === oldest;
  $("chartRangeLabel").textContent = `${formatChartTime(chartStart)} – ${formatChartTime(Math.min(end, latest))} · ${visible.length} punti visibili · ${history.length} nello storico${historyMoreBefore ? " · dati più vecchi disponibili" : ""} · ${chartAtEnd ? "tempo reale" : "storico"}`;
}

function decodeHistoryRow(row) {
  const [seq, ms, sensor1, sensor2, pidDuty, flags] = row;
  return { seq, ms, sensor1, sensor2,
    actual: Number.isFinite(sensor1) && Number.isFinite(sensor2) ? (sensor1 + sensor2) / 2 : null,
    target: 35, pidDuty, relay: Boolean(flags & 4), mode: flags & 8 ? "simulated" : "real" };
}

async function refreshHistory() {
  if (historyBusy || !connected) return;
  historyBusy = true;
  try {
    const initial = historyBoot === null;
    const after = history.at(-1)?.seq || 0;
    const data = await api(initial ? "/api/history?tail=1" : `/api/history?after=${after}`);
    if (currentBoot !== null && data.bootCount !== currentBoot) return;
    if (initial) { history = []; historyBoot = data.bootCount; }
    const added = data.samples.map(decodeHistoryRow).filter(point => point.seq > after || initial);
    if (added.length) history.push(...added);
    history = history.filter(point => point.seq >= data.oldestSeq);
    historyMoreBefore = history.length > 0 && history[0].seq > data.oldestSeq;
    if (initial || added.length) scheduleChartDraw();
  } catch (e) {
    if (!history.length) $("chartRangeLabel").textContent = `Storico non disponibile: ${e.message}`;
  } finally {
    historyBusy = false;
    if (historyMoreBefore && $("chartSpan").value === "all") setTimeout(loadAllHistory, 0);
  }
}

async function loadOlderHistory() {
  if (historyBusy || !connected || !history.length || !historyMoreBefore) return false;
  historyBusy = true;
  try {
    const before = history[0].seq;
    const data = await api(`/api/history?before=${before}`);
    if (currentBoot !== null && data.bootCount !== currentBoot) return false;
    const older = data.samples.map(decodeHistoryRow).filter(point => point.seq < before);
    if (!older.length) { historyMoreBefore = false; drawChart(); return false; }
    history = [...older, ...history].filter(point => point.seq >= data.oldestSeq);
    historyMoreBefore = history[0].seq > data.oldestSeq;
    scheduleChartDraw();
    return true;
  } catch (e) {
    $("chartRangeLabel").textContent = `Impossibile caricare i dati precedenti: ${e.message}`;
    return false;
  } finally { historyBusy = false; }
}

async function loadAllHistory() {
  if (backfillBusy) return;
  backfillBusy = true;
  try {
    while (connected && historyMoreBefore && $("chartSpan").value === "all") {
      if (historyBusy) { await new Promise(resolve => setTimeout(resolve, 200)); continue; }
      if (!await loadOlderHistory()) break;
      await new Promise(resolve => setTimeout(resolve, 300));
    }
  } finally { backfillBusy = false; }
}

async function moveChart(direction) {
  if (!history.length) return;
  const step = Number($("chartSpan").value) * 0.75;
  if (direction < 0 && historyMoreBefore && chartStart - step < history[0].ms) await loadOlderHistory();
  chartAtEnd = false;
  chartStart += direction * step;
  drawChart();
}

async function refreshLogs() {
  if (logsBusy) return;
  logsBusy = true;
  try {
    const data = await api("/api/logs");
    const last = data.events.at(-1);
    const key = `${currentBoot}:${data.events.length}:${last?.seq || 0}`;
    if (key === lastLogsKey) return;
    lastLogsKey = key;
    lastLogs = data.events;
    $("logRetention").textContent = `La pagina mostra fino a ${data.capacity} eventi recenti. I ${data.savedCapacity} eventi principali più recenti restano salvati dopo un riavvio; i dettagli PID/relè sono disponibili durante questa accensione.`;
    const list = $("logList");
    list.replaceChildren();
    if (!data.events.length) { list.textContent = "Nessun evento registrato."; return; }
    for (const event of [...data.events].reverse()) {
      const row = document.createElement("div"); row.className = "log-row";
      const when = document.createElement("span"); when.className = "log-time";
      when.textContent = event.utc ? new Date(Number(event.utc) * 1000).toLocaleString("it-IT") : `Avvio / evento #${event.seq}`;
      const level = document.createElement("strong"); level.className = event.level === "ALLARME" ? "log-danger" : event.level === "ATTENZIONE" ? "log-warning" : "";
      level.textContent = event.level;
      const body = document.createElement("div");
      const detail = document.createElement("span"); detail.textContent = event.message;
      const source = document.createElement("div"); source.className = "log-source";
      source.textContent = event.saved ? "Salvato nella memoria ESP" : "Dettaglio di questa accensione";
      body.append(detail, source);
      row.append(when, level, body); list.appendChild(row);
    }
  } catch (e) { lastLogsKey = ""; $("logList").textContent = `Registro non disponibile: ${e.message}`; }
  finally { logsBusy = false; }
}

async function refresh() {
  if (requestBusy) return;
  requestBusy = true;
  try {
    const s = await api("/api/status");
    const wasConnected = connected;
    connected = true;
    displayStatus(s);
    if (!wasConnected && !$("logsView").hidden) refreshLogs();
    if (historyBoot === null || s.historyLatestSeq > (history.at(-1)?.seq || 0)) refreshHistory();
  } catch (e) {
    connected = false;
    $("statusText").textContent = "ESP NON RAGGIUNGIBILE";
    $("statusDot").className = "status-dot fault";
    $("startBtn").disabled = true;
  } finally { requestBusy = false; }
}

async function command(action) {
  try {
    const payload = { action };
    const result = await api("/api/command", payload);
    displayStatus(result);
    if (!$("logsView").hidden) refreshLogs();
    if (result.historyLatestSeq > (history.at(-1)?.seq || 0)) refreshHistory();
    const labels = { start: "Test avviato.", stop: "Test fermato.", reset: "Allarme azzerato.", simUp: "Temperatura simulata aumentata.", simDown: "Temperatura simulata diminuita." };
    message(labels[action] || "Comando eseguito.");
  } catch (e) { message(e.message, true); await refresh(); }
}

function exportSamples() {
  if (!history.length) { message("Non ci sono ancora campioni da esportare.", true); return; }
  const lines = ["ora_iso,sonda1_c,sonda2_c,media_c,target_c,pid_percento,rele,origine"];
  for (const p of history) {
    lines.push([new Date(deviceClockOffset + p.ms).toISOString(),
      Number.isFinite(p.sensor1) ? p.sensor1.toFixed(2) : "",
      Number.isFinite(p.sensor2) ? p.sensor2.toFixed(2) : "",
      Number.isFinite(p.actual) ? p.actual.toFixed(2) : "",
      p.target.toFixed(2), p.pidDuty.toFixed(1), p.relay ? 1 : 0, p.mode].join(","));
  }
  const url = URL.createObjectURL(new Blob([lines.join("\n") + "\n"], { type: "text/csv" }));
  const a = document.createElement("a"); a.href = url; a.download = "oven-test-pid.csv"; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function changeMode() {
  const requested = $("sensorMode").value === "simulated";
  try {
    const s = await api("/api/mode", { simulated: requested });
    displayStatus(s);
    if (!$("logsView").hidden) refreshLogs();
    if (s.historyLatestSeq > (history.at(-1)?.seq || 0)) refreshHistory();
    message(requested ? "Sonde simulate attivate: valori finti, relè di prova abilitato." : "Sonde reali attivate.");
  } catch (e) {
    $("sensorMode").value = currentMode || "real";
    message(e.message, true);
  }
}

function exportLogs() {
  if (!lastLogs.length) { message("Nessun log disponibile.", true); return; }
  const url = URL.createObjectURL(new Blob([JSON.stringify(lastLogs, null, 2)], { type: "application/json" }));
  const a = document.createElement("a"); a.href = url; a.download = "oven-controller-logs.json"; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function showView(view) {
  const logs = view === "logs";
  $("dashboardView").hidden = logs;
  $("logsView").hidden = !logs;
  $("tabDashboard").classList.toggle("active", !logs);
  $("tabLogs").classList.toggle("active", logs);
  $("tabDashboard").setAttribute("aria-selected", String(!logs));
  $("tabLogs").setAttribute("aria-selected", String(logs));
  if (logs) refreshLogs();
}

$("startBtn").addEventListener("click", () => command("start"));
$("stopBtn").addEventListener("click", () => command("stop"));
$("resetBtn").addEventListener("click", () => command("reset"));
$("sensorMode").addEventListener("change", changeMode);
$("simUpBtn").addEventListener("click", () => command("simUp"));
$("simDownBtn").addEventListener("click", () => command("simDown"));
$("refreshLogs").addEventListener("click", refreshLogs);
$("exportLogs").addEventListener("click", exportLogs);
$("exportSamples").addEventListener("click", exportSamples);
$("chartBack").addEventListener("click", () => moveChart(-1));
$("chartForward").addEventListener("click", () => moveChart(1));
$("chartNow").addEventListener("click", () => { chartAtEnd = true; drawChart(); });
$("chartPosition").addEventListener("input", event => { chartAtEnd = Number(event.target.value) >= Number(event.target.max) - 1000; chartStart = Number(event.target.value); scheduleChartDraw(); });
$("chartSpan").addEventListener("change", () => { if ($("chartSpan").value === "all") { chartAtEnd = true; loadAllHistory(); } drawChart(); });
$("tabDashboard").addEventListener("click", () => showView("dashboard"));
$("tabLogs").addEventListener("click", () => showView("logs"));
drawChart(); refresh();
setInterval(refresh, 500);
setInterval(() => { if (!$("logsView").hidden) refreshLogs(); }, 5000);
// The phone/PC supplies wall time only for log timestamps, never for outage duration.
async function syncTime() {
  try { await api("/api/time", { unixMs: Date.now() }); } catch (_) {}
}
setTimeout(syncTime, 2000); setInterval(syncTime, 30000);
)OVEN_ASSET_2026";
