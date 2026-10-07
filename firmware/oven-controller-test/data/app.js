const $ = id => document.getElementById(id);
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
