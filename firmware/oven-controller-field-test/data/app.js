'use strict';
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
