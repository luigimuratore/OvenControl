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
