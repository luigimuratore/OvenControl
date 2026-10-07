#pragma once
#include <Arduino.h>
// Generated from data/ by scripts/embed_web.py.

const char WEB_HTML[] PROGMEM = R"OVEN_ASSET_2026(<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Oven Controller · Controllo completo</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header class="topbar"><div><div class="eyebrow">ESP32-S3 · VERSIONE COMPLETA</div><h1>Oven Controller</h1></div>
    <div class="status-wrap"><span id="statusDot" class="status-dot idle"></span><span id="statusText">Connessione…</span></div></header>
  <nav class="view-tabs"><button id="tabDashboard" class="active">Dashboard</button><button id="tabRecipes">Ricette</button><button id="tabLogs">Log</button></nav>
  <div id="globalMessage" class="global-message notice hidden" role="status" aria-live="polite"></div>
  <button id="globalStop" class="danger global-stop" disabled>STOP · USCITA SPENTA</button>
  <main id="dashboardView" class="layout">
    <section class="panel dashboard-panel">
      <div class="panel-title-row"><div><h2>Forno</h2><p id="cycleInfo">In attesa del controllore</p></div><div class="clock-box"><span>ESP acceso da</span><strong id="uptime">—</strong></div></div>
      <div class="hero-grid">
        <article class="metric hero"><span>Media PT100</span><strong><span id="avgTemp">—</span><small> °C</small></strong></article>
        <article class="metric hero target"><span>Setpoint di rampa</span><strong><span id="targetTemp">—</span><small> °C</small></strong></article>
        <article class="metric"><span>Uscita / LED rosso</span><strong id="relayState">SPENTO</strong><small id="pidInfo">PID 0%</small></article>
        <article class="metric"><span>ΔT sonde</span><strong><span id="deltaTemp">—</span><small> °C</small></strong><small>Arresto oltre 10 °C</small></article>
      </div>
      <div class="sensor-grid two"><article class="sensor-card"><span>PT100 #1 · CS GPIO14</span><strong id="t1">—</strong><small id="fault1">—</small></article><article class="sensor-card"><span>PT100 #2 · CS GPIO10</span><strong id="t2">—</strong><small id="fault2">—</small></article></div>
      <div class="chart-card"><div class="chart-head"><div><h3>Temperature nel tempo</h3><small>Live 0,5 s · storico ESP ogni 10 s, fino a 12 ore</small></div><div class="legend"><span><i class="legend-line target-line"></i>Setpoint</span><span><i class="legend-line actual-line"></i>Media PT100</span><span><i class="legend-line start-marker-line"></i>Nuovo ciclo</span><span><i class="legend-line end-marker-line"></i>STOP / fine / allarme</span></div><button id="exportSamples" class="ghost">Esporta CSV completo</button></div>
        <canvas id="tempChart" width="1200" height="420" aria-label="Grafico temperature"></canvas>
        <div class="chart-navigation"><button id="chartBack">← Indietro</button><input id="chartPosition" type="range" min="0" max="0" value="0" aria-label="Posizione grafico"><button id="chartForward">Avanti →</button><button id="chartNow">Adesso</button><select id="chartSpan"><option value="900000">15 min</option><option value="1800000" selected>30 min</option><option value="3600000">1 ora</option><option value="all">Tutto</option></select></div>
        <p id="chartRangeLabel" class="chart-range-label">Caricamento…</p>
      </div>
    </section>
    <aside class="panel control-panel">
      <h2>Controllo ciclo</h2><p>La rampa avanza in °C/min. Il mantenimento conta solo il tempo con entrambe le PT100 entro ±1 °C. Il cooldown è passivo: uscita spenta.</p>
      <label class="field"><span>Ricetta</span><select id="recipeSelect"></select></label>
      <ol id="recipePreview" class="recipe-preview"></ol>
      <label class="hardware-ready"><input id="hardwareReady" type="checkbox"> Collaudo hardware concluso e protezioni indipendenti verificate.</label>
      <div class="recipe-summary"><div><span>Fase</span><strong id="stepPosition">—</strong></div><div><span>Target finale step</span><strong id="finalTarget">—</strong></div><div><span>Rate / mantenimento</span><strong id="rateInfo">—</strong></div></div>
      <div class="buttons site-buttons"><button id="startBtn" class="primary">AVVIA CICLO</button><button id="pauseBtn">PAUSA</button><button id="resumeBtn">RIPRENDI</button><button id="stopBtn" class="danger">STOP</button></div>
      <button id="resetBtn" class="ghost full">Riconosci allarme / interruzione</button>
      <div class="recipe-summary cycle-timing"><div><span>Tempo ciclo, incluse pause</span><strong id="cycleDuration">—</strong></div><div><span>Tempo step attivo</span><strong id="stepDuration">—</strong></div></div>
      <p class="hint">Il ciclo continua anche se chiudi la pagina o perdi il Wi-Fi. STOP e PAUSA spengono l'uscita. Dopo un reset non c'è ripresa automatica.</p>
      <div id="alarmBox" class="alarm hidden"><strong>Attenzione</strong><span id="alarmText"></span></div><div id="messageBox" class="notice hidden"></div>
      <hr><h2>Accesso alla dashboard</h2><div class="network-box"><div class="network-line"><span>Wi-Fi capannone</span><strong id="workshopState">—</strong></div><a id="workshopAddress" class="network-address" hidden></a><a id="localAddress" class="network-address" hidden></a><div class="network-line"><span>Rete diretta OvenController-Full</span><strong id="apState">—</strong></div><span id="apAddress" class="network-address"></span></div>
      <hr><h2>Corrente e ripartenza</h2><div class="recipe-summary power-summary"><div><span>Avvii</span><strong id="bootCount">—</strong></div><div><span>Ultimo reset</span><strong id="resetReason">—</strong></div><div><span>Ora NTP</span><strong id="timeStatus">—</strong></div><div><span>Temperatura interna ESP</span><strong id="chipTemperature">—</strong></div></div><p id="outageInfo" class="hint">Nessun dato.</p>
      <p class="hint">Temperatura interna indicativa: non misura il regolatore 3,3 V né l'aria attorno al modulo.</p>
      <p class="hint">Dopo spegnimento/reset il ciclo non riparte da solo. La durata mostrata è il massimo intervallo dall'ultimo salvataggio al riavvio, non la misura esatta del blackout.</p>
      <hr><h2>Diagnostica circuito</h2><div class="recipe-summary power-summary"><div><span>Attuatore</span><strong id="actuatorInfo">—</strong></div><div><span>Potenza massima richiesta</span><strong id="powerInfo">—</strong></div><div><span>RAM libera / minimo</span><strong id="heapInfo">—</strong></div><div><span>Età letture</span><strong id="sampleAge">—</strong></div><div><span>Segnale Wi-Fi</span><strong id="rssiInfo">—</strong></div></div>
      <p class="hint">L'uscita indica il comando GPIO, senza feedback di corrente o dei contatti. Puoi modificare attuatore, finestra e limite potenza nella scheda Ricette.</p>
    </aside>
  </main>
  <section id="recipesView" class="logs-view panel" hidden><div class="panel-title-row"><div><div class="eyebrow">PROFILI</div><h2>Ricette sul controllore</h2><p>Modifica e salva sull'ESP. Massimo 8 ricette e 12 step ciascuna.</p></div><div class="log-actions"><button id="addRecipe" class="ghost">Nuova ricetta</button><button id="exportRecipes" class="ghost">Esporta JSON</button><label class="import-label">Importa JSON<input id="importRecipes" type="file" accept="application/json,.json" hidden></label></div></div>
    <label class="field"><span>Ricetta da modificare</span><select id="editRecipeSelect"></select></label><label class="field"><span>Nome</span><input id="recipeName" maxlength="48"></label>
    <div id="stepsEditor" class="steps-editor"></div><div class="buttons recipe-buttons"><button id="addStep">Aggiungi step</button><button id="deleteRecipe" class="danger">Elimina ricetta</button><button id="saveRecipes" class="primary">Salva sull'ESP</button></div>
    <p class="hint">Rate minimo 0,1 °C/min, senza massimo software. Ramp: salita del setpoint in °C/min. Hold: minuti effettivi con entrambe le sonde entro ±1 °C. Cooldown: discesa naturale; il rate è il riferimento sul grafico e non viene forzato dal forno.</p><div id="recipeMessage" class="notice hidden"></div>
    <hr><h2>Tuning PID</h2><p>Valori iniziali prudenziali da tarare sul forno. Modificabili solo a ciclo fermo. Limiti: Kp 0-50, Ki 0-0,5, Kd 0-200.</p>
    <div class="pid-fields"><label class="field"><span>Kp</span><input id="kp" type="number" min="0" max="50" step="0.1"></label><label class="field"><span>Ki</span><input id="ki" type="number" min="0" max="0.5" step="0.001"></label><label class="field"><span>Kd</span><input id="kd" type="number" min="0" max="200" step="0.1"></label></div><button id="savePid" class="ghost full">Salva PID sull'ESP</button>
    <hr><h2>Attuatore e limite potenza</h2><p>Seleziona l'attuatore realmente installato. Il limite è la percentuale di tempo ON richiesta, senza misura della potenza elettrica.</p>
    <div class="pid-fields"><label class="field"><span>Tipo attuatore</span><select id="actuator"><option value="relay">Relè meccanico</option><option value="ssr">SSR</option></select></label><label class="field"><span>Finestra comando · s</span><input id="windowSec" type="number" min="60" max="300" step="1"></label><label class="field"><span>Limite comando · %</span><input id="maxPower" type="number" min="1" max="100" step="1"></label></div>
    <button id="saveControl" class="ghost full">Salva comando sull'ESP</button><p class="hint">Default: relè meccanico, finestra 60 s, comando massimo 30%. SSR: finestra 1–300 s; relè: 60–300 s. Questi valori iniziali e il PID richiedono taratura sul forno. Il firmware non rileva il tipo di attuatore.</p>
  </section>
  <section id="logsView" class="logs-view panel" hidden><div class="panel-title-row"><div><div class="eyebrow">DIAGNOSTICA</div><h2>Log del controllore</h2><p>Avvio/fine ciclo, step, PID, allarmi, rete e interruzioni.</p></div><div class="log-actions"><button id="refreshLogs" class="ghost">Aggiorna</button><button id="exportLogs" class="ghost">Esporta JSON</button></div></div><p id="logRetention" class="hint">Caricamento…</p><div id="logList" class="log-list"></div></section>
  <footer>Controllo completo · Due PT100 reali obbligatorie · Protezioni hardware indipendenti richieste</footer>
  <script src="/app.js"></script>
</body></html>
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
.legend { display: flex; gap: 10px 14px; flex-wrap: wrap; color: var(--muted); font-size: 12px; }
.legend-line {
  display: inline-block; width: 22px; height: 3px; border-radius: 4px; vertical-align: middle; margin-right: 6px;
}
.target-line { background: var(--accent-2); }
.actual-line { background: var(--accent); }
.simulated-line { background: var(--warning); }
.start-marker-line { width: 4px; height: 14px; background: #cc9cff; }
.end-marker-line { width: 4px; height: 14px; background: repeating-linear-gradient(to bottom, var(--warning) 0 5px, transparent 5px 8px); }

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
.site-buttons { grid-template-columns: repeat(2, 1fr); }
.recipe-buttons { grid-template-columns: repeat(3, 1fr); }
.step-fields { grid-template-columns: repeat(3, 1fr); }
.step-fields .wide { grid-column: auto; }
.import-label { display: inline-flex; align-items: center; justify-content: center; }
.import-label input { display: none; }
.pid-fields { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
.global-stop { position: fixed; bottom: 16px; right: 18px; z-index: 20; padding: 16px 22px; background: #5c2029 !important; color: #fff !important; box-shadow: 0 4px 22px rgba(0,0,0,.5); }
.global-message { max-width: 1532px; margin: 0 34px 16px; }
.hardware-ready { display: flex; gap: 10px; align-items: flex-start; padding: 12px; margin-top: 14px; border: 1px solid var(--border); border-radius: 12px; font-size: 13px; line-height: 1.5; }
.hardware-ready input { width: 18px; height: 18px; margin: 2px 0 0; flex-shrink: 0; accent-color: var(--accent); }
.recipe-preview { padding-left: 20px; margin-top: 14px; color: var(--muted); font-size: 12px; line-height: 1.7; }
.cycle-timing { grid-template-columns: repeat(2, 1fr); }
button:focus-visible, input:focus-visible, select:focus-visible, a:focus-visible { outline: 2px solid var(--accent-2); outline-offset: 3px; }
body { padding-bottom: 65px; }

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
  .global-message { margin-left: 16px; margin-right: 16px; }
  .view-tabs button { min-width: 0; flex: 1; }
  .topbar, .layout, .view-tabs, footer { padding-left: 16px; padding-right: 16px; }
  .logs-view { margin-left: 16px; margin-right: 16px; padding: 16px; }
  .topbar { align-items: flex-start; }
  .hero-grid, .sensor-grid { grid-template-columns: 1fr; }
  .buttons, .buttons.secondary { grid-template-columns: 1fr; }
  .site-buttons { grid-template-columns: repeat(2, 1fr); }
  .recipe-buttons { grid-template-columns: 1fr; }
  .step-fields { grid-template-columns: 1fr; }
  .pid-fields { grid-template-columns: 1fr; }
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
const put = (id, value) => { const el = $(id), text = String(value); if (el.textContent !== text) el.textContent = text; };
const labels = { idle: "PRONTO", running: "CICLO IN CORSO", paused: "IN PAUSA", complete: "CICLO FINITO", fault: "ALLARME", interrupted: "CICLO INTERROTTO" };
const resetNames = { 1: "Accensione", 2: "Reset esterno", 3: "Reset software", 4: "Watchdog panic", 5: "Watchdog interrupt", 6: "Watchdog task", 7: "Watchdog altro", 8: "Deep sleep", 9: "Brownout", 10: "Reset SDIO" };
const types = { ramp: "Ramp", hold: "Hold", cooldown: "Cooldown" };
const markerNames = { 16: "INIZIO CICLO", 32: "STOP", 64: "FINE CICLO", 128: "ALLARME" };
let recipes = [], selectedId = "", editId = "", latest = null, connected = false, busy = false;
let pidLoaded = false;
let settingsLoaded = false, recipesLoaded = false, recipesDirty = false, commandBusy = false, commandGeneration = 0, exportBusy = false;
let recipeLoadBusy = false;
let events = [], eventKey = "", logsBusy = false;
let history = [], live = [], historyBusy = false, hasOlder = false, boot = null;
let chartAtEnd = true, chartStart = 0, timeOffset = Date.now(), chartFrame = 0;
const LIVE_MS = 120000;

function note(id, text, isError = false) {
  const box = $(id); box.textContent = text; box.className = isError ? "notice error" : "notice";
  setTimeout(() => { if (box.textContent === text) box.classList.add("hidden"); }, 7000);
}
async function api(path, body) {
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 2500);
  try {
    const response = await fetch(path, body === undefined ? { cache: "no-store", signal: controller.signal } : {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body), signal: controller.signal
    });
    const data = await response.json();
    if (!response.ok) throw Error(data.error || `HTTP ${response.status}`);
    return data;
  } catch (e) { if (e.name === "AbortError") throw Error("ESP non risponde: verifica il collegamento."); throw e; }
  finally { clearTimeout(timer); }
}
function clock(sec) {
  const n = Math.max(0, Math.floor(Number(sec) || 0));
  return [Math.floor(n / 3600), Math.floor(n / 60) % 60, n % 60].map(x => String(x).padStart(2, "0")).join(":");
}
function temp(value) { return Number.isFinite(value) ? value.toFixed(1) : "—"; }
function dateAt(ms) { return new Date(timeOffset + ms).toLocaleTimeString("it-IT", { hour: "2-digit", minute: "2-digit", second: "2-digit" }); }

function display(s) {
  if (boot !== null && boot !== s.bootCount) {
    history = []; live = []; hasOlder = false; chartAtEnd = true; chartStart = 0; eventKey = "";
    pidLoaded = settingsLoaded = false; $("hardwareReady").checked = false;
    if (!recipesDirty) { recipesLoaded = false; loadRecipes().catch(() => {}); }
  }
  boot = s.bootCount; latest = s; connected = true;
  timeOffset = (s.utcSec ? s.utcSec * 1000 : Date.now()) - Number(s.uptimeMs);
  const t1 = s.sensors[0].valid ? Number(s.sensors[0].c) : NaN;
  const t2 = s.sensors[1].valid ? Number(s.sensors[1].c) : NaN;
  const avg = Number.isFinite(t1) && Number.isFinite(t2) ? (t1 + t2) / 2 : NaN;
  const target = s.target === null ? NaN : Number(s.target);
  put("avgTemp", temp(avg)); put("targetTemp", temp(target));
  put("deltaTemp", temp(Math.abs(t1 - t2)));
  put("t1", Number.isFinite(t1) ? `${temp(t1)} °C` : "Non valida");
  put("t2", Number.isFinite(t2) ? `${temp(t2)} °C` : "Non valida");
  for (let i = 0; i < 2; i++) {
    const sensor = s.sensors[i];
    put(`fault${i + 1}`, `${sensor.message} · ${Number(sensor.ohms).toFixed(2)} Ω · RAW ${sensor.raw} · RREF ${sensor.rref} Ω · Fault 0x${Number(sensor.faultCode).toString(16).padStart(2, "0")}`);
  }
  put("relayState", s.relay ? "ACCESO" : "SPENTO"); $("relayState").classList.toggle("heating", s.relay);
  put("pidInfo", `PID ${Math.round(s.duty)}% · finestra ${s.windowSec} s · max ${s.maxPower}%`);
  put("uptime", clock(s.uptimeMs / 1000));
  put("cycleInfo", s.recipeName && (s.phase === "running" || s.phase === "paused") ? s.recipeName : "Due PT100 reali · nessuna simulazione");
  put("stepPosition", s.phase === "complete" ? `${s.stepCount} / ${s.stepCount} · Terminata` : s.stepCount && s.stepIndex < s.stepCount ? `${s.stepIndex + 1} / ${s.stepCount} · ${types[s.stepType] || "—"}` : "—");
  put("finalTarget", Number.isFinite(s.stepFinalTarget) ? `${temp(s.stepFinalTarget)} °C` : "—");
  put("rateInfo", s.stepType === "hold" ? `${clock(s.holdInBandSec)} / ${clock(s.holdRequiredSec)}` :
    s.stepRate ? `${Number(s.stepRate).toFixed(2)} °C/min` : "—");
  put("bootCount", s.bootCount);
  put("resetReason", resetNames[s.resetReason] || `Codice ${s.resetReason}`);
  put("timeStatus", s.ntpSynced ? "Sincronizzata" : "Non disponibile");
  put("chipTemperature", s.chipTemperatureC === null ? "—" : `${Number(s.chipTemperatureC).toFixed(1)} °C`);
  put("cycleDuration", clock(s.cycleElapsedSec)); put("stepDuration", clock(s.stepElapsedSec));
  put("actuatorInfo", s.actuator === "ssr" ? "SSR" : "Relè meccanico");
  put("powerInfo", `${s.maxPower}% · finestra ${s.windowSec} s`);
  put("heapInfo", `${Math.round(s.freeHeap / 1024)} / ${Math.round(s.minFreeHeap / 1024)} kB`);
  put("sampleAge", `${s.sampleAgeMs} ms`); put("rssiInfo", s.workshopConnected ? `${s.rssi} dBm` : "—");
  put("workshopState", s.workshopConnected ? "Connesso" : s.workshopConfigured ? "In attesa" : "Non configurato");
  const station = $("workshopAddress"); station.hidden = !s.stationIp;
  if (s.stationIp) { station.href = `http://${s.stationIp}/`; station.textContent = station.href; }
  const local = $("localAddress"); local.hidden = !s.localName;
  if (s.localName) { local.href = `http://${s.localName}/`; local.textContent = `${local.href} (se mDNS disponibile)`; }
  put("apState", s.apIp ? "Attiva" : "Non disponibile");
  put("apAddress", s.apIp ? `http://${s.apIp}/` : "");
  const bad = s.phase === "fault" || s.phase === "interrupted";
  $("statusDot").className = `status-dot ${bad ? "fault" : s.phase === "running" ? "running" : s.phase === "paused" ? "paused" : "idle"}`;
  put("statusText", labels[s.phase] || s.phase);
  $("alarmBox").classList.toggle("hidden", !bad);
  put("alarmText", s.fault || (s.phase === "interrupted" ? "Il ciclo precedente si è interrotto. uscita spenta. Valuta il materiale prima di iniziare un nuovo ciclo." : ""));
  put("outageInfo", s.interrupted ? (s.outageUpperBoundSec ?
    `Interruzione rilevata. Intervallo massimo dall'ultimo checkpoint al riavvio: ${clock(s.outageUpperBoundSec)}.` :
    "Interruzione rilevata. Durata sconosciuta: NTP non disponibile o nessun checkpoint salvato.") :
    "Nessuna interruzione di un ciclo attivo registrata all'ultimo riavvio.");
  updateControls();
  if (!pidLoaded && s.pid) {
    $("kp").value = Number(s.pid.kp).toFixed(2);
    $("ki").value = Number(s.pid.ki).toFixed(3);
    $("kd").value = Number(s.pid.kd).toFixed(1);
    pidLoaded = true;
  }
  if (!settingsLoaded) {
    $("actuator").value = s.actuator; $("windowSec").value = s.windowSec; $("maxPower").value = s.maxPower;
    $("windowSec").min = s.actuator === "ssr" ? "1" : "60"; settingsLoaded = true;
  }
  if (s.sampleAtMs && Number(s.sampleAtMs) > (live.at(-1)?.ms ?? -1)) {
    live.push({ ms: Number(s.sampleAtMs), t1, t2, actual: avg, target, duty: Number(s.duty), relay: !!s.relay });
    while (live.length > 1 && live[0].ms < Number(s.sampleAtMs) - LIVE_MS) live.shift();
    drawSoon();
  }
}

function updateControls() {
  const active = latest?.phase === "running" || latest?.phase === "paused";
  $("startBtn").disabled = !connected || commandBusy || !latest?.ready || active || latest?.phase === "interrupted" ||
                            !recipesLoaded || recipesDirty || !recipes.length || !$("hardwareReady").checked;
  $("hardwareReady").disabled = active;
  $("recipeSelect").disabled = active || !connected;
  $("globalStop").disabled = !connected;
  for (const id of ["saveRecipes", "savePid", "saveControl"]) $(id).disabled = !connected || active;
  $("exportSamples").disabled = !connected || exportBusy;
  document.querySelectorAll('#recipesView input, #recipesView select, #recipesView button').forEach(el => {
    if (el.id !== "exportRecipes") el.disabled = active || !connected;
  });
  $("pauseBtn").disabled = !connected || commandBusy || latest?.phase !== "running";
  $("resumeBtn").disabled = !connected || commandBusy || latest?.phase !== "paused";
  $("resetBtn").disabled = !connected || commandBusy || !["fault", "interrupted"].includes(latest?.phase);
  $("stopBtn").disabled = !connected;
}
function markDirty() { recipesDirty = true; updateControls(); }
function renderPreview() {
  const list = $("recipePreview"); list.replaceChildren();
  const recipe = recipes.find(r => r.id === selectedId);
  if (!recipe) return;
  for (const step of recipe.steps) {
    const item = document.createElement("li");
    item.textContent = `${types[step.type]} → ${step.target} °C · ${step.type === "hold" ? `${step.duration} min in banda` : `${step.rate} °C/min`}`;
    list.append(item);
  }
  if (recipesDirty) { const item = document.createElement("li"); item.textContent = "Modifiche non salvate: salva sull'ESP prima di avviare."; list.append(item); }
}

function redrawSelectors() {
  for (const [id, value] of [["recipeSelect", selectedId], ["editRecipeSelect", editId]]) {
    const select = $(id); select.replaceChildren();
    for (const r of recipes) { const option = document.createElement("option"); option.value = r.id; option.textContent = r.name; select.append(option); }
    if (recipes.some(r => r.id === value)) select.value = value;
    else if (recipes.length) select.value = recipes[0].id;
  }
  selectedId = $("recipeSelect").value; editId = $("editRecipeSelect").value;
  renderEditor(); renderPreview(); updateControls();
}
async function loadRecipes() {
  if (recipeLoadBusy || recipesDirty) return;
  recipeLoadBusy = true;
  try {
    const data = await api("/api/recipes");
    if (recipesDirty) return;
    recipes = data.recipes; recipesLoaded = true; recipesDirty = false;
    redrawSelectors();
  } finally { recipeLoadBusy = false; }
}
function editRecipe() { return recipes.find(r => r.id === editId); }
function field(parent, label, element) {
  const wrap = document.createElement("label");
  const caption = document.createElement("span"); caption.textContent = label;
  wrap.append(caption, element); parent.append(wrap);
}
function numeric(value, min, max, step, onChange) {
  const el = document.createElement("input"); el.type = "number"; el.min = min;
  if (max !== null) el.max = max;
  el.step = step; el.value = value;
  el.addEventListener("change", () => { onChange(el.value.trim() ? Number(el.value) : NaN); markDirty(); renderPreview(); }); return el;
}
function renderEditor() {
  const recipe = editRecipe(), box = $("stepsEditor"); box.replaceChildren();
  if (!recipe) { $("recipeName").value = ""; return; }
  $("recipeName").value = recipe.name;
  recipe.steps.forEach((step, index) => {
    const row = document.createElement("div"); row.className = "step-row";
    const head = document.createElement("div"); head.className = "step-row-head";
    const title = document.createElement("strong"); title.textContent = `Step ${index + 1}`;
    const remove = document.createElement("button"); remove.className = "step-remove"; remove.textContent = "Rimuovi";
    remove.onclick = () => { recipe.steps.splice(index, 1); markDirty(); renderEditor(); renderPreview(); };
    head.append(title, remove); row.append(head);
    const fields = document.createElement("div"); fields.className = "step-fields";
    const type = document.createElement("select");
    for (const [key, value] of Object.entries(types)) { const option = document.createElement("option"); option.value = key; option.textContent = value; type.append(option); }
    type.value = step.type;
    type.onchange = () => { step.type = type.value; if (step.type === "hold") { delete step.rate; step.duration = 10; } else { delete step.duration; step.rate = 1; } markDirty(); renderEditor(); renderPreview(); };
    field(fields, "Tipo", type);
    field(fields, "Target °C", numeric(step.target, 0, 190, 0.1, value => { step.target = value; }));
    if (step.type === "hold") field(fields, "Durata min", numeric(step.duration, 1, 360, 1, value => { step.duration = value; }));
    else field(fields, "Rate °C/min", numeric(step.rate, 0.1, null, 0.1, value => { step.rate = value; }));
    row.append(fields); box.append(row);
  });
  updateControls();
}
function saveName() { const recipe = editRecipe(); if (recipe && recipe.name !== $("recipeName").value.trim()) { recipe.name = $("recipeName").value.trim(); markDirty(); } }
async function saveRecipes() {
  saveName();
  try { validateRecipes(recipes); const data = await api("/api/recipes", { recipes }); recipes = data.recipes; recipesDirty = false; recipesLoaded = true; redrawSelectors(); note("recipeMessage", "Ricette salvate nella memoria dell'ESP."); }
  catch (e) { note("recipeMessage", e.message, true); }
}
function download(name, data, mime) {
  const url = URL.createObjectURL(new Blob([data], { type: mime }));
  const a = document.createElement("a"); a.href = url; a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function command(action) {
  if (commandBusy && action !== "stop") return;
  const generation = ++commandGeneration; commandBusy = true; updateControls();
  try {
    const result = await api("/api/command", { action, recipeId: selectedId, hardwareReady: $("hardwareReady").checked });
    if (generation !== commandGeneration) { if (action === "start") api("/api/command", {action:"stop"}).catch(() => {}); return; }
    if (["start", "stop", "reset"].includes(action)) $("hardwareReady").checked = false;
    display(result);
    if (action === "start" || action === "stop") { chartAtEnd = true; drawSoon(); }
    if (!$("logsView").hidden) refreshLogs();
    const text = ({ start: "Ciclo avviato.", pause: "Ciclo in pausa: uscita spenta.", resume: "Ciclo ripreso.", stop: "Ciclo fermato: uscita spenta.", reset: "Allarme/interruzione riconosciuti." })[action];
    note("messageBox", text); note("globalMessage", text);
  } catch (e) { if (generation === commandGeneration) { note("messageBox", e.message, true); note("globalMessage", e.message, true); } }
  finally { if (generation === commandGeneration) { commandBusy = false; updateControls(); refresh(); } }
}
function historyPoint(row) {
  const [seq, ms, t1, t2, target, duty, flags, step] = row;
  const a = t1 === null ? NaN : Number(t1), b = t2 === null ? NaN : Number(t2);
  return { seq, ms, t1: a, t2: b, actual: Number.isFinite(a) && Number.isFinite(b) ? (a + b) / 2 : NaN,
           target: target === null ? NaN : Number(target), duty: Number(duty), relay: !!(flags & 4),
           marker: flags & 0xf0, step };
}
async function loadHistory(initial = false) {
  if (historyBusy) return;
  historyBusy = true;
  try {
    const after = history.at(-1)?.seq || 0;
    const data = await api(initial ? "/api/history?tail=1" : `/api/history?after=${after}`);
    if (boot !== null && data.bootCount !== boot) return;
    if (initial) history = [];
    history.push(...data.samples.map(historyPoint).filter(p => initial || p.seq > after));
    history = history.filter(p => p.seq >= data.oldestSeq);
    hasOlder = !!history.length && history[0].seq > data.oldestSeq;
    drawSoon();
  } catch (e) { put("chartRangeLabel", `Storico non disponibile: ${e.message}`); }
  finally { historyBusy = false; }
}
async function loadOlder() {
  if (historyBusy || !hasOlder || !history.length) return false;
  historyBusy = true;
  try {
    const before = history[0].seq, data = await api(`/api/history?before=${before}`);
    if (boot !== null && data.bootCount !== boot) return false;
    const older = data.samples.map(historyPoint).filter(p => p.seq < before);
    history = older.concat(history).filter(p => p.seq >= data.oldestSeq);
    hasOlder = !!history.length && history[0].seq > data.oldestSeq;
    drawSoon(); return older.length > 0;
  } catch (e) { put("chartRangeLabel", `Errore caricamento: ${e.message}`); return false; }
  finally { historyBusy = false; }
}
async function loadAll() { while (connected && hasOlder && $("chartSpan").value === "all") { if (!await loadOlder()) break; await new Promise(r => setTimeout(r, 200)); } }
function drawSoon() { if (chartFrame) return; chartFrame = requestAnimationFrame(() => { chartFrame = 0; drawChart(); }); }
function drawChart() {
  const canvas = $("tempChart"), ctx = canvas.getContext("2d"), W = canvas.width, H = canvas.height;
  const firstLive = live.length ? live[0].ms : Infinity;
  const points = history.filter(p => p.ms < firstLive).concat(live);
  ctx.fillStyle = "#0d141c"; ctx.fillRect(0, 0, W, H);
  if (!points.length) { put("chartRangeLabel", "Nessun campione disponibile."); return; }
  const oldest = points[0].ms, newest = points.at(-1).ms;
  const choice = $("chartSpan").value;
  const span = choice === "all" ? Math.max(60000, newest - oldest) : Number(choice);
  const maxStart = Math.max(oldest, newest - span);
  if (chartAtEnd) chartStart = maxStart;
  else chartStart = Math.max(oldest, Math.min(maxStart, chartStart));
  const end = chartStart + span;
  const visible = points.filter(p => p.ms >= chartStart && p.ms <= end);
  // Markers come from ESP history even while the temperature curve uses denser live points.
  const markers = history.filter(p => p.marker && p.ms >= chartStart && p.ms <= end);
  const vals = visible.flatMap(p => [p.actual, p.target]).filter(Number.isFinite);
  const low = Math.floor((Math.min(...vals, 25) - 2) / 10) * 10;
  const high = Math.max(low + 10, Math.ceil((Math.max(...vals, 35) + 2) / 10) * 10);
  const plot = { l: 62, r: 15, t: 18, b: 38 };
  const x = ms => plot.l + (ms - chartStart) / span * (W - plot.l - plot.r);
  const y = c => plot.t + (H - plot.t - plot.b) * (1 - (c - low) / (high - low));
  ctx.font = "16px system-ui";
  const gridStep = high - low > 80 ? 20 : 10;
  for (let c = low; c <= high; c += gridStep) {
    ctx.strokeStyle = "#20303e"; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(plot.l, y(c)); ctx.lineTo(W - plot.r, y(c)); ctx.stroke();
    ctx.fillStyle = "#8fa2b6"; ctx.fillText(`${c}°`, 8, y(c) + 5);
  }
  for (const [field, color] of [["target", "#4ba3ff"], ["actual", "#36c58c"]]) {
    ctx.strokeStyle = color; ctx.lineWidth = 3; ctx.beginPath(); let started = false, previousMs = -1;
    for (const p of visible) {
      if (!Number.isFinite(p[field])) { started = false; previousMs = p.ms; continue; }
      const newCycle = field === "target" && markers.some(m => m.ms >= previousMs && m.ms <= p.ms);
      if (!started || newCycle) ctx.moveTo(x(p.ms), y(p[field]));
      else ctx.lineTo(x(p.ms), y(p[field]));
      started = true; previousMs = p.ms;
    }
    ctx.stroke();
  }
  // End markers first: a new start stays prominent even when STOP and START are seconds apart.
  for (const p of markers.sort((a, b) => Boolean(a.marker & 16) - Boolean(b.marker & 16))) {
    const start = !!(p.marker & 16);
    const color = start ? "#cc9cff" : p.marker & 128 ? "#ff5e6c" : p.marker & 64 ? "#4ba3ff" : "#f6b94f";
    const label = markerNames[p.marker] || "EVENTO";
    const px = x(p.ms);
    ctx.save();
    ctx.strokeStyle = color; ctx.lineWidth = start ? 4 : 2;
    ctx.setLineDash(start ? [] : [7, 5]);
    ctx.beginPath(); ctx.moveTo(px, plot.t); ctx.lineTo(px, H - plot.b); ctx.stroke();
    ctx.setLineDash([]); ctx.font = "bold 13px system-ui";
    const width = ctx.measureText(label).width + 12;
    const labelX = Math.max(plot.l, Math.min(px + 6, W - plot.r - width));
    const labelY = start ? plot.t + 18 : H - plot.b - 8;
    ctx.fillStyle = "#0d141c"; ctx.fillRect(labelX - 3, labelY - 14, width, 18);
    ctx.fillStyle = color; ctx.fillText(label, labelX + 3, labelY);
    ctx.restore();
  }
  ctx.fillStyle = "#8fa2b6"; ctx.fillText(dateAt(chartStart), plot.l, H - 10);
  const endText = dateAt(Math.min(end, newest)); ctx.fillText(endText, W - 120, H - 10);
  const slider = $("chartPosition"); slider.min = String(oldest); slider.max = String(maxStart); slider.step = "1000"; slider.value = String(chartStart); slider.disabled = maxStart === oldest;
  $("chartBack").disabled = chartStart <= oldest && !hasOlder;
  $("chartForward").disabled = chartStart >= maxStart;
  $("chartNow").disabled = chartAtEnd;
  put("chartRangeLabel", `${dateAt(chartStart)} – ${dateAt(Math.min(end, newest))} · ${visible.length} campioni${hasOlder ? " · dati precedenti disponibili" : ""}`);
}
async function moveChart(dir) {
  const step = ($("chartSpan").value === "all" ? 1800000 : Number($("chartSpan").value)) * 0.75;
  if (dir < 0 && hasOlder && chartStart - step <= (history[0]?.ms || 0)) await loadOlder();
  chartAtEnd = false; chartStart += dir * step; drawChart();
}
async function refreshLogs() {
  if (logsBusy) return; logsBusy = true;
  try {
    const data = await api("/api/logs");
    if (boot !== null && data.bootCount !== boot) return;
    const key = `${boot}:${data.events.length}:${data.events.at(-1)?.seq || 0}`;
    if (key === eventKey) return;
    eventKey = key; events = data.events;
    put("logRetention", `Ultimi ${data.capacity} eventi; i ${data.savedCapacity} eventi principali restano nella memoria ESP dopo il riavvio.`);
    const list = $("logList"); list.replaceChildren();
    for (const e of [...events].reverse()) {
      const row = document.createElement("div"); row.className = "log-row";
      const when = document.createElement("span"); when.className = "log-time";
      when.textContent = e.utc ? new Date(Number(e.utc) * 1000).toLocaleString("it-IT") : `Avvio / evento #${e.seq}`;
      const level = document.createElement("strong"); level.textContent = e.level;
      if (e.level === "ALLARME") level.className = "log-danger";
      if (e.level === "ATTENZIONE") level.className = "log-warning";
      const message = document.createElement("span"); message.textContent = e.message;
      row.append(when, level, message); list.append(row);
    }
  } catch (e) { eventKey = ""; put("logList", `Log non disponibili: ${e.message}`); }
  finally { logsBusy = false; }
}
async function refresh() {
  if (busy || commandBusy) return; busy = true;
  const generation = commandGeneration;
  try {
    const s = await api("/api/status"); if (generation !== commandGeneration) return; display(s);
    if (!recipesLoaded && !recipesDirty) loadRecipes().catch(() => {});
    if (!history.length || s.historyLatestSeq > (history.at(-1)?.seq || 0)) loadHistory(!history.length);
  } catch (e) {
    if (generation !== commandGeneration) return;
    connected = false; $("hardwareReady").checked = false;
    put("statusText", "ESP NON RAGGIUNGIBILE"); $("statusDot").className = "status-dot fault";
    for (const id of ["avgTemp", "targetTemp", "deltaTemp", "t1", "t2", "cycleDuration", "stepDuration", "sampleAge"]) put(id, "—");
    put("relayState", "SCONOSCIUTO"); put("pidInfo", "Stato non aggiornato");
    put("fault1", "Dato non aggiornato"); put("fault2", "Dato non aggiornato");
    updateControls();
  }
  finally { busy = false; }
}
function show(view) {
  for (const name of ["Dashboard", "Recipes", "Logs"]) {
    const active = view === name;
    $(`tab${name}`).classList.toggle("active", active);
    $(`${name.toLowerCase()}View`).hidden = !active;
  }
  if (view === "Logs") refreshLogs();
  if (view === "Dashboard") drawSoon();
}

function validateRecipes(list) {
  if (!Array.isArray(list) || list.length < 1 || list.length > 8) throw Error("Servono 1–8 ricette.");
  const ids = new Set();
  for (const recipe of list) {
    if (!recipe || typeof recipe.id !== "string" || !/^[A-Za-z0-9_-]{1,32}$/.test(recipe.id) || ids.has(recipe.id)) throw Error("ID ricetta assente, duplicato o non valido.");
    ids.add(recipe.id);
    if (typeof recipe.name !== "string" || !recipe.name.trim() || new TextEncoder().encode(recipe.name).length > 48) throw Error("Nome ricetta richiesto, massimo 48 byte.");
    if (!Array.isArray(recipe.steps) || recipe.steps.length < 1 || recipe.steps.length > 12) throw Error("Servono 1–12 step per ricetta.");
    let previous = null;
    for (const step of recipe.steps) {
      if (!step || !["ramp", "hold", "cooldown"].includes(step.type) || !Number.isFinite(step.target) || step.target < 0 || step.target > 190) throw Error("Tipo step o target non valido (0–190 °C).");
      if (step.type === "hold") { if (!Number.isInteger(step.duration) || step.duration < 1 || step.duration > 360) throw Error("Hold: 1–360 minuti interi."); }
      else if (!Number.isFinite(step.rate) || step.rate < 0.1) throw Error("Rate minimo 0,1 °C/min.");
      if (previous !== null && ((step.type === "ramp" && step.target <= previous) || (step.type === "cooldown" && step.target >= previous) || (step.type === "hold" && Math.abs(step.target - previous) > 0.1))) throw Error("Sequenza incoerente: ramp sale, cooldown scende, hold mantiene il target precedente.");
      previous = step.target;
    }
  }
}

$("tabDashboard").onclick = () => show("Dashboard");
$("tabRecipes").onclick = () => show("Recipes");
$("tabLogs").onclick = () => show("Logs");
$("startBtn").onclick = () => command("start");
$("pauseBtn").onclick = () => command("pause");
$("resumeBtn").onclick = () => command("resume");
$("stopBtn").onclick = () => command("stop");
$("globalStop").onclick = () => command("stop");
$("hardwareReady").onchange = updateControls;
$("resetBtn").onclick = () => command("reset");
$("recipeSelect").onchange = e => { selectedId = e.target.value; renderPreview(); };
$("editRecipeSelect").onchange = e => { saveName(); editId = e.target.value; renderEditor(); };
$("recipeName").onchange = saveName;
$("addStep").onclick = () => { const r = editRecipe(); if (!r) return; if (r.steps.length >= 12) return note("recipeMessage", "Massimo 12 step.", true); r.steps.push({ type: "hold", target: r.steps.at(-1)?.target ?? 50, duration: 10 }); markDirty(); renderEditor(); renderPreview(); };
$("addRecipe").onclick = () => {
  if (recipes.length >= 8) return note("recipeMessage", "Massimo 8 ricette.", true);
  saveName(); const id = `ricetta-${Date.now().toString(36)}`;
  recipes.push({ id, name: "Nuova ricetta", steps: [{ type: "ramp", target: 50, rate: 1 }, { type: "hold", target: 50, duration: 10 }] });
  editId = selectedId = id; markDirty(); redrawSelectors();
};
$("deleteRecipe").onclick = () => { if (recipes.length <= 1) return note("recipeMessage", "Deve restare almeno una ricetta.", true); recipes = recipes.filter(r => r.id !== editId); editId = selectedId = recipes[0].id; markDirty(); redrawSelectors(); };
$("saveRecipes").onclick = saveRecipes;
$("savePid").onclick = async () => {
  try {
    if (["kp", "ki", "kd"].some(id => $(id).value.trim() === "")) throw Error("Inserisci Kp, Ki e Kd.");
    const result = await api("/api/pid", { kp: Number($("kp").value), ki: Number($("ki").value), kd: Number($("kd").value) });
    display(result); note("recipeMessage", "Coefficienti PID salvati nella memoria dell'ESP.");
  } catch (e) { note("recipeMessage", e.message, true); }
};
$("actuator").onchange = () => {
  const minimum = $("actuator").value === "ssr" ? 1 : 60;
  $("windowSec").min = String(minimum);
  if (Number($("windowSec").value) < minimum) $("windowSec").value = minimum;
};
$("saveControl").onclick = async () => {
  try {
    if (["windowSec", "maxPower"].some(id => !$(id).value.trim())) throw Error("Inserisci finestra e limite potenza.");
    const result = await api("/api/control", {actuator: $("actuator").value, windowSec: Number($("windowSec").value), maxPower: Number($("maxPower").value)});
    display(result); note("recipeMessage", "Attuatore e limite comando salvati nella memoria dell'ESP.");
  } catch (e) { note("recipeMessage", e.message, true); }
};
$("exportRecipes").onclick = () => { saveName(); download("oven-full-recipes.json", JSON.stringify(recipes, null, 2), "application/json"); };
$("importRecipes").onchange = async e => {
  const file = e.target.files[0]; if (!file) return;
  try { const parsed = JSON.parse(await file.text()); validateRecipes(parsed); recipes = parsed; markDirty(); editId = selectedId = recipes[0]?.id || ""; redrawSelectors(); note("recipeMessage", "Ricette importate. Controlla gli step e premi Salva sull'ESP."); }
  catch (err) { note("recipeMessage", err.message, true); }
  e.target.value = "";
};
$("refreshLogs").onclick = refreshLogs;
$("exportLogs").onclick = async () => {
  try { const data = await api("/api/logs"); download("oven-full-logs.json", JSON.stringify(data, null, 2), "application/json"); }
  catch (e) { note("globalMessage", e.message, true); }
};
$("exportSamples").onclick = async () => {
  if (exportBusy) return; exportBusy = true; updateControls();
  const exportBoot = boot, exportOffset = timeOffset;
  try {
    let points = [], after = 0, latestSeq = null;
    do {
      const data = await api(`/api/history?after=${after}`);
      if (data.bootCount !== exportBoot) throw Error("ESP riavviato durante esportazione: riprova.");
      if (latestSeq === null) latestSeq = data.latestSeq;
      const page = data.samples.filter(row => row[0] <= latestSeq);
      points.push(...page.map(historyPoint));
      if (!page.length) break;
      after = page.at(-1)[0];
      if (after >= latestSeq || !data.more) break;
    } while (true);
    if (!points.length) throw Error("Nessun campione disponibile.");
    const rows = ["boot,seq,uptime_ms,ora_stimata_iso,sonda1_c,sonda2_c,media_c,setpoint_c,pid_percento,comando_uscita,step,evento"];
    for (const p of points) rows.push([exportBoot, p.seq, p.ms, new Date(exportOffset + p.ms).toISOString(), Number.isFinite(p.t1) ? p.t1.toFixed(2) : "", Number.isFinite(p.t2) ? p.t2.toFixed(2) : "", Number.isFinite(p.actual) ? p.actual.toFixed(2) : "", Number.isFinite(p.target) ? p.target.toFixed(2) : "", p.duty.toFixed(1), p.relay ? 1 : 0, p.marker & 0xe0 ? "" : p.step + 1, markerNames[p.marker] || ""].join(","));
    download("oven-full-history.csv", rows.join("\n") + "\n", "text/csv");
    note("globalMessage", `CSV completo esportato: ${points.length} campioni.`);
  } catch (e) { note("globalMessage", e.message, true); }
  finally { exportBusy = false; updateControls(); }
};
$("chartBack").onclick = () => moveChart(-1);
$("chartForward").onclick = () => moveChart(1);
$("chartNow").onclick = () => { chartAtEnd = true; drawChart(); };
$("chartPosition").oninput = e => { chartStart = Number(e.target.value); chartAtEnd = chartStart >= Number(e.target.max) - 1000; drawSoon(); };
$("chartSpan").onchange = () => { if ($("chartSpan").value === "all") loadAll(); drawChart(); };
updateControls(); drawChart(); loadRecipes().catch(e => note("messageBox", `Ricette non disponibili: ${e.message}`, true)); refresh();
setInterval(refresh, 500);
setInterval(() => { if (!$("logsView").hidden) refreshLogs(); }, 5000);
)OVEN_ASSET_2026";
