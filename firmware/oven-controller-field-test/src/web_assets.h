#pragma once
#include <Arduino.h>
// Generated from data/ by scripts/embed_web.py.

const char WEB_HTML[] PROGMEM = R"OVEN_ASSET_2026(<!DOCTYPE html>
<html lang="it">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Oven Controller · Test sul campo</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <header class="topbar">
    <div><div class="eyebrow">ESP32-S3 · COLLAUDO SENZA RESISTENZE</div><h1>Oven Controller</h1></div>
    <div class="status-wrap"><span id="statusDot" class="status-dot idle"></span><span id="statusText">Connessione…</span></div>
  </header>
  <div class="field-banner">TEST SUL CAMPO · Resistenze reali scollegate. Le indicazioni delle uscite sono i comandi GPIO: verifica il circuito con uno strumento.</div>
  <nav class="view-tabs" aria-label="Sezioni">
    <button id="tabDashboard" class="active" type="button" aria-selected="true">Dashboard</button>
    <button id="tabLogs" type="button" aria-selected="false">Log</button>
  </nav>
  <main id="dashboardView" class="layout">
    <section class="panel dashboard-panel">
      <div class="panel-title-row"><div><h2>PT100 reali e circuito</h2><p>Letture continue · 3 fili · Filtro 50 Hz</p></div><div class="clock-box"><span>Tempo acceso</span><strong id="uptime">—</strong></div></div>
      <div class="hero-grid">
        <article class="metric hero"><span>PT100 #1 · CS 14</span><strong><span id="t1">—</span><small> °C</small></strong><small id="sensorState1">In attesa</small></article>
        <article class="metric hero"><span>PT100 #2 · CS 10</span><strong><span id="t2">—</span><small> °C</small></strong><small id="sensorState2">In attesa</small></article>
        <article class="metric"><span>ΔT sonde</span><strong><span id="deltaTemp">—</span><small> °C</small></strong><small>Confronto diagnostico</small></article>
        <article class="metric"><span>Comando relè · GPIO4</span><strong id="relayState">—</strong><small id="relayHint">Nessun feedback elettrico</small></article>
      </div>
      <div class="sensor-grid two">
        <article class="sensor-card"><span>Diagnostica PT100 #1</span><strong id="ohms1">— Ω</strong><small id="raw1">RAW — · RREF —</small><p id="fault1" class="hint">—</p></article>
        <article class="sensor-card"><span>Diagnostica PT100 #2</span><strong id="ohms2">— Ω</strong><small id="raw2">RAW — · RREF —</small><p id="fault2" class="hint">—</p></article>
      </div>
      <div class="chart-card">
        <div class="chart-head"><div><h3>Temperature reali</h3><small>Ultimi 5 minuti · Storico ESP fino a circa 1 ora</small></div><div class="legend"><span><i class="legend-line actual-line"></i>PT100 #1</span><span><i class="legend-line target-line"></i>PT100 #2</span></div><button id="exportSamples" class="ghost" disabled>Esporta CSV completo</button></div>
        <canvas id="tempChart" width="1200" height="420" aria-label="Grafico delle due temperature reali"></canvas>
        <p id="chartRangeLabel" class="chart-range-label">In attesa di letture…</p>
        <p class="hint">Il CSV include temperature, RAW, fault e comandi GPIO campionati ogni circa 1 s. Gli impulsi più brevi sono nel log. Storico e log in RAM si cancellano al riavvio.</p>
      </div>
      <hr>
      <h2>Verifiche sul campo</h2>
      <p class="hint">Questa lista raccoglie le tue verifiche visive e strumentali. Il firmware non misura alimentazioni, contatti del relè o corrente del carico.</p>
      <div class="checklist">
        <label><input type="checkbox"> Alimentazione 3,3 V delle Click e massa comune verificate con multimetro.</label>
        <label><input type="checkbox"> PT100 #1 reagisce scaldandola con la mano; #2 resta coerente.</label>
        <label><input type="checkbox"> PT100 #2 reagisce scaldandola con la mano; #1 resta coerente.</label>
        <label><input type="checkbox"> Verde e rosso identificati con le prove separate.</label>
        <label><input type="checkbox"> GPIO4, transistor e comando del relè verificati durante un impulso.</label>
        <label><input type="checkbox"> Uscita realmente inattiva dopo STOP e riavvio della scheda.</label>
      </div>
      <p class="hint">Per provare un fault: disalimenta prima il circuito, scollega una sonda, riaccendi e verifica il messaggio. Ricollegala a circuito disalimentato. Per il driver SSR usa le misure adatte al tuo circuito; la continuità da sola non ne verifica il funzionamento.</p>
    </section>
    <aside class="panel control-panel">
      <h2>Comandi di collaudo</h2>
      <p>Ogni prova è temporizzata. Nessun PID e nessuna ricetta. Puoi testare relè e LED anche con sonde assenti o in fault.</p>
      <label class="confirm-loads"><input id="loadsDisconnected" type="checkbox"> Le resistenze reali del forno sono fisicamente scollegate.</label>
      <button id="armBtn" class="primary full" disabled>ABILITA TEST PER 60 s</button>
      <p id="armState" class="hint">Comandi disabilitati · Solo lettura sonde</p>
      <button id="stopBtn" class="danger full" disabled>STOP · RELÈ SPENTO</button>
      <div id="messageBox" class="notice hidden" role="status" aria-live="polite"></div>
      <div class="test-progress"><strong id="testName">Lettura sonde</strong><span id="testRemaining">—</span></div>
      <hr>
      <h2>Test LED</h2>
      <div class="led-indicators"><span><i id="greenIndicator" class="physical-led"></i>Verde · GPIO7</span><span><i id="redIndicator" class="physical-led"></i>Rosso · GPIO6</span></div>
      <div class="buttons led-buttons">
        <button data-action="ledGreen" disabled>Verde · 3 s</button><button data-action="ledRed" disabled>Rosso · 3 s</button>
        <button data-action="ledBoth" disabled>Entrambi · 3 s</button><button data-action="ledOff" disabled>Spenti · 3 s</button>
      </div>
      <button data-action="ledSequence" class="ghost full" disabled>Sequenza LED · 8 s</button>
      <p class="hint">Sequenza: spenti → verde → rosso → entrambi, 2 s ciascuno. GPIO4 resta spento. A riposo verde acceso = dashboard avviata; rosso segue il relè. Durante il test LED il rosso è indipendente dal relè.</p>
      <hr>
      <h2>Test comando relè</h2>
      <label class="field"><span>Durata impulso</span><select id="pulseDuration"><option value="100">0,1 s</option><option value="500">0,5 s</option><option value="1000" selected>1 s</option><option value="2000">2 s</option><option value="5000">5 s</option></select></label>
      <div class="buttons relay-buttons"><button data-action="relayPulse" class="primary" disabled>Invia impulso</button><button data-action="relaySequence" disabled>3 impulsi da 1 s</button></div>
      <p class="hint">Sequenza relè: 1 s ON / 2 s OFF, per 3 volte. Rosso segue il comando. Se manca l'heartbeat della dashboard per 2,5 s, il supervisore spegne il relè e disabilita i test. Cambiare scheda del browser o bloccare il telefono ferma le prove.</p>
      <hr>
      <h2>Rete e scheda</h2>
      <div class="network-box"><div class="network-line"><span>Wi-Fi capannone</span><strong id="workshopState">—</strong></div><a id="workshopAddress" class="network-address" hidden></a><a id="localAddress" class="network-address" hidden></a><div class="network-line"><span>Rete diretta</span><strong>OvenController-FieldTest</strong></div><span id="apAddress" class="network-address">—</span></div>
      <div class="recipe-summary power-summary"><div><span>Avvii</span><strong id="bootCount">—</strong></div><div><span>Reset codice</span><strong id="resetReason">—</strong></div><div><span>Chip ESP</span><strong id="chipTemperature">—</strong></div><div><span>RAM libera / minimo</span><strong id="heap">—</strong></div><div><span>Segnale Wi-Fi</span><strong id="rssi">—</strong></div><div><span>Test al reset</span><strong id="interrupted">—</strong></div></div>
      <p class="hint">La temperatura del chip ESP non è quella del regolatore o dell'aria. Nessuna uscita riparte automaticamente dopo un reset. Nessun login: usa una rete di prova controllata.</p>
    </aside>
  </main>
  <section id="logsView" class="logs-view panel" hidden>
    <div class="panel-title-row"><div><div class="eyebrow">DIAGNOSTICA</div><h2>Log del collaudo</h2><p>Avvio, sonde, test, GPIO e arresti · Ultimi 128 eventi di questa accensione</p></div><div class="log-actions"><button id="refreshLogs" class="ghost" disabled>Aggiorna</button><button id="exportLogs" class="ghost" disabled>Esporta JSON</button></div></div>
    <div id="logList" class="log-list">In attesa del controllore…</div>
  </section>
  <footer>Firmware di collaudo · ESP32-S3 N16R8 + 2 MAX31865 · Resistenze del forno scollegate</footer>
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

.field-banner { max-width: 1532px; margin: 0 auto 18px; padding: 14px 18px; border: 1px solid rgba(246,185,79,.4); border-radius: 12px; background: rgba(246,185,79,.1); color: #ffd68a; font-size: 13px; line-height: 1.5; }
.confirm-loads, .checklist label { display: flex; gap: 10px; align-items: flex-start; font-size: 13px; line-height: 1.5; }
.confirm-loads { margin-top: 16px; padding: 12px; border: 1px solid var(--border); border-radius: 12px; }
.confirm-loads input, .checklist input { width: 18px; height: 18px; flex-shrink: 0; margin: 2px 0 0; accent-color: var(--accent); }
.checklist { display: grid; gap: 13px; margin-top: 18px; }
.test-progress { display: flex; justify-content: space-between; gap: 12px; margin-top: 16px; padding: 12px; background: #0d141c; border-radius: 12px; font-size: 13px; }
.led-indicators { display: flex; gap: 16px; margin-top: 12px; font-size: 12px; color: var(--muted); }
.physical-led { display: inline-block; width: 12px; height: 12px; margin-right: 6px; border-radius: 50%; background: #3e4b59; vertical-align: middle; }
.physical-led.on.green { background: var(--accent); box-shadow: 0 0 8px rgba(54,197,140,.5); }
.physical-led.on.red { background: var(--danger); box-shadow: 0 0 8px rgba(255,94,108,.5); }
.physical-led.unknown { border: 1px dashed var(--warning); background: transparent; }
.led-buttons, .relay-buttons { grid-template-columns: repeat(2, 1fr); }
.sensor-ok { color: var(--accent); }
.sensor-warning { color: var(--warning); }
.control-panel > p { line-height: 1.55; font-size: 13px; }
.sensor-card small { display: block; margin-top: 6px; color: var(--muted); }
.field-banner + .view-tabs { margin-bottom: 18px; }
button:focus-visible, a:focus-visible, input:focus-visible, select:focus-visible { outline: 2px solid var(--accent-2); outline-offset: 3px; }
@media (max-width: 1600px) { .field-banner { margin-left: 34px; margin-right: 34px; } }

@media (max-width: 1100px) {
  .layout { grid-template-columns: 1fr; }
  .control-panel { position: static; }
  .hero-grid { grid-template-columns: repeat(2, 1fr); }
}

@media (max-width: 680px) {
  .field-banner { margin-left: 16px; margin-right: 16px; }
  .led-buttons, .relay-buttons { grid-template-columns: repeat(2, 1fr); }
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

const char WEB_JS[] PROGMEM = R"OVEN_ASSET_2026('use strict';
const $ = id => document.getElementById(id);
let token = 0, online = false, state = null, boot = null, busy = false;
let samples = [], historySeq = 0, historyBusy = false, historyGeneration = 0;
let logs = [], lastHistoryPoll = 0, lastLogPoll = 0, lastHeartbeat = 0;
let heartbeatBusy = false, commandBusy = false, logsBusy = false;
let commandGeneration = 0;
const fmt = (value, digits = 1) => Number.isFinite(value) ? value.toFixed(digits) : '—';
const duration = ms => {
  const s = Math.floor(ms / 1000);
  return `${String(Math.floor(s / 3600)).padStart(2, '0')}:${String(Math.floor(s / 60) % 60).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
};
async function api(path, body) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 1800);
  try {
    const response = await fetch(path, {
      method: body === undefined ? 'GET' : 'POST', cache: 'no-store', signal: controller.signal,
      headers: body === undefined ? {} : {'Content-Type': 'application/json'},
      body: body === undefined ? undefined : JSON.stringify(body)
    });
    if (response.status === 204) return null;
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
    return data;
  } finally { clearTimeout(timeout); }
}
function message(text, error = false) {
  $('messageBox').textContent = text;
  $('messageBox').className = `notice${error ? ' error' : ''}`;
}
function setControls() {
  const owned = online && !!token && !!state?.armed;
  $('armBtn').disabled = !online || commandBusy || !!state?.armed || !$('loadsDisconnected').checked;
  $('loadsDisconnected').disabled = !!state?.armed;
  $('stopBtn').disabled = !online;
  $('pulseDuration').disabled = !!state?.running;
  document.querySelectorAll('[data-action]').forEach(button => {
    button.disabled = !owned || commandBusy || !!state?.running;
  });
  $('exportSamples').disabled = !online || historyBusy;
  $('refreshLogs').disabled = !online || logsBusy;
  $('exportLogs').disabled = !online || logsBusy;
}
function clearOwnership() { token = 0; $('loadsDisconnected').checked = false; }
function setOffline() {
  online = false; clearOwnership();
  $('statusText').textContent = 'Scollegato'; $('statusDot').className = 'status-dot fault';
  $('armState').textContent = 'Collegamento perso · Riabilita i test dopo la riconnessione';
  $('relayState').textContent = 'SCONOSCIUTO'; $('relayHint').textContent = 'Ultimo stato non aggiornato';
  $('greenIndicator').className = $('redIndicator').className = 'physical-led unknown';
  for (let i = 1; i <= 2; i++) {
    $(`t${i}`).textContent = '—'; $(`sensorState${i}`).textContent = 'Dato non aggiornato';
    $(`ohms${i}`).textContent = '— Ω'; $(`raw${i}`).textContent = 'Dato non aggiornato';
    $(`fault${i}`).textContent = 'Connessione persa';
  }
  $('deltaTemp').textContent = '—'; $('testName').textContent = 'Collegamento perso'; $('testRemaining').textContent = '—';
  setControls();
}
function renderStatus(data) {
  if (boot !== null && boot !== data.bootCount) {
    clearOwnership(); samples = []; historySeq = 0; historyGeneration++;
    logs = []; $('logList').textContent = 'Scheda riavviata · Caricamento log…';
    message('Scheda riavviata: relè spento e comandi da riabilitare.');
  }
  boot = data.bootCount; state = data; online = true;
  if (!data.armed && token) clearOwnership();
  $('statusText').textContent = data.running ? 'Test in corso' : 'Connesso';
  $('statusDot').className = `status-dot ${data.running ? 'paused' : 'running'}`;
  $('uptime').textContent = duration(data.uptimeMs);
  $('relayState').textContent = data.relay ? 'ATTIVO' : 'SPENTO';
  $('relayState').className = data.relay ? 'heating' : '';
  $('relayHint').textContent = 'Comando GPIO · Verifica il driver';
  $('greenIndicator').className = `physical-led${data.green ? ' on green' : ''}`;
  $('redIndicator').className = `physical-led${data.red ? ' on red' : ''}`;
  data.sensors.forEach((sensor, index) => {
    const i = index + 1;
    $(`t${i}`).textContent = fmt(sensor.c);
    $(`sensorState${i}`).textContent = sensor.valid ? 'Lettura valida' : 'Sonda / SPI da verificare';
    $(`sensorState${i}`).className = sensor.valid ? 'sensor-ok' : 'sensor-warning';
    $(`ohms${i}`).textContent = `${fmt(sensor.ohms, 2)} Ω`;
    $(`raw${i}`).textContent = `RAW ${sensor.raw} · RREF ${sensor.rref} Ω`;
    $(`fault${i}`).textContent = `Fault 0x${sensor.faultCode.toString(16).padStart(2, '0').toUpperCase()} · ${sensor.message}`;
  });
  $('deltaTemp').textContent = data.sensors.every(s => s.valid) ? fmt(Math.abs(data.sensors[0].c - data.sensors[1].c)) : '—';
  $('armState').textContent = data.armed ?
    `${token ? 'Test abilitati su questa pagina' : 'Test abilitati da altra pagina'} · ${Math.ceil(data.armRemainingMs / 1000)} s residui` :
    'Comandi disabilitati · Solo lettura sonde';
  $('testName').textContent = data.test;
  $('testRemaining').textContent = data.running ? `${(data.remainingMs / 1000).toFixed(1)} s` : '—';
  $('workshopState').textContent = data.workshopConnected ? 'Connesso' : data.workshopConfigured ? 'In connessione' : 'Non configurato';
  const address = $('workshopAddress'); address.hidden = !data.stationIp;
  if (data.stationIp) address.href = address.textContent = `http://${data.stationIp}/`;
  const local = $('localAddress'); local.hidden = !data.localName;
  if (data.localName) local.href = local.textContent = `http://${data.localName}/`;
  $('apAddress').textContent = data.apIp ? `http://${data.apIp}/ · Password oven12345` : 'Access point non disponibile';
  $('bootCount').textContent = data.bootCount; $('resetReason').textContent = data.resetReason;
  $('chipTemperature').textContent = `${fmt(data.chipTemperatureC)} °C`;
  $('heap').textContent = `${Math.round(data.freeHeap / 1024)} / ${Math.round(data.minFreeHeap / 1024)} kB`;
  $('rssi').textContent = data.workshopConnected ? `${data.rssi} dBm` : '—';
  $('interrupted').textContent = data.interrupted ? 'Interrotto · Uscita OFF' : 'Nessuno';
  setControls();
}
async function command(action) {
  if (commandBusy && action !== 'stop') return;
  const generation = ++commandGeneration;
  commandBusy = true; setControls();
  try {
    const body = {action, token};
    if (action === 'arm') body.loadsDisconnected = $('loadsDisconnected').checked;
    if (action === 'relayPulse') body.durationMs = Number($('pulseDuration').value);
    if (action === 'stop') clearOwnership();
    const data = await api('/api/command', body);
    if (generation !== commandGeneration) {
      if (action === 'arm') void api('/api/command', {action: 'stop'}).catch(() => {});
      return;
    }
    if (action === 'arm') {
      token = data.token;
      // Take a fresh status before enabling buttons. Start the lease immediately.
      lastHeartbeat = performance.now(); await api('/api/heartbeat', {token});
      const status = await api('/api/status');
      if (generation !== commandGeneration) return;
      renderStatus(status);
      message('Test abilitati per 60 s. Mantieni questa pagina visibile.');
    } else { renderStatus(data); message(action === 'stop' ? 'STOP ricevuto: relè spento e test disabilitati.' : 'Prova avviata: osserva il circuito.'); }
    lastLogPoll = 0;
  } catch (error) {
    if (generation !== commandGeneration) return;
    message(error.name === 'AbortError' ? 'Risposta assente: verifica la connessione. Il supervisore arresta il test senza heartbeat.' : error.message, true);
    if (action === 'arm') clearOwnership();
  } finally { if (generation === commandGeneration) { commandBusy = false; setControls(); } }
}
async function heartbeat() {
  if (!token || heartbeatBusy || document.hidden) return;
  heartbeatBusy = true; lastHeartbeat = performance.now();
  const requestToken = token;
  try { await api('/api/heartbeat', {token: requestToken}); }
  catch (error) { if (token === requestToken) { clearOwnership(); message('Abilitazione persa: verifica il collegamento e riabilita i test.', true); setControls(); } }
  finally { heartbeatBusy = false; }
}
async function poll() {
  if (busy || commandBusy || document.hidden) return;
  busy = true;
  const generation = commandGeneration;
  try { const data = await api('/api/status'); if (generation === commandGeneration) renderStatus(data); }
  catch (error) { if (generation === commandGeneration) setOffline(); }
  finally { busy = false; }
  if (online && performance.now() - lastHistoryPoll > 1500) void loadHistory();
  if (online && performance.now() - lastLogPoll > 3000) void loadLogs();
}
async function loadHistory() {
  if (historyBusy) return;
  historyBusy = true; lastHistoryPoll = performance.now();
  const generation = historyGeneration;
  try {
    const data = await api(historySeq ? `/api/history?after=${historySeq}` : '/api/history?tail=1');
    if (generation !== historyGeneration || data.bootCount !== boot) return;
    samples = samples.concat(data.samples).filter((row, i, all) => !i || row[0] !== all[i - 1][0]).slice(-600);
    if (data.samples.length) historySeq = data.samples[data.samples.length - 1][0];
    if (data.more) lastHistoryPoll = 0;
    drawChart();
  } catch (error) { $('chartRangeLabel').textContent = 'Storico temporaneamente non disponibile'; }
  finally { historyBusy = false; setControls(); }
}
async function loadLogs() {
  if (logsBusy) return;
  logsBusy = true; lastLogPoll = performance.now(); setControls();
  const generation = historyGeneration;
  try {
    const data = await api('/api/logs');
    if (generation !== historyGeneration || data.bootCount !== boot) return;
    logs = data.events;
    $('logList').replaceChildren();
    for (const event of logs.slice().reverse()) {
      const row = document.createElement('div'); row.className = 'log-row';
      for (const [value, className] of [[duration(event.atMs), 'log-time'], [event.level, event.level === 'STOP' ? 'log-warning' : ''], [event.message, '']]) {
        const item = document.createElement('span'); item.textContent = value; item.className = className; row.append(item);
      }
      $('logList').append(row);
    }
    if (!logs.length) $('logList').textContent = 'Nessun evento disponibile.';
  } catch (error) { $('logList').textContent = 'Impossibile aggiornare il log.'; }
  finally { logsBusy = false; setControls(); }
}
function drawChart() {
  const canvas = $('tempChart'), ctx = canvas.getContext('2d');
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(300, canvas.clientWidth), height = canvas.clientHeight || 280;
  canvas.width = Math.round(width * ratio); canvas.height = Math.round(height * ratio); ctx.scale(ratio, ratio);
  ctx.clearRect(0, 0, width, height);
  const end = samples.length ? samples[samples.length - 1][1] : 300000;
  const start = Math.max(0, end - 300000), visible = samples.filter(s => s[1] >= start);
  const values = visible.flatMap(s => [s[2], s[3]]).filter(Number.isFinite);
  const low = values.length ? Math.floor(Math.min(...values) - 1) : 15;
  const high = values.length ? Math.max(low + 5, Math.ceil(Math.max(...values) + 1)) : 35;
  const left = 48, right = width - 12, top = 16, bottom = height - 35;
  const x = ms => left + (ms - start) / Math.max(1, end - start) * (right - left);
  const y = c => bottom - (c - low) / (high - low) * (bottom - top);
  ctx.font = '11px system-ui'; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const value = low + (high - low) * i / 4;
    ctx.strokeStyle = '#243140'; ctx.beginPath(); ctx.moveTo(left, y(value)); ctx.lineTo(right, y(value)); ctx.stroke();
    ctx.fillStyle = '#8fa2b6'; ctx.fillText(`${value.toFixed(1)}°`, 0, y(value) + 4);
    const ms = start + (end - start) * i / 4;
    ctx.fillText(duration(ms).slice(3), x(ms) - 15, height - 9);
  }
  [2, 3].forEach((col, i) => {
    ctx.strokeStyle = i ? '#4ba3ff' : '#36c58c'; ctx.lineWidth = 2; ctx.beginPath();
    let previous = null;
    for (const row of visible) {
      if (!Number.isFinite(row[col])) { previous = null; continue; }
      if (!previous || row[1] - previous[1] > 2500) ctx.moveTo(x(row[1]), y(row[col]));
      else ctx.lineTo(x(row[1]), y(row[col]));
      previous = row;
    }
    ctx.stroke();
  });
  if (!values.length) { ctx.fillStyle = '#8fa2b6'; ctx.fillText('In attesa di letture PT100 valide', left + 10, top + 30); }
  $('chartRangeLabel').textContent = `${duration(start)} – ${duration(end)} dall'avvio · ${visible.length} campioni`;
}
function download(filename, content, type) {
  const url = URL.createObjectURL(new Blob([content], {type}));
  const a = document.createElement('a'); a.href = url; a.download = filename; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function exportCsv() {
  if (historyBusy) return;
  historyBusy = true; setControls();
  const exportBoot = boot;
  try {
    let rows = [], after = 0, endSeq = null;
    do {
      const data = await api(`/api/history?after=${after}`);
      if (data.bootCount !== exportBoot) throw new Error('Scheda riavviata durante esportazione: riprova.');
      if (endSeq === null) endSeq = data.latestSeq;
      const page = data.samples.filter(row => row[0] <= endSeq);
      rows = rows.concat(page);
      if (!page.length) break;
      after = page[page.length - 1][0];
      if (after >= endSeq || !data.more) break;
    } while (true);
    const header = 'boot,seq,uptime_ms,pt100_1_c,pt100_2_c,raw1,raw2,fault1,fault2,rele_comando,led_rosso,led_verde';
    const csv = rows.map(row => {
      const flags = row[8];
      return [exportBoot, ...row.slice(0, 8).map(v => v === null ? '' : v), flags & 4 ? 1 : 0, flags & 8 ? 1 : 0, flags & 16 ? 1 : 0].join(',');
    });
    download(`collaudo-pt100-avvio-${exportBoot}.csv`, `${header}\n${csv.join('\n')}\n`, 'text/csv;charset=utf-8');
    message(`CSV esportato: ${rows.length} campioni di questa accensione.`);
  } catch (error) { message(error.message, true); }
  finally { historyBusy = false; setControls(); }
}
function stopOnHide() {
  if (!token) return;
  commandGeneration++; commandBusy = false; clearOwnership(); setControls();
  // Best effort immediate stop. The firmware lease also expires independently.
  const body = new Blob([JSON.stringify({action: 'stop'})], {type: 'text/plain'});
  navigator.sendBeacon('/api/command', body);
}
$('loadsDisconnected').addEventListener('change', setControls);
$('armBtn').addEventListener('click', () => void command('arm'));
$('stopBtn').addEventListener('click', () => void command('stop'));
document.querySelectorAll('[data-action]').forEach(b => b.addEventListener('click', () => void command(b.dataset.action)));
$('exportSamples').addEventListener('click', () => void exportCsv());
$('refreshLogs').addEventListener('click', () => void loadLogs());
$('exportLogs').addEventListener('click', async () => {
  const exportBoot = boot;
  try {
    const data = await api('/api/logs');
    if (data.bootCount !== exportBoot) throw new Error('Scheda riavviata: riprova esportazione.');
    download(`collaudo-log-avvio-${exportBoot}.json`, JSON.stringify(data, null, 2), 'application/json');
  } catch (error) { message(error.message, true); }
});
function showTab(showLogs) {
  $('dashboardView').hidden = showLogs; $('logsView').hidden = !showLogs;
  $('tabDashboard').classList.toggle('active', !showLogs); $('tabLogs').classList.toggle('active', showLogs);
  $('tabDashboard').setAttribute('aria-selected', String(!showLogs)); $('tabLogs').setAttribute('aria-selected', String(showLogs));
  if (showLogs) void loadLogs(); else drawChart();
}
$('tabDashboard').addEventListener('click', () => showTab(false));
$('tabLogs').addEventListener('click', () => showTab(true));
document.addEventListener('visibilitychange', () => { if (document.hidden) stopOnHide(); else void poll(); });
window.addEventListener('pagehide', stopOnHide);
window.addEventListener('resize', drawChart);
setInterval(() => { if (performance.now() - lastHeartbeat >= 650) void heartbeat(); }, 100);
setInterval(() => void poll(), 500);
drawChart(); void poll();
)OVEN_ASSET_2026";
