const $ = id => document.getElementById(id);
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
let cycleAnchor = null, cycleKey = null, lastResponseAt = null, renderFailed = false;
let readBusy = false, readQueue = [];
let renderTestStatus = () => {}, testOffline = () => {}, cancelTestOwnership = () => {};
let testCommandBusy = false;
let lastReport = null, reportBusy = false, reportExportBusy = false;
const LIVE_MS = 120000;
const REPORT_DEFINITIONS = $('reportDefinitions').textContent;

function note(id, text, isError = false) {
  const box = $(id); box.textContent = text; box.className = isError ? "notice error" : "notice";
  setTimeout(() => { if (box.textContent === text) box.classList.add("hidden"); }, 7000);
}
// WebServer serves one client at a time. Queue reads and give status priority;
// STOP and test heartbeats bypass the queue. Timeouts start when a request is sent.
function api(path, body, timeoutMs = 2500) {
  if (body !== undefined) return fetchApi(path, body, timeoutMs);
  return new Promise((resolve, reject) => {
    const request = {path, timeoutMs, resolve, reject};
    if (path === '/api/status') readQueue.unshift(request); else readQueue.push(request);
    pumpReads();
  });
}
function pumpReads() {
  if (readBusy || !readQueue.length) return;
  readBusy = true;
  const request = readQueue.shift();
  fetchApi(request.path, undefined, request.timeoutMs).then(request.resolve, request.reject)
    .finally(() => { readBusy = false; pumpReads(); });
}
async function fetchApi(path, body, timeoutMs = 2500) {
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(path, body === undefined ? { cache: "no-store", signal: controller.signal } : {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body), signal: controller.signal
    });
    if (response.status === 204) return null;
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
function remainingStepSeconds(s) {
  if (!['running', 'paused'].includes(s.phase)) return 0;
  if ('stepRemainingSec' in s) return Number.isFinite(s.stepRemainingSec) ? s.stepRemainingSec : null;
  // Older local preview APIs: use qualifying hold time or a rate-based estimate.
  if (s.stepType === 'hold') return Math.max(0, (s.holdRequiredSec || 0) - (s.holdInBandSec || 0));
  const readings = s.sensors.filter(p => p.valid && Number.isFinite(p.c)).map(p => p.c);
  if (readings.length !== 2 || !(s.stepRate > 0) || !Number.isFinite(s.target)) return null;
  const ramp = s.stepType === 'ramp';
  const reference = ramp ? s.stepFinalTarget - s.target : s.target - s.stepFinalTarget;
  const measured = ramp ? s.stepFinalTarget - 1 - Math.min(...readings) : Math.max(...readings) - s.stepFinalTarget - 1;
  return Math.ceil(Math.max(0, reference, measured) * 60 / s.stepRate);
}
function dateAt(ms) { return new Date(timeOffset + ms).toLocaleTimeString("it-IT", { hour: "2-digit", minute: "2-digit", second: "2-digit" }); }
function chartWindow(oldest, newest, span, anchor, follow, position) {
  const maxStart = Math.max(oldest, anchor ?? oldest, newest - span);
  const start = follow ? maxStart : Math.max(oldest, Math.min(maxStart, position));
  return {start, end: start + span, maxStart};
}

function gridStep(select) { const value = Number(select.value); return [1, 5, 10].includes(value) ? value : 1; }
function setupChartScale(select, redraw) {
  try { const saved = localStorage.getItem(`oven-full:${select.id}`); if (['1', '5', '10'].includes(saved)) select.value = saved; } catch (_) {}
  select.onchange = () => {
    try { localStorage.setItem(`oven-full:${select.id}`, String(gridStep(select))); } catch (_) {}
    redraw();
  };
}
function temperatureBounds(values, step = 1) {
  const low = Math.floor((values.length ? Math.min(...values) - 1 : 15) / step) * step;
  const high = Math.ceil((values.length ? Math.max(...values) + 1 : 35) / step) * step;
  return {low, high: Math.max(low + Math.max(5, 2 * step), high)};
}
function drawTemperatureGrid(ctx, canvas, low, high, left, right, top, bottom, labelX, fontSize, step = 1) {
  const y = c => bottom - (c - low) / (high - low) * (bottom - top);
  const scale = canvas.clientHeight ? canvas.clientHeight / canvas.height : 1;
  const pixelsPerDegree = (bottom - top) * scale / (high - low);
  // Keep the selected grid spacing; thin labels only if they would overlap.
  let labelStep = step;
  while (pixelsPerDegree * labelStep < 14) labelStep = labelStep === 1 ? 5 : labelStep === 5 ? 10 : labelStep * 2;
  ctx.font = `${fontSize}px system-ui`;
  for (let c = Math.ceil(low / step) * step; c <= high; c += step) {
    const ten = c % 10 === 0, five = c % 5 === 0;
    ctx.strokeStyle = ten ? "#415970" : five ? "#2b3e50" : "#192632";
    ctx.lineWidth = ten ? 2 : five ? 1.5 : 1;
    ctx.beginPath(); ctx.moveTo(left, y(c)); ctx.lineTo(right, y(c)); ctx.stroke();
    if (c % labelStep === 0) {
      ctx.fillStyle = five ? "#b6c8d9" : "#8fa2b6";
      ctx.fillText(`${c}°`, labelX, y(c) + fontSize * 0.3);
    }
  }
  return labelStep;
}

function closestChartPoint(points, ms) {
  if (!points.length || ms < points[0].ms || ms > points.at(-1).ms) return null;
  let low = 0, high = points.length - 1;
  while (low < high) { const mid = (low + high) >>> 1; if (points[mid].ms < ms) low = mid + 1; else high = mid; }
  const after = points[low], before = points[Math.max(0, low - 1)];
  return ms - before.ms <= after.ms - ms ? before : after;
}
// Browser-only inspection: use the cached samples and DOM overlays, with no HTTP
// requests or canvas redraws on pointer movement. Coordinates also handle CSS/DPR scaling.
function createChartInspector(canvas, tooltip, cursor) {
  let plot = null, pointer = null, pending = false;
  function hide() { tooltip.hidden = cursor.hidden = true; }
  function paint() {
    if (!plot || !pointer) { hide(); return; }
    const rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) { hide(); return; }
    const sx = rect.width / plot.width, sy = rect.height / plot.height;
    const px = (pointer.x - rect.left) / sx, py = (pointer.y - rect.top) / sy;
    if (px < plot.left || px > plot.right || py < plot.top || py > plot.bottom) { hide(); return; }
    const ms = plot.start + (px - plot.left) / (plot.right - plot.left) * (plot.end - plot.start);
    const point = closestChartPoint(plot.points, ms);
    if (!point || Math.abs(plot.x(point.ms) - px) * sx > 20) { hide(); return; }
    const readings = plot.series.filter(s => Number.isFinite(point[s.key]));
    if (!readings.length) { hide(); return; }
    tooltip.textContent = `Campione · ${plot.timeLabel(point.ms)}\n` + readings.map(s => `${s.label}: ${temp(point[s.key])} °C`).join('\n');
    tooltip.hidden = cursor.hidden = false;
    cursor.style.left = `${plot.x(point.ms) * sx}px`;
    cursor.style.top = `${plot.top * sy}px`; cursor.style.height = `${(plot.bottom - plot.top) * sy}px`;
    const left = Math.max(4, Math.min(rect.width - tooltip.offsetWidth - 4, px * sx + 12));
    const top = Math.max(4, Math.min(rect.height - tooltip.offsetHeight - 4, py * sy - tooltip.offsetHeight - 10));
    tooltip.style.left = `${left}px`; tooltip.style.top = `${top}px`;
  }
  function move(event) {
    pointer = {x: event.clientX, y: event.clientY};
    if (!pending) { pending = true; requestAnimationFrame(() => { pending = false; paint(); }); }
  }
  canvas.addEventListener('pointermove', move);
  canvas.addEventListener('pointerdown', move);
  for (const event of ['pointerleave', 'pointercancel']) canvas.addEventListener(event, () => { pointer = null; hide(); });
  return {update(data) { plot = data; paint(); }, clear() { plot = null; hide(); }};
}

function display(s) {
  if (boot !== null && boot !== s.bootCount) {
    history = []; live = []; hasOlder = false; chartAtEnd = true; chartStart = 0; eventKey = "";
    cycleAnchor = cycleKey = null;
    pidLoaded = settingsLoaded = false; $("hardwareReady").checked = false;
    if (!recipesDirty) { recipesLoaded = false; loadRecipes().catch(() => {}); }
  }
  boot = s.bootCount; latest = s; connected = true; lastResponseAt = performance.now(); renderFailed = false;
  if (Number.isFinite(s.cycleStartedAtMs)) {
    const key = `${boot}:${s.cycleStartedAtMs}`;
    if (key !== cycleKey) { cycleKey = key; cycleAnchor = s.cycleStartedAtMs; chartAtEnd = true; drawSoon(); }
  }
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
  put("pidInfo", s.test?.enabled ? "Comando manuale TEST · PID escluso" : `PID ${Math.round(s.duty)}% · finestra ${s.windowSec} s · max ${s.maxPower}%`);
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
  put("cycleDuration", clock(s.cycleElapsedSec));
  const remaining = remainingStepSeconds(s), activeStep = ['running', 'paused'].includes(s.phase);
  put("stepDuration", remaining === null ? '—' : clock(remaining));
  put("stepTimeHint", activeStep ? s.stepType === 'hold' ? 'Tempo ancora richiesto in banda ±5 °C' :
    remaining === null ? 'Stima non disponibile' : 'Stima al rate del programma' : '');
  $('globalStop').classList.toggle('is-latched', !!s.emergency);
  $('globalStop').setAttribute?.('aria-pressed', String(!!s.emergency));
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
  $("statusDot").className = `status-dot ${bad ? "fault" : s.test?.running ? "paused" : s.phase === "running" ? "running" : s.phase === "paused" ? "paused" : "idle"}`;
  put("statusText", s.test?.enabled ? (s.test.running ? "TEST IN CORSO" : "MODALITÀ TEST") : labels[s.phase] || s.phase);
  $("alarmBox").classList.toggle("hidden", !bad);
  put("alarmText", s.fault || (s.phase === "interrupted" ? "Il ciclo precedente si è interrotto. uscita spenta. Valuta il materiale prima di iniziare un nuovo ciclo." : ""));
  put('cycleErrorInfo', s.emergency ? `Emergenza attiva: ${s.fault || 'uscite bloccate fino al riconoscimento.'}` :
    s.phase === 'fault' ? `Allarme attivo: ${s.fault || 'controlla il log.'}` :
    s.lastCycleError ? `Ultimo ciclo interrotto per errore: ${s.lastCycleError}` : 'Nessuna interruzione per errore registrata.');
  const interruption = s.lastInterruption || {recorded: s.interrupted, resetReason: s.resetReason, upperBoundSec: s.outageUpperBoundSec};
  const powerReset = [1, 9].includes(Number(interruption.resetReason));
  put("outageInfo", interruption.recorded ? `${powerReset ? '🚨 BLACKOUT / possibile interruzione di alimentazione' : '🚨 Riavvio durante un ciclo attivo'} registrato. ` +
    ((interruption.estimateKnown ?? !!interruption.upperBoundSec) ? `Intervallo senza controllo fino al riavvio, massimo stimato: ${clock(interruption.upperBoundSec)}; durata esatta non misurata.` :
    'Durata sconosciuta: ora NTP o checkpoint non disponibili.') +
    (interruption.programName ? ` Programma interrotto: ${interruption.programName}.` : '') +
    (interruption.pending ? ' Uscite bloccate: riconoscimento e nuovo avvio manuale richiesti.' : '') : 'Nessuna interruzione da blackout/reset durante un ciclo attivo registrata.');
  updateReportStatus(s);
  renderTestStatus(s);
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
  const blocked = active || !!latest?.test?.enabled || !!latest?.otaUpdating || testCommandBusy;
  $("startBtn").disabled = !connected || commandBusy || !latest?.ready || blocked || latest?.phase === "interrupted" ||
                            !recipesLoaded || recipesDirty || !recipes.length || !$("hardwareReady").checked;
  $("hardwareReady").disabled = active;
  $("recipeSelect").disabled = active || !connected;
  $("globalStop").disabled = false;
  $("refreshReport").disabled = !connected || !latest?.reportAvailable || reportBusy;
  updateReportExports();
  for (const id of ["saveRecipes", "savePid", "saveControl"]) $(id).disabled = !connected || active;
  $("exportSamples").disabled = !connected || exportBusy;
  document.querySelectorAll('#recipesView input, #recipesView select, #recipesView button').forEach(el => {
    if (el.id !== "exportRecipes") el.disabled = active || !connected;
  });
  $("pauseBtn").disabled = !connected || commandBusy || latest?.phase !== "running";
  $("resumeBtn").disabled = !connected || commandBusy || latest?.otaUpdating || latest?.test?.enabled || testCommandBusy || latest?.phase !== "paused";
  $("resetBtn").disabled = !connected || commandBusy || blocked || !["fault", "interrupted"].includes(latest?.phase);
  $("stopBtn").disabled = !latest;
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
    field(fields, "Target °C", numeric(step.target, 0, 400, 0.1, value => { step.target = value; }));
    if (step.type === "hold") field(fields, "Durata min", numeric(step.duration, 1, 360, 1, value => { step.duration = value; }));
    else field(fields, "Rate °C/min", numeric(step.rate, 0.1, null, 0.1, value => { step.rate = value; }));
    row.append(fields); box.append(row);
  });
  updateControls();
}
function saveName() { const recipe = editRecipe(); if (recipe && recipe.name !== $("recipeName").value.trim()) { recipe.name = $("recipeName").value.trim(); markDirty(); } }
async function saveRecipes() {
  saveName();
  try { validateRecipes(recipes); const data = await api("/api/recipes", { recipes }); recipes = data.recipes; recipesDirty = false; recipesLoaded = true; redrawSelectors(); note("recipeMessage", "Programmi salvati nella memoria dell'ESP."); }
  catch (e) { note("recipeMessage", e.message, true); }
}
function download(name, data, mime) {
  const url = URL.createObjectURL(new Blob([data], { type: mime }));
  const a = document.createElement("a"); a.href = url; a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
async function command(action) {
  const safetyAction = action === 'stop' || action === 'emergency';
  if ((commandBusy || testCommandBusy) && !safetyAction) return;
  if (safetyAction) cancelTestOwnership();
  const generation = ++commandGeneration; commandBusy = true; updateControls();
  try {
    const result = await api("/api/command", { action, recipeId: selectedId, hardwareReady: $("hardwareReady").checked });
    if (generation !== commandGeneration) { if (action === "start") api("/api/command", {action:"stop"}).catch(() => {}); return; }
    if (["start", "stop", "emergency", "reset"].includes(action)) $("hardwareReady").checked = false;
    display(result);
    if (action === "start") { chartAtEnd = true; drawSoon(); }
    if (!$("logsView").hidden) refreshLogs();
    const text = ({ start: "Ciclo avviato.", pause: "Ciclo in pausa: uscita spenta.", resume: "Ciclo ripreso.", stop: "Ciclo fermato: uscita spenta.", emergency: 'EMERGENZA: ciclo e TEST fermati; uscite bloccate fino al riconoscimento.', reset: "Allarme/interruzione riconosciuti." })[action];
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
  if (!points.length) { dashboardInspector.clear(); put("chartRangeLabel", "Nessun campione disponibile."); return; }
  const oldest = points[0].ms, newest = points.at(-1).ms;
  const choice = $("chartSpan").value;
  const span = choice === "all" ? Math.max(60000, newest - oldest) : Number(choice);
  const viewport = chartWindow(oldest, newest, span, choice === 'all' ? null : cycleAnchor, chartAtEnd, chartStart);
  chartStart = viewport.start;
  const {end, maxStart} = viewport;
  const visible = points.filter(p => p.ms >= chartStart && p.ms <= end);
  // Markers come from ESP history even while the temperature curve uses denser live points.
  const markers = history.filter(p => p.marker && p.ms >= chartStart && p.ms <= end);
  const vals = visible.flatMap(p => [p.actual, p.target]).filter(Number.isFinite);
  const step = gridStep($("chartYStep")), {low, high} = temperatureBounds(vals, step);
  const plot = { l: 62, r: 15, t: 18, b: 38 };
  const x = ms => plot.l + (ms - chartStart) / span * (W - plot.l - plot.r);
  const y = c => plot.t + (H - plot.t - plot.b) * (1 - (c - low) / (high - low));
  drawTemperatureGrid(ctx, canvas, low, high, plot.l, W - plot.r, plot.t, H - plot.b, 8, 14, step);
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
  const endText = dateAt(end); ctx.fillText(endText, W - 120, H - 10);
  const slider = $("chartPosition"); slider.min = String(oldest); slider.max = String(maxStart); slider.step = "1000"; slider.value = String(chartStart); slider.disabled = maxStart === oldest;
  $("chartBack").disabled = chartStart <= oldest && !hasOlder;
  $("chartForward").disabled = chartStart >= maxStart;
  $("chartNow").disabled = chartAtEnd;
  put("chartRangeLabel", `${dateAt(chartStart)} – ${dateAt(end)} · ${visible.length} campioni${end > newest ? ' · spazio a destra per le nuove letture' : ''}${hasOlder ? " · dati precedenti disponibili" : ""}`);
  dashboardInspector.update({points: visible, series: [{key: 'actual', label: 'Media PT100'}, {key: 'target', label: 'Setpoint'}],
    width: W, height: H, left: plot.l, right: W - plot.r, top: plot.t, bottom: H - plot.b, start: chartStart, end, x, timeLabel: dateAt});
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
function connectionLost() {
  const age = lastResponseAt === null ? Infinity : performance.now() - lastResponseAt;
  const stale = age < 6000;
  connected = false; renderFailed = false; $("hardwareReady").checked = false; testOffline(stale);
  put("statusText", stale ? `AGGIORNAMENTO IN RITARDO · ${Math.ceil(age / 1000)} s` : "ESP NON RAGGIUNGIBILE");
  $("statusDot").className = `status-dot ${stale ? 'paused' : 'fault'}`;
  if (!stale) for (const id of ["avgTemp", "targetTemp", "deltaTemp", "t1", "t2", "cycleDuration", "stepDuration"]) put(id, "—");
  put("sampleAge", Number.isFinite(age) ? `Non aggiornate da ${Math.ceil(age / 1000)} s` : 'Non disponibili');
  put("relayState", "SCONOSCIUTO"); put("pidInfo", "Comando attuale non verificato");
  $("relayState").classList.remove('heating');
  for (let i = 0; i < 2; i++) put(`fault${i + 1}`, stale ? `Ultima lettura · ${latest.sensors[i].message} · dati non aggiornati` : "Dato non aggiornato");
  updateControls();
}
async function refresh() {
  if (busy || commandBusy || testCommandBusy) return; busy = true;
  const generation = commandGeneration;
  let s;
  try { s = await api("/api/status"); }
  catch (e) { if (generation === commandGeneration) connectionLost(); }
  finally { busy = false; }
  if (!s || generation !== commandGeneration) return;
  try {
    display(s);
    if (!recipesLoaded && !recipesDirty) loadRecipes().catch(() => {});
    if (!$("dashboardView").hidden && (!history.length || s.historyLatestSeq > (history.at(-1)?.seq || 0))) loadHistory(!history.length);
  } catch (e) {
    if (generation !== commandGeneration) return;
    connected = false; renderFailed = true; testOffline(false); updateControls();
    put("statusText", "ERRORE DASHBOARD");
    console.error(e); note("globalMessage", `Errore visualizzazione: ${e.message}`, true);
  }
  finally { busy = false; }
}
function show(view) {
  for (const name of ["Dashboard", "Recipes", "Test", "Logs", "Report"]) {
    const active = view === name;
    $(`tab${name}`).classList.toggle("active", active);
    $(`${name.toLowerCase()}View`).hidden = !active;
  }
  if (view !== "Test") cancelTestOwnership(true);
  if (view === "Report" && latest?.reportAvailable) loadReport();
  if (view === "Logs") refreshLogs();
  if (view === "Dashboard") drawSoon();
}

const reportOutcomes = {completed: "CICLO COMPLETATO", stopped: "CICLO FERMATO", fault: "CICLO IN ALLARME"};
function reportNumber(value, unit = "", digits = 2) { return Number.isFinite(value) ? `${value.toFixed(digits)}${unit}` : "—"; }
function reportTime(epoch, uptimeMs, bootCount) {
  return epoch ? new Date(epoch * 1000).toLocaleString("it-IT") : `Avvio ${bootCount} · uptime ${clock(uptimeMs / 1000)} (ora NTP assente)`;
}
function reportSummaryPairs(r) {
  const stats = r.stats, cfg = r.settings;
  const thermal = stats.sensors.flatMap((s, i) => [
    [`PT100 #${i + 1} · iniziale → finale valida`, `${reportNumber(s.startC, " °C")} → ${reportNumber(s.endC, " °C")}`],
    [`PT100 #${i + 1} · min / max`, `${reportNumber(s.minC, " °C")} / ${reportNumber(s.maxC, " °C")}`]
  ]);
  return [
    ["Programma", r.recipeName], ["Inizio", reportTime(r.startUtc, r.startUptimeMs, r.bootCount)],
    ["Fine", reportTime(r.endUtc, r.endUptimeMs, r.bootCount)],
    ["Durata totale", clock(r.durationMs / 1000)], ["Tempo attivo", clock(r.activeMs / 1000)],
    ["Pause", `${r.pauseCount} · ${clock(r.pausedMs / 1000)}`], ["Step completati", `${r.completedSteps} / ${r.stepCount}`],
    ["PID usato", `Kp ${cfg.kp} · Ki ${cfg.ki} · Kd ${cfg.kd}`],
    ["Comando usato", `${cfg.actuator === "ssr" ? "SSR" : "Relè meccanico"} · ${cfg.windowSec} s · limite ${cfg.maxPower}%`],
    ['Banda mantenimento', `±${r.holdBandC ?? (r.version >= 2 ? 5 : 1)} °C`],
    ...thermal, ["ΔT massimo sonde", reportNumber(stats.maxDeltaC, " °C")],
    ["Errore medio assoluto · rampa + hold", reportNumber(stats.meanAbsoluteErrorC, " °C")],
    ["Superamento massimo del setpoint", reportNumber(stats.maxOvershootC, " °C")],
    ["Ritardo massimo della media", reportNumber(stats.maxLagC, " °C")],
    ["PID medio richiesto", reportNumber(stats.meanCommandPct, "%")],
    ["Campioni al limite di comando", reportNumber(stats.saturationPct, "%")],
    ["Campioni / coppie non valide", `${stats.samples} / ${stats.invalidPairs}`]
  ];
}
function reportStepRows(r) {
  return r.steps.map(step => {
    const stats = step.stats;
    const holdRange = step.type === "hold" ? stats.sensors.map(s => reportNumber(
      s.validSamples ? s.maxC - s.minC : null, " °C")).join(" / ") : "—";
    return [
      `${step.index + 1} · ${types[step.type] || step.type}${step.type !== "hold" ? ` · ${step.rate} °C/min` : ""}`,
      reportNumber(step.target, " °C", 1), step.completed ? "Completato" : step.started ? "Interrotto" : "Non iniziato",
      clock(step.activeMs / 1000), clock(step.pausedMs / 1000),
      step.type === "hold" ? `${clock(step.holdInBandMs / 1000)} / ${clock(step.durationMin * 60)}` : "—",
      reportNumber(stats.meanAbsoluteErrorC, " °C"), reportNumber(stats.maxOvershootC, " °C"), reportNumber(stats.maxLagC, " °C"),
      `${reportNumber(stats.meanCommandPct, "%")} / ${reportNumber(stats.saturationPct, "%")}`, holdRange
    ];
  });
}
function updateReportStatus(s) {
  $("refreshReport").disabled = !s.reportAvailable || reportBusy;
  $("reportNotice").hidden = !s.reportAvailable;
  if (s.reportAvailable && (!lastReport || lastReport.key !== s.reportKey)) void loadReport(s.reportKey);
  if (!s.reportAvailable && !lastReport) put("reportMessage", "Il report sarà disponibile al termine del primo ciclo con questo firmware.");
}
async function loadReport(expectedKey = latest?.reportKey) {
  if (reportBusy) return;
  reportBusy = true; $("refreshReport").disabled = true;
  try {
    const r = await api("/api/report");
    if (r.key !== latest?.reportKey || (expectedKey && r.key !== expectedKey)) return;
    const previous = lastReport?.key === r.key ? lastReport : null;
    r.curve = previous?.curve || [];
    r.curvePartial = previous?.curvePartial ?? r.curvePartial;
    r.curveError = '';
    if (r.curveAvailable && !r.curve.length) {
      try { r.curve = await loadReportCurve(r); }
      catch (e) { r.curveError = e.message; }
    }
    if (r.key !== latest?.reportKey) return;
    lastReport = r;
    put('reportDefinitions', REPORT_DEFINITIONS.replace('±5 °C', `±${r.holdBandC ?? (r.version >= 2 ? 5 : 1)} °C`));
    put("reportNoticeText", `${r.recipeName} · ${reportOutcomes[r.outcome]} · ${clock(r.durationMs / 1000)}`);
    put("reportOutcome", reportOutcomes[r.outcome] || r.outcome);
    $("reportOutcome").className = r.outcome === "fault" ? "log-danger" : r.outcome === "stopped" ? "log-warning" : "sensor-ok";
    put("reportReason", r.reason);
    put("reportMessage", r.saved ? "Ultimo report salvato sull'ESP, disponibile anche dopo un riavvio. Un nuovo ciclo lo sostituirà solo quando termina." : "Report disponibile solo in RAM: scaricalo prima di riavviare l'ESP.");
    $("reportSummary").replaceChildren();
    for (const [label, value] of reportSummaryPairs(r)) {
      const box = document.createElement("div"), title = document.createElement("span"), item = document.createElement("strong");
      title.textContent = label; item.textContent = value; box.append(title, item); $("reportSummary").append(box);
    }
    $("reportSteps").replaceChildren();
    for (const values of reportStepRows(r)) {
      const row = document.createElement("tr");
      for (const value of values) { const cell = document.createElement("td"); cell.textContent = value; row.append(cell); }
      $("reportSteps").append(row);
    }
    $("reportContent").hidden = false;
    drawReportPlot();
    put('reportCurveMessage', reportCurveMessage(r));
    updateReportExports();
  } catch (e) { put("reportMessage", `Report non disponibile: ${e.message}`); }
  finally { reportBusy = false; $("refreshReport").disabled = !connected || !latest?.reportAvailable; updateReportExports(); }
}
function updateReportExports() {
  for (const id of ['exportReport', 'exportReportJson', 'exportReportCsv', 'exportReportImage'])
    $(id).disabled = !lastReport || reportBusy || reportExportBusy ||
      (['exportReportCsv', 'exportReportImage'].includes(id) && !lastReport.curve?.length);
}
function reportChartOptions(r) {
  return {startMs: r.startUptimeMs, durationMs: r.durationMs, intervalMs: r.curveIntervalMs || 10000};
}
function drawReportPlot() {
  if (!lastReport) return;
  $('reportPlot').innerHTML = OvenExport.svg(lastReport.curve || [],
    {...reportChartOptions(lastReport), width: $('reportPlot').clientWidth || 1200});
}
window.addEventListener('resize', drawReportPlot);
function reportCurveMessage(r) {
  if (!r.curve?.length) return r.curveError ? `Curva non disponibile: ${r.curveError}` :
    'Curva non disponibile: il riepilogo è conservato, ma i campioni in RAM non sono disponibili dopo un riavvio o per i report precedenti a questo aggiornamento.';
  return `${r.curve.length} campioni · intervallo storico circa ${(r.curveIntervalMs || 10000) / 1000} s · ${r.curvePartial ?
    'Curva parziale: l’inizio non è più nello storico.' : 'Intero ciclo, incluse pause e cooldown.'} Scarica la curva prima di riavviare l’ESP.`;
}
async function loadReportCurve(r) {
  const points = []; let after = 0;
  while (true) {
    const data = await api(`/api/report/history?key=${encodeURIComponent(r.key)}&after=${after}`);
    if (data.key !== r.key || data.bootCount !== r.bootCount || !data.available)
      throw Error('Campioni del ciclo non più disponibili.');
    const page = data.samples.map(historyPoint);
    if (data.more && (!page.length || page.at(-1).seq <= after)) throw Error('Pagina della curva non valida.');
    points.push(...page); r.curvePartial = !!data.partial;
    if (!data.more) break;
    after = page.at(-1).seq;
  }
  if (r.curveSamples && points.length !== r.curveSamples) throw Error('Curva incompleta: aggiorna il report.');
  return points;
}
async function exportCurvePackage(name, points, csvOptions, chartOptions) {
  const image = await OvenExport.png(points, chartOptions);
  const zip = OvenExport.zip([{name: `${name}.csv`, data: OvenExport.csv(points, csvOptions)},
    {name: `${name}.png`, data: await image.arrayBuffer()}]);
  download(`${name}.zip`, zip, 'application/zip');
}
async function exportCycleReport(format = 'pdf') {
  if (!lastReport || reportExportBusy) return;
  const r = lastReport, name = `oven-cycle-${r.key}`;
  reportExportBusy = true; updateReportExports();
  try {
    if (format === 'json') { download(`${name}.json`, JSON.stringify(r, null, 2), 'application/json'); return; }
    if (format === 'csv') {
      if (!r.curve?.length) throw Error('Campioni del ciclo non disponibili.');
      await exportCurvePackage(name, r.curve, {boot: r.bootCount, startMs: r.startUptimeMs,
        offset: r.startUtc ? r.startUtc * 1000 - r.startUptimeMs : null}, reportChartOptions(r));
    } else if (format === 'png') {
      if (!r.curve?.length) throw Error('Campioni del ciclo non disponibili.');
      download(`${name}.png`, await OvenExport.png(r.curve, reportChartOptions(r)), 'image/png');
    } else {
      const bytes = OvenExport.pdf({report: r, points: r.curve || [], summary: reportSummaryPairs(r),
        stepRows: reportStepRows(r), explanation: $('reportDefinitions').textContent,
        curveMessage: reportCurveMessage(r)});
      download(`${name}.pdf`, bytes, 'application/pdf');
    }
  } catch(e) { put('reportMessage', `Esportazione non riuscita: ${e.message}`); }
  finally { reportExportBusy = false; updateReportExports(); }
}

function validateRecipes(list) {
  if (!Array.isArray(list) || list.length < 1 || list.length > 8) throw Error("Servono 1–8 programmi.");
  const ids = new Set();
  for (const recipe of list) {
    if (!recipe || typeof recipe.id !== "string" || !/^[A-Za-z0-9_-]{1,32}$/.test(recipe.id) || ids.has(recipe.id)) throw Error("ID programma assente, duplicato o non valido.");
    ids.add(recipe.id);
    if (typeof recipe.name !== "string" || !recipe.name.trim() || new TextEncoder().encode(recipe.name).length > 48) throw Error("Nome programma richiesto, massimo 48 byte.");
    if (!Array.isArray(recipe.steps) || recipe.steps.length < 1 || recipe.steps.length > 12) throw Error("Servono 1–12 step per programma.");
    let previous = null;
    for (const step of recipe.steps) {
      if (!step || !["ramp", "hold", "cooldown"].includes(step.type) || !Number.isFinite(step.target) || step.target < 0 || step.target > 400) throw Error("Tipo step o target non valido (0–400 °C).");
      if (step.type === "hold") { if (!Number.isInteger(step.duration) || step.duration < 1 || step.duration > 360) throw Error("Hold: 1–360 minuti interi."); }
      else if (!Number.isFinite(step.rate) || step.rate < 0.1) throw Error("Rate minimo 0,1 °C/min.");
      if (previous !== null && ((step.type === "ramp" && step.target <= previous) || (step.type === "cooldown" && step.target >= previous) || (step.type === "hold" && Math.abs(step.target - previous) > 0.1))) throw Error("Sequenza incoerente: ramp sale, cooldown scende, hold mantiene il target precedente.");
      previous = step.target;
    }
  }
}

$("tabDashboard").onclick = () => show("Dashboard");
$("tabRecipes").onclick = () => show("Recipes");
$("tabTest").onclick = () => show("Test");
$("tabLogs").onclick = () => show("Logs");
$("tabReport").onclick = () => show("Report");
$("openReport").onclick = () => show("Report");
$("refreshReport").onclick = () => loadReport();
$("exportReport").onclick = () => exportCycleReport('pdf');
$("exportReportCsv").onclick = () => exportCycleReport('csv');
$("exportReportImage").onclick = () => exportCycleReport('png');
$("exportReportJson").onclick = () => exportCycleReport('json');
$("startBtn").onclick = () => command("start");
$("pauseBtn").onclick = () => command("pause");
$("resumeBtn").onclick = () => command("resume");
$("stopBtn").onclick = () => command("stop");
$("globalStop").onclick = () => command("emergency");
$("hardwareReady").onchange = updateControls;
$("resetBtn").onclick = () => command("reset");
$("recipeSelect").onchange = e => { selectedId = e.target.value; renderPreview(); };
$("editRecipeSelect").onchange = e => { saveName(); editId = e.target.value; renderEditor(); };
$("recipeName").onchange = saveName;
$("addStep").onclick = () => { const r = editRecipe(); if (!r) return; if (r.steps.length >= 12) return note("recipeMessage", "Massimo 12 step.", true); r.steps.push({ type: "hold", target: r.steps.at(-1)?.target ?? 50, duration: 10 }); markDirty(); renderEditor(); renderPreview(); };
$("addRecipe").onclick = () => {
  if (recipes.length >= 8) return note("recipeMessage", "Massimo 8 programmi.", true);
  saveName(); const id = `programma-${Date.now().toString(36)}`;
  recipes.push({ id, name: "Nuovo programma", steps: [{ type: "ramp", target: 50, rate: 1 }, { type: "hold", target: 50, duration: 10 }] });
  editId = selectedId = id; markDirty(); redrawSelectors();
};
$("deleteRecipe").onclick = () => { if (recipes.length <= 1) return note("recipeMessage", "Deve restare almeno un programma.", true); recipes = recipes.filter(r => r.id !== editId); editId = selectedId = recipes[0].id; markDirty(); redrawSelectors(); };
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
$("exportRecipes").onclick = () => { saveName(); download("oven-full-programmi.json", JSON.stringify(recipes, null, 2), "application/json"); };
$("importRecipes").onchange = async e => {
  const file = e.target.files[0]; if (!file) return;
  try { const parsed = JSON.parse(await file.text()); validateRecipes(parsed); recipes = parsed; markDirty(); editId = selectedId = recipes[0]?.id || ""; redrawSelectors(); note("recipeMessage", "Programmi importati. Controlla gli step e premi Salva sull'ESP."); }
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
    await exportCurvePackage(`oven-full-history-avvio-${exportBoot}`, points,
      {boot: exportBoot, offset: exportOffset}, {startMs: points[0].ms});
    note("globalMessage", `CSV e grafico PNG esportati nello ZIP: ${points.length} campioni.`);
  } catch (e) { note("globalMessage", e.message, true); }
  finally { exportBusy = false; updateControls(); }
};
$("chartBack").onclick = () => moveChart(-1);
$("chartForward").onclick = () => moveChart(1);
$("chartNow").onclick = () => { chartAtEnd = true; drawChart(); };
$("chartPosition").oninput = e => { chartStart = Number(e.target.value); chartAtEnd = chartStart >= Number(e.target.max) - 1000; drawSoon(); };
$("chartSpan").onchange = () => { if ($("chartSpan").value === "all") loadAll(); drawChart(); };
const dashboardInspector = createChartInspector($("tempChart"), $("chartTooltip"), $("chartCursor"));
setupChartScale($("chartYStep"), drawChart);
updateControls(); drawChart(); loadRecipes().catch(e => note("messageBox", `Programmi non disponibili: ${e.message}`, true)); refresh();
setInterval(refresh, 500);
setInterval(() => { if (!renderFailed && lastResponseAt !== null && (!connected || performance.now() - lastResponseAt > 5000)) connectionLost(); }, 500);
setInterval(() => { if (!$("logsView").hidden) refreshLogs(); }, 5000);

// Field-test UI uses the same firmware and the same status poll as the full dashboard.
(function (fullApi) {

const $ = id => document.getElementById('test' + id[0].toUpperCase() + id.slice(1));
let token = 0, online = false, state = null, boot = null;
let samples = [], historySeq = 0, historyBusy = false, historyGeneration = 0;
let chartAtEnd = true, chartStart = 0, testAnchor = null, runSeq = 0, hasOlder = false;
let logs = [], lastHistoryPoll = 0, lastLogPoll = 0, lastHeartbeat = 0;
let heartbeatBusy = false, logsBusy = false;
const inspector = createChartInspector($('tempChart'), $('chartTooltip'), $('chartCursor'));

const fmt = (value, digits = 1) => Number.isFinite(value) ? value.toFixed(digits) : '—';
const duration = ms => {
  const s = Math.floor(ms / 1000);
  return `${String(Math.floor(s / 3600)).padStart(2, '0')}:${String(Math.floor(s / 60) % 60).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;
};
function adapt(data) {
  return {...data, ...data.test, test: data.test?.name || 'Lettura sonde',
    interrupted: data.test?.interrupted || false, historyLatestSeq: data.test?.historyLatestSeq || 0};
}
async function api(path, body) {
  const route = path.replace('/api/command', '/api/test/command').replace('/api/heartbeat', '/api/test/heartbeat').replace('/api/history', '/api/test/history');
  return fullApi(route, body, body === undefined ? 2500 : 1800);
}
function message(text, error = false) {
  $('messageBox').textContent = text;
  $('messageBox').className = `notice${error ? ' error' : ''}`;
}
function setControls() {
  $('stopBtn').disabled = !state;
  $('pulseDuration').disabled = !online || !!state?.running;
  document.querySelectorAll('#testView [data-action]').forEach(button => {
    button.disabled = !online || testCommandBusy || commandBusy || !!state?.running || !!state?.otaUpdating || !!state?.emergency || ['running', 'paused'].includes(state?.phase);
  });
  const cycle = ['running', 'paused'].includes(state?.phase);
  $('enter').disabled = !online || testCommandBusy || commandBusy || cycle || !!state?.enabled || !!state?.otaUpdating || !!state?.emergency;
  $('exit').disabled = !online || testCommandBusy || commandBusy || !state?.enabled || !!state?.otaUpdating;
  $('exportSamples').disabled = !online || historyBusy;
  $('refreshLogs').disabled = !online || logsBusy;
  $('exportLogs').disabled = !online || logsBusy;
}
function clearOwnership() { token = 0; }
function setOffline(stale = false) {
  online = false; clearOwnership();
  $('statusText').textContent = stale ? 'Aggiornamento in ritardo · ultimi dati ricevuti' : 'Scollegato'; $('statusDot').className = `status-dot ${stale ? 'paused' : 'fault'}`;
  $('relayState').textContent = 'SCONOSCIUTO'; $('relayHint').textContent = 'Ultimo stato non aggiornato';
  $('relayState').className = '';
  $('greenIndicator').className = $('redIndicator').className = 'physical-led unknown';
  for (let i = 1; i <= 2; i++) {
    if (!stale) { $(`t${i}`).textContent = '—'; $(`ohms${i}`).textContent = '— Ω'; $(`raw${i}`).textContent = 'Dato non aggiornato'; }
    $(`sensorState${i}`).textContent = 'Dato non aggiornato';
    if (!stale) $(`fault${i}`).textContent = 'Connessione persa';
  }
  if (!stale) $('deltaTemp').textContent = '—'; $('testName').textContent = 'Stato attuale non verificato'; $('testRemaining').textContent = '—';
  $('otaState').textContent = 'Collegamento perso';
  setControls();
}
function renderStatus(data) {
  if (boot !== null && boot !== data.bootCount) {
    clearOwnership(); samples = []; historySeq = 0; historyGeneration++;
    chartAtEnd = true; chartStart = 0; testAnchor = null; runSeq = 0; hasOlder = false;
    logs = []; $('logList').textContent = 'Scheda riavviata · Caricamento log…';
    message('Scheda riavviata: relè spento.');
  }
  boot = data.bootCount; state = data; online = true;
  if (data.runSeq && data.runSeq !== runSeq) {
    runSeq = data.runSeq; testAnchor = data.startedAtMs; chartAtEnd = true;
    lastHistoryPoll = 0; drawChart();
  }
  if (!data.running && token) clearOwnership();
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
  $('modeInfo').textContent = data.enabled ? 'Modalità TEST attiva · Cicli disabilitati fino a Esci da TEST.' : 'Modalità ciclo · Le prove attivano TEST automaticamente a ciclo fermo.';
  $('historyInfo').textContent = `Finestra 5 minuti · Storico ESP ${data.historyCapacity === 0 ? 'non disponibile' : 'fino a ' + (data.historyCapacity ?? 3600) + ' campioni a 1 s'}`;
  $('testName').textContent = data.test;
  $('testRemaining').textContent = data.running ? (data.remainingMs >= 60000 ? duration(Math.ceil(data.remainingMs / 1000) * 1000) : `${(data.remainingMs / 1000).toFixed(1)} s`) : '—';
  $('workshopState').textContent = data.workshopConnected ? 'Connesso' : data.workshopConfigured ? 'In connessione' : 'Non configurato';
  $('otaState').textContent = data.otaUpdating ? 'Aggiornamento in corso' : data.otaEnabled ? `Attivo · Porta ${data.otaPort}` : 'Disabilitato';
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
  if ((testCommandBusy || commandBusy) && action !== 'stop') return;
  const generation = ++commandGeneration;
  testCommandBusy = true; setControls(); updateControls();
  try {
    const body = {action};
    if (action === 'relayPulse') body.durationMs = Number($('pulseDuration').value);
    if (action === 'stop') clearOwnership();
    const data = await api('/api/command', body);
    if (generation !== commandGeneration) {
      if (action !== 'stop') void api('/api/command', {action: 'stop'}).catch(() => {});
      return;
    }
    token = data.test?.token || 0;
    lastHeartbeat = 0;
    display(data); updateControls();
    if (token) void heartbeat();
    message(({stop: 'STOP ricevuto: relè spento.', enter: 'Diagnostica attiva: nessun PID.', exit: 'Modalità TEST terminata: uscita spenta.'})[action] || 'Prova avviata: mantieni questa pagina visibile.');
    lastLogPoll = 0;
  } catch (error) {
    if (generation !== commandGeneration) return;
    message(error.name === 'AbortError' ? 'Risposta assente: verifica la connessione. Il supervisore arresta il test senza heartbeat.' : error.message, true);
  } finally { if (generation === commandGeneration) { testCommandBusy = false; setControls(); updateControls(); } }
}
async function heartbeat() {
  if (!token || heartbeatBusy || document.hidden) return;
  heartbeatBusy = true; lastHeartbeat = performance.now();
  const requestToken = token;
  try { await api('/api/heartbeat', {token: requestToken}); }
  catch (error) { if (token === requestToken) { clearOwnership(); setControls(); void poll(); } }
  finally { heartbeatBusy = false; }
}
async function poll() {
  if (document.getElementById('testView').hidden || document.hidden || !online || testCommandBusy) return;
  if (performance.now() - lastHistoryPoll > 1500) await loadHistory();
  if (performance.now() - lastLogPoll > 5000) void loadLogs();
}
async function loadHistory() {
  if (historyBusy) return;
  historyBusy = true; lastHistoryPoll = performance.now();
  const generation = historyGeneration;
  try {
    const data = await api(historySeq ? `/api/history?after=${historySeq}` : '/api/history?tail=1');
    if (generation !== historyGeneration || data.bootCount !== boot) return;
    samples = samples.concat(data.samples).filter((row, i, all) => (!i || row[0] !== all[i - 1][0]) && row[0] >= data.oldestSeq);
    hasOlder = !!samples.length && samples[0][0] > data.oldestSeq;
    if (data.samples.length) historySeq = data.samples[data.samples.length - 1][0];
    if (data.more) lastHistoryPoll = 0;
    drawChart();
  } catch (error) { $('chartRangeLabel').textContent = 'Storico temporaneamente non disponibile'; }
  finally { historyBusy = false; setControls(); }
}
async function moveChart(dir) {
  if (dir < 0 && hasOlder && samples.length && chartStart - 225000 < samples[0][1]) {
    if (historyBusy) return;
    historyBusy = true;
    const generation = historyGeneration, before = samples[0][0];
    try {
      const data = await api(`/api/history?before=${before}`);
      if (generation !== historyGeneration || data.bootCount !== boot) return;
      samples = data.samples.filter(row => row[0] < before).concat(samples).filter(row => row[0] >= data.oldestSeq);
      hasOlder = !!samples.length && samples[0][0] > data.oldestSeq;
    } catch (error) { $('chartRangeLabel').textContent = 'Impossibile caricare i dati precedenti'; return; }
    finally { historyBusy = false; setControls(); }
  }
  chartAtEnd = false; chartStart += dir * 225000; drawChart();
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
  const oldest = samples[0]?.[1] ?? testAnchor ?? 0, newest = samples.at(-1)?.[1] ?? oldest;
  const viewport = chartWindow(oldest, newest, 300000, testAnchor, chartAtEnd, chartStart);
  const {start, end, maxStart} = viewport; chartStart = start;
  const visible = samples.filter(s => s[1] >= start && s[1] <= end);
  const values = visible.flatMap(s => [s[2], s[3]]).filter(Number.isFinite);
  const step = gridStep($('chartYStep')), {low, high} = temperatureBounds(values, step);
  const left = 48, right = width - 12, top = 16, bottom = height - 35;
  const x = ms => left + (ms - start) / Math.max(1, end - start) * (right - left);
  const y = c => bottom - (c - low) / (high - low) * (bottom - top);
  // This canvas uses CSS coordinates after the device-pixel-ratio transform.
  drawTemperatureGrid(ctx, {height, clientHeight: height}, low, high, left, right, top, bottom, 0, 11, step);
  ctx.font = '11px system-ui'; ctx.fillStyle = '#8fa2b6';
  for (let i = 0; i <= 4; i++) {
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
  if (testAnchor !== null && testAnchor >= start && testAnchor <= end) {
    ctx.strokeStyle = '#cc9cff'; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(x(testAnchor), top); ctx.lineTo(x(testAnchor), bottom); ctx.stroke();
    ctx.fillStyle = '#cc9cff'; ctx.fillText('INIZIO TEST', x(testAnchor) + 5, top + 12);
  }
  $('chartBack').disabled = chartStart <= oldest && !hasOlder;
  $('chartForward').disabled = chartStart >= maxStart;
  $('chartNow').disabled = chartAtEnd;
  $('chartRangeLabel').textContent = `${duration(start)} – ${duration(end)} dall'avvio · ${visible.length} campioni`;
  inspector.update({points: visible.map(row => ({ms: row[1], t1: row[2], t2: row[3]})),
    series: [{key: 't1', label: 'PT100 #1'}, {key: 't2', label: 'PT100 #2'}], width, height, left, right, top, bottom, start, end, x,
    timeLabel: ms => `${duration(ms)} dall'avvio`});
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
  if (!token && !testCommandBusy) return;
  commandGeneration++; testCommandBusy = false; clearOwnership(); setControls(); updateControls();
  // Best effort immediate stop. The firmware lease also expires independently.
  const body = new Blob([JSON.stringify({action: 'stop'})], {type: 'text/plain'});
  navigator.sendBeacon('/api/test/command', body);
}
$('enter').addEventListener('click', () => void command('enter'));
setupChartScale($('chartYStep'), drawChart);
$('chartBack').onclick = () => void moveChart(-1);
$('chartForward').onclick = () => void moveChart(1);
$('chartNow').onclick = () => { chartAtEnd = true; drawChart(); };
$('exit').addEventListener('click', () => void command('exit'));
$('stopBtn').addEventListener('click', () => void command('stop'));
document.querySelectorAll('#testView [data-action]').forEach(b => b.addEventListener('click', () => void command(b.dataset.action)));
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
document.addEventListener('visibilitychange', () => { if (document.hidden) stopOnHide(); else void poll(); });
window.addEventListener('pagehide', stopOnHide);
window.addEventListener('resize', drawChart);
setInterval(() => { if (performance.now() - lastHeartbeat >= 650) void heartbeat(); }, 100);
setInterval(() => void poll(), 500);
renderTestStatus = data => { renderStatus(adapt(data)); if (!document.getElementById('testView').hidden) void poll(); };
testOffline = setOffline;
cancelTestOwnership = leave => {
  const owned = !!token || testCommandBusy;
  stopOnHide();
  if (leave && owned) void api('/api/command', {action: 'exit'}).catch(() => {});
};
drawChart();

})(api);
