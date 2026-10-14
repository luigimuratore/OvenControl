// Run with Node.js; exercises the actual dashboard against a small DOM/canvas mock.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'data/index.html'), 'utf8');
const elements = new Map(), strokes = [], texts = [], frames = [], preferences = new Map();
function element() {
  const classes = new Set();
  return {textContent: '', value: '', hidden: false, disabled: false, checked: false,
    style: {}, listeners: {}, offsetWidth: 180, offsetHeight: 70,
    classList: {add(...names) { names.forEach(name => classes.add(name)); },
      remove(...names) { names.forEach(name => classes.delete(name)); }, contains(name) { return classes.has(name); },
      toggle(name, force) { const on = force === undefined ? !classes.has(name) : force;
        if (on) classes.add(name); else classes.delete(name); return on; }},
    addEventListener(name, handler) { this.listeners[name] = handler; },
    append(...nodes) { this.textContent += nodes.map(node => typeof node === 'string' ? node : node.textContent).join(''); }, replaceChildren() {},
    getBoundingClientRect: () => ({left: 100, top: 100, width: 600, height: 210}),
    width: 1200, height: 420, clientWidth: 1200, clientHeight: 420,
    getContext() { return new Proxy({}, {get(target, key) {
      if (key === 'measureText') return text => ({width: text.length * 7});
      if (key === 'moveTo' || key === 'lineTo') return (x, y) => strokes.push([key, x, y]);
      if (key === 'fillText') return text => texts.push(text);
      return target[key] ?? (() => {});
    }}); }};
}
for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
  const el = element(); el.id = match[1]; el.hidden = /\bhidden\b/.test(match[0]); elements.set(match[1], el);
  const initial = match[0].match(/\bvalue="([^"]*)"/); if (initial) el.value = initial[1];
}
elements.get('chartSpan').value = '1800000';
let now = 10000, fetchImpl, requestCount = 0;
const status = {
  bootCount: 1, uptimeMs: 610000, sampleAtMs: 610000, cycleStartedAtMs: 600000,
  phase: 'running', target: 31, duty: 20, relay: true, ready: false,
  sensors: [30, 30.2].map(c => ({valid: true, c, ohms: 111, raw: 7739, rref: 470, faultCode: 0, message: 'Lettura valida'})),
  stepCount: 1, stepIndex: 0, stepType: 'ramp', stepFinalTarget: 50, stepRate: 1,
  actuator: 'relay', maxPower: 30, windowSec: 60, pid: {kp: 12, ki: .015, kd: 50},
  cycleElapsedSec: 10, stepElapsedSec: 10, recipeName: 'Prova', historyLatestSeq: 0,
  test: {enabled: false, running: false, name: 'Lettura sonde', runSeq: 0, startedAtMs: 0, historyCapacity: 3600},
  freeHeap: 160000, minFreeHeap: 150000, resetReason: 1, chipTemperatureC: 40,
};
const response = data => ({status: 200, ok: true, json: async () => data});
fetchImpl = async url => response(url === '/api/status' ? status : url === '/api/recipes' ? {recipes: []} :
  {bootCount: 1, oldestSeq: 1, latestSeq: 0, samples: [], events: []});
const context = vm.createContext({console, AbortController, Blob, URL, TextEncoder,
  performance: {now: () => now}, fetch: (...args) => { requestCount++; return fetchImpl(...args); },
  localStorage: {getItem: key => preferences.get(key), setItem: (key, value) => preferences.set(key, value)},
  setTimeout: () => 1, clearTimeout() {}, setInterval() {}, requestAnimationFrame: callback => { frames.push(callback); return frames.length; },
  window: {devicePixelRatio: 1, addEventListener() {}}, navigator: {sendBeacon() {}},
  document: {hidden: false, addEventListener() {}, querySelectorAll: () => [], createElement: element,
    getElementById(id) { assert(elements.has(id), `Missing HTML element: ${id}`); return elements.get(id); }},
});
vm.runInContext(fs.readFileSync(path.join(root, 'data/app.js'), 'utf8'), context);
const run = source => vm.runInContext(source, context);
const flush = () => new Promise(resolve => setImmediate(resolve));
const flushFrames = () => { while (frames.length) frames.shift()(); };

(async () => {
  await flush(); await flush();
  // The recipe editor/import validation accepts 400 C and rejects targets above it.
  const highRecipe = target => [{id:'alta', name:'Prova alta', steps:[{type:'ramp', target, rate:1}]}];
  run(`validateRecipes(${JSON.stringify(highRecipe(400))})`);
  assert.throws(() => run(`validateRecipes(${JSON.stringify(highRecipe(400.1))})`), /0–400/);
  assert.equal(elements.get('avgTemp').textContent, '30.1');
  // A latched error remains prominent after valid readings return, in every tab.
  const clean = {...status, phase:'idle', emergency:false, resetRequired:false, supervisorFault:0,
    fault:'', lastInterruption:{pending:false}, manual:{active:false,mode:'idle'}, ready:true};
  for (const blocked of [
    {phase:'fault',fault:'PT100 in errore'}, {phase:'interrupted'}, {emergency:true},
    {supervisorFault:1}, {resetRequired:true}, {lastInterruption:{pending:true}},
  ]) {
    run(`display(${JSON.stringify({...clean,...blocked})})`);
    assert(elements.get('errorResetBar').classList.contains('reset-required'));
    assert(elements.get('resetBtn').classList.contains('reset-required'));
    assert(!elements.get('resetBtn').disabled);
    assert(elements.get('errorResetText').textContent.includes('RESET ERRORI'));
    for (const tab of ['Dashboard','Manual','Test','Recipes']) {
      run(`show('${tab}')`);
      assert(!elements.get('errorResetBar').hidden);
    }
  }
  run(`display(${JSON.stringify({...clean,phase:'fault',sensors:clean.sensors.map(s=>({...s,valid:false}))})})`);
  assert(elements.get('errorResetText').textContent.includes('ripristina le letture'));
  run('connected=false;updateControls()');
  assert(elements.get('resetBtn').disabled);
  assert(elements.get('errorResetBar').classList.contains('reset-required'));
  assert(elements.get('errorResetText').textContent.includes('Connessione assente'));
  run(`connected=true;show('Dashboard');display(${JSON.stringify(clean)})`);
  assert(!elements.get('errorResetBar').classList.contains('reset-required'));
  assert(elements.get('resetBtn').disabled);
  assert(!html.includes('Riconosci allarme / interruzione'));
  run(`display(${JSON.stringify(status)})`);
  assert.equal(elements.get('targetDeltaTemp').textContent, '-0.9');
  for (const [mean, expected] of [[32, '+1.0'], [31, '0.0'], [30.99, '0.0'], [29, '-2.0']]) {
    run(`display({...latest,target:31,sensors:[${mean},${mean}].map((c,i)=>({...latest.sensors[i],c,valid:true}))})`);
    assert.equal(elements.get('targetDeltaTemp').textContent, expected);
  }
  run('display({...latest,target:null})'); assert.equal(elements.get('targetDeltaTemp').textContent, '—');
  assert(!html.slice(html.indexOf('class="hero-grid"'), html.indexOf('class="sensor-grid probes-row"')).includes('id="deltaCard"'));
  assert(html.indexOf('id="deltaCard"') > html.indexOf('id="fault2"'));
  const deltaCard = elements.get('deltaCard').classList;
  for (const [delta, warning, danger] of [[2.2,false,false],[10,false,false],[10.1,true,false],
      [20,true,false],[20.1,false,true],[40,false,true],[5,false,false]]) {
    run(`display({...latest,sensors:[100,${100 + delta}].map((c,i)=>({...latest.sensors[i],c,valid:true}))})`);
    assert.equal(deltaCard.contains('delta-warning'), warning, `yellow at ${delta}`);
    assert.equal(deltaCard.contains('delta-danger'), danger, `red at ${delta}`);
  }
  run(`display({...latest,sensors:[100,140].map((c,i)=>({...latest.sensors[i],c,valid:i===0}))})`);
  assert.equal(elements.get('deltaTemp').textContent, '—');
  assert.equal(elements.get('targetDeltaTemp').textContent, '—');
  assert(!deltaCard.contains('delta-warning') && !deltaCard.contains('delta-danger'));
  run(`display(${JSON.stringify(status)})`);
  run(`display({...latest,firmware:'full-1.6',firmwareModifiedOn:'2026-10-13'})`);
  assert.equal(elements.get('firmwareInfo').textContent, 'Firmware full-1.6 · Ultima modifica: 13/10/2026');
  run(`display({...latest,firmware:'LOCAL-PREVIEW',firmwareVersion:'full-1.6',preview:true})`);
  assert.equal(elements.get('firmwareInfo').textContent, 'Anteprima locale · Firmware full-1.6 · Ultima modifica: 13/10/2026');
  run(`display(${JSON.stringify(status)})`);
  assert.equal(elements.get('firmwareInfo').textContent, 'Firmware — · Ultima modifica: non disponibile');
  run(`display({...latest, workshopConfigured:true, workshopConnected:false, stationMac:'AA:BB:CC:DD:EE:FF',
    wifi:{recoveryRunning:true, recoveryPaused:false, retryCount:2, retryInSec:30, statusCode:4,
      lastDisconnectReason:202, lastDisconnectMessage:'Autenticazione rifiutata'}})`);
  assert(elements.get('wifiDiagnostics').textContent.includes('202: Autenticazione rifiutata'));
  assert(elements.get('wifiDiagnostics').textContent.includes('30 s'));
  assert(elements.get('wifiDiagnostics').textContent.includes('AA:BB:CC:DD:EE:FF'));
  run(`display({...latest, workshopConfigured:true, workshopConnected:true, stationMac:'AA:BB:CC:DD:EE:FF',
    wifi:{recoveryRunning:true, retryCount:2, lastDisconnectReason:202, lastDisconnectMessage:'Autenticazione rifiutata'}})`);
  assert(!elements.get('wifiDiagnostics').textContent.includes('Ultimo errore'));
  run(`display({...latest, stepRemainingSec:180, stepRemainingEstimated:true, lastCycleError:'Fault PT100',
    lastInterruption:{recorded:true, resetReason:9, upperBoundSec:90}})`);
  assert.equal(elements.get('stepDuration').textContent, '00:03:00');
  assert(elements.get('stepTimeHint').textContent.includes('Stima'));
  assert(elements.get('cycleErrorInfo').textContent.includes('Fault PT100'));
  assert(elements.get('outageInfo').textContent.includes('00:01:30'));
  assert(elements.get('outageInfo').textContent.includes('durata esatta non misurata'));
  run(`display({...latest, lastInterruption:{recorded:true, resetReason:1, upperBoundSec:0,
    estimateKnown:true, pending:true, programName:'Programma interrotto'}})`);
  assert(elements.get('outageInfo').textContent.includes('🚨 BLACKOUT'));
  assert(elements.get('outageInfo').textContent.includes('00:00:00'));
  assert(elements.get('outageInfo').textContent.includes('Programma interrotto'));
  assert(elements.get('outageInfo').textContent.includes('Uscite bloccate'));
  run(`display({...latest, lastInterruption:{recorded:true, resetReason:3, upperBoundSec:0,
    estimateKnown:false, pending:true}})`);
  assert(elements.get('outageInfo').textContent.includes('Riavvio durante'));
  assert(!elements.get('outageInfo').textContent.includes('BLACKOUT'));
  assert(elements.get('outageInfo').textContent.includes('Durata sconosciuta'));
  run(`display({...latest, phase:'paused', stepType:'hold', stepRemainingSec:120,lastInterruption:{pending:false}})`);
  assert.equal(elements.get('stepDuration').textContent, '00:02:00');
  assert(elements.get('stepTimeHint').textContent.includes('pausa'));
  run(`display({...latest,phase:'paused',pauseMaintainsTemperature:true,pauseTargetC:174.2,target:174.2,duty:8,relay:true})`);
  assert.equal(elements.get('targetLabel').textContent, 'Target di pausa');
  assert.equal(elements.get('statusText').textContent, 'PAUSA · MANTENIMENTO');
  assert(elements.get('stepTimeHint').textContent.includes('PID mantiene 174.2 °C'));
  assert.equal(elements.get('relayState').textContent, 'ACCESO');
  assert(elements.get('startBtn').disabled && elements.get('pauseBtn').disabled);
  assert(!elements.get('resumeBtn').disabled);
  run(`display({...latest, phase:'running', stepType:'ramp', stepFinalTarget:175, stepRate:10,
    stepRemainingSec:6, stepWaitingForTemperature:true, stepTransitionToHold:true, stepCompletionBandC:5})`);
  assert(elements.get('stepTimeHint').textContent.includes('entro ±5 °C per iniziare il mantenimento'));
  assert.equal(elements.get('stepDuration').textContent, '00:00:06');
  assert.equal(run(`remainingStepSeconds({phase:'running',stepType:'ramp',target:175,stepFinalTarget:175,
    stepRate:10,stepTransitionToHold:true,stepCompletionBandC:5,sensors:[173.759,174.7651].map(c=>({c,valid:true}))})`), 0);
  assert.equal(run(`remainingStepSeconds({phase:'running',stepType:'ramp',target:175,stepFinalTarget:175,
    stepRate:10,stepTransitionToHold:true,stepCompletionBandC:5,sensors:[175,181].map(c=>({c,valid:true}))})`), 6);
  run(`display({...latest, phase:'running', stepType:'hold', stepRemainingSec:120})`);
  assert(elements.get('stepTimeHint').textContent.includes('banda ±5'));
  run(`display(${JSON.stringify(status)})`);
  // The new cycle begins at the left margin despite preceding measurements.
  run(`history = [{seq:1, ms:0, actual:24, target:NaN}, {seq:2, ms:600000, actual:30, target:30, marker:16}]; drawChart();`);
  assert.equal(run('chartStart'), 600000);
  assert(strokes.some(([op, x]) => op === 'moveTo' && x === 62));
  assert(elements.get('chartRangeLabel').textContent.includes('spazio a destra'));
  await run('moveChart(-1)');
  assert(run('chartStart') < 600000);
  assert.equal(run('history.length'), 2);
  elements.get('chartNow').onclick();
  assert.equal(run('chartStart'), 600000);
  run('live = [{ms:2500000, actual:40, target:40}]; drawChart();');
  assert.equal(run('chartStart'), 700000); // Follow after filling the 30-minute window.
  // New starts reset the viewport even while inspecting older data; reload also anchors.
  run('chartAtEnd = false; display({...latest, cycleStartedAtMs:2600000, sampleAtMs:2600000}); drawChart();');
  assert.equal(run('chartStart'), 2600000);

  // Brief failures retain visibly stale temperatures; relay state becomes unknown immediately.
  run('updateDeltaIndicator(25)');
  now += 2500; run('connectionLost()');
  assert(deltaCard.contains('delta-danger'));
  assert.equal(elements.get('avgTemp').textContent, '30.1');
  assert(elements.get('statusText').textContent.includes('RITARDO'));
  assert.equal(elements.get('relayState').textContent, 'SCONOSCIUTO');
  assert(elements.get('startBtn').disabled);
  assert(!elements.get('globalStop').disabled);
  now += 4000; run('connectionLost()');
  assert.equal(elements.get('avgTemp').textContent, '—');
  assert.equal(elements.get('targetDeltaTemp').textContent, '—');
  assert(!deltaCard.contains('delta-warning') && !deltaCard.contains('delta-danger'));
  assert.equal(elements.get('statusText').textContent, 'ESP NON RAGGIUNGIBILE');
  run('display(latest)');
  assert.equal(elements.get('avgTemp').textContent, '30.1');

  // TEST uses the same left anchor, and the navigation handlers are installed.
  run(`display({...latest, test:{...latest.test, runSeq:1, startedAtMs:3000000}})`);
  assert(elements.get('testChartRangeLabel').textContent.startsWith('00:50:00'));
  run(`display({...latest, test:{...latest.test, runSeq:2, startedAtMs:4000000}})`);
  assert(elements.get('testChartRangeLabel').textContent.startsWith('01:06:40'));
  assert.equal(typeof elements.get('testChartBack').onclick, 'function');

  // Y choices change the actual grid and are remembered independently, without moving time.
  assert(!html.includes('Griglia 1 °C · linee marcate'));
  for (const step of [1, 5, 10]) {
    texts.length = strokes.length = 0;
    run(`drawTemperatureGrid($('tempChart').getContext('2d'), $('tempChart'), 20, 40, 0, 100, 0, 400, 0, 10, ${step})`);
    assert.equal(strokes.length, 2 * (20 / step + 1));
    assert.deepEqual(texts, Array.from({length: 20 / step + 1}, (_, i) => `${20 + i * step}°`));
  }
  const position = run('chartStart');
  elements.get('chartYStep').value = '10'; elements.get('chartYStep').onchange();
  assert.equal(run('chartStart'), position);
  assert.equal(preferences.get('oven-full:chartYStep'), '10');
  elements.get('testChartYStep').value = '5'; elements.get('testChartYStep').onchange();
  assert.equal(preferences.get('oven-full:testChartYStep'), '5');
  elements.get('chartYStep').value = '1'; run(`setupChartScale($('chartYStep'), drawChart)`);
  assert.equal(elements.get('chartYStep').value, '10');

  // Hover uses recorded values and accounts for a canvas displayed at half its logical size.
  run(`history = [{ms:0,actual:20,target:21}, {ms:10000,actual:22,target:23}]; live=[]; cycleAnchor=null;
    chartAtEnd=true; $('chartSpan').value='900000'; drawChart();`);
  const requestsBeforeHover = requestCount;
  const chart = elements.get('tempChart'), tooltip = elements.get('chartTooltip');
  chart.listeners.pointermove({clientX: 100 + (62 + 9000 / 900000 * 1123) / 2, clientY: 150}); flushFrames();
  assert(!tooltip.hidden);
  assert(tooltip.textContent.includes('Media PT100: 22.0 °C'));
  assert(tooltip.textContent.includes('Setpoint: 23.0 °C'));
  assert(tooltip.textContent.includes('ΔT (media PT100 − target): -1.0 °C'));
  for (const [actual, label] of [[24, '+1.0'], [23, '0.0'], [23.01, '0.0'], [22.99, '0.0']]) {
    run(`history[1].actual=${actual}; drawChart()`);
    assert(tooltip.textContent.includes(`ΔT (media PT100 − target): ${label} °C`));
  }
  run('history[1].target=NaN; drawChart()');
  assert(tooltip.textContent.includes('Media PT100: 23.0 °C'));
  assert(!tooltip.textContent.includes('ΔT (media PT100 − target)'));
  run('history[1].target=23; history[1].actual=NaN; drawChart()');
  assert(tooltip.textContent.includes('Setpoint: 23.0 °C'));
  assert(!tooltip.textContent.includes('ΔT (media PT100 − target)'));
  assert.equal(requestCount, requestsBeforeHover);
  assert(parseFloat(tooltip.style.left) >= 0 && parseFloat(tooltip.style.left) + tooltip.offsetWidth <= 600);
  chart.listeners.pointermove({clientX: 400, clientY: 150}); flushFrames();
  assert(tooltip.hidden); // No value is invented in the empty future portion.
  run('history[1].actual=NaN; history[1].target=NaN; drawChart()');
  chart.listeners.pointermove({clientX: 100 + (62 + 9000 / 900000 * 1123) / 2, clientY: 150}); flushFrames();
  assert(tooltip.hidden);
  chart.listeners.pointerleave(); assert(elements.get('chartCursor').hidden);

  // TEST hover shows the two individual PT100 samples, also without requesting data.
  await flush();
  fetchImpl = async url => response(url.startsWith('/api/test/history') ? {
    bootCount:1, oldestSeq:1, latestSeq:2, samples:[[1,5000000,30,31,0,0,0,0,3],[2,5010000,32,33,0,0,0,0,3]],
  } : {bootCount:1, events:[]});
  run(`show('Test'); renderTestStatus({...latest, test:{...latest.test, runSeq:3, startedAtMs:5000000}})`);
  await flush(); await flush();
  const testRequests = requestCount;
  elements.get('testTempChart').listeners.pointermove({clientX: 100 + (48 + 9000 / 300000 * 1140) / 2, clientY:150}); flushFrames();
  assert(elements.get('testChartTooltip').textContent.includes('PT100 #1: 32.0 °C'));
  assert(elements.get('testChartTooltip').textContent.includes('PT100 #2: 33.0 °C'));
  assert(!elements.get('testChartTooltip').textContent.includes('ΔT (media PT100 − target)'));
  assert.equal(requestCount, testRequests);
  run(`show('Dashboard')`);

  // Manual has its own tab while sharing live measurements/history with the dashboard.
  run("show('Manual')");
  assert(!elements.get('manualControl').hidden && elements.get('programControls').hidden);
  assert(!elements.get('dashboardView').hidden);
  assert(elements.get('tabManual').classList.contains('active'));
  run("show('Dashboard')");
  assert(elements.get('manualControl').hidden && !elements.get('programControls').hidden);
  run("show('Manual')");
  // Manual regulation owns no program/steps; changing target keeps the same graph/session.
  run(`display({...latest,phase:'idle',ready:true,cycleStartedAtMs:null,manual:{active:false,mode:'idle',targetC:null,elapsedSec:0}})`);
  assert(elements.get('manualStart').disabled); // Operator confirmation is mandatory.
  elements.get('manualHardwareReady').checked = true;
  elements.get('manualTarget').value = '100'; run('updateControls()');
  assert(!elements.get('manualStart').disabled);
  for (const value of ['', '401', '-1', 'not-a-number']) {
    elements.get('manualTarget').value = value; run('updateControls()');
    assert(elements.get('manualStart').disabled);
  }
  elements.get('manualTarget').value = '100'; run('updateControls()');
  for (const value of ['', '0', '.09', 'not-a-number']) {
    elements.get('manualRate').value = value; run('updateControls()');
    assert(elements.get('manualStart').disabled);
  }
  elements.get('manualRate').value = '2'; run('updateControls()');
  const manualPosts = [];
  fetchImpl = async (url, opts) => {
    if (opts.method !== 'POST') return response({bootCount:1,events:[],samples:[],recipes:[]});
    const body = JSON.parse(opts.body); manualPosts.push([url, body]);
    const cooldown = body.action === 'cooldown';
    return response({...status, phase: 'running',ready:true,stepCount:0,recipeName:'',
      cycleStartedAtMs:620000, target:body.targetC,
      manual:{active:true,mode:cooldown?'cooldown':'regulating',targetC:body.targetC,rateCMin:body.rateCMin,setpointC:body.targetC,elapsedSec:30,startedAtMs:620000}});
  };
  await run("manualCommand('start')");
  assert.deepEqual(manualPosts[0], ['/api/manual', {action:'start',hardwareReady:true,targetC:100,rateCMin:2}]);
  assert(elements.get('programControls').hidden);
  assert.equal(elements.get('targetLabel').textContent, 'Setpoint manuale');
  assert(elements.get('statusText').textContent.includes('MANUALE'));
  assert.equal(elements.get('manualDuration').textContent, '00:00:30');
  assert(!elements.get('manualApply').disabled && !elements.get('manualCooldown').disabled && !elements.get('manualStop').disabled);
  assert(elements.get('savePid').disabled && elements.get('startBtn').disabled);
  const manualAnchor = run('cycleAnchor');
  elements.get('manualTarget').value = '150'; elements.get('manualTarget').oninput();
  run('display(latest)'); assert.equal(elements.get('manualTarget').value, '150'); // Polling preserves a draft.
  await run("manualCommand('target')");
  assert.equal(manualPosts[1][1].targetC, 150);
  assert.equal(manualPosts[1][1].rateCMin, 2);
  assert.equal(run('cycleAnchor'), manualAnchor);
  await run("manualCommand('cooldown')");
  assert.equal(manualPosts[2][1].targetC, 25);
  assert.equal(manualPosts[2][1].rateCMin, 2);
  assert.equal(elements.get('manualAppliedTarget').textContent, '25.0 °C');
  assert(elements.get('manualState').textContent.includes('COOLDOWN'));
  assert(elements.get('manualCooldown').disabled);
  assert(!elements.get('manualApply').disabled && !elements.get('manualStop').disabled);
  run(`display({...latest,cooldownRegulated:true,target:174,duty:7,relay:true,
    manual:{...latest.manual,active:true,mode:'cooldown',targetC:25,setpointC:174,cooldownComplete:false}})`);
  assert(elements.get('manualState').textContent.includes('DISCESA REGOLATA'));
  assert(elements.get('manualHint').textContent.includes('Il PID dà calore'));
  assert.equal(elements.get('relayState').textContent, 'ACCESO');
  run(`display({...latest,duty:0,relay:false,manual:{...latest.manual,cooldownComplete:true}})`);
  assert(elements.get('manualState').textContent.includes('COMPLETATO · uscita spenta'));
  assert(elements.get('manualHint').textContent.includes('Target cooldown raggiunto'));
  const point = run('historyPoint([1,1000,30,31,50,10,19,255])');
  assert.equal(point.step, null); assert.equal(point.operation, 'manual');
  run('connectionLost()');
  assert(elements.get('manualStart').disabled && elements.get('manualApply').disabled);
  assert(!elements.get('globalStop').disabled);
  // An emergency overtakes an in-flight manual start. Its late reply triggers a compensating STOP.
  let finishManual;
  const safetyPosts = [];
  fetchImpl = (url, opts) => {
    if (opts.method !== 'POST') return Promise.resolve(response(url === '/api/status' ?
      {...status,phase:'fault',emergency:true,manual:{active:false,mode:'idle'}} : {events:[],samples:[],bootCount:1}));
    const body = JSON.parse(opts.body); safetyPosts.push([url,body.action]);
    if (url === '/api/manual') return new Promise(resolve => { finishManual = resolve; });
    return Promise.resolve(response({...status,phase:'fault',emergency:true,manual:{active:false,mode:'idle'}}));
  };
  run('display({...latest,phase:"idle",ready:true,emergency:false,manual:{active:false,mode:"idle"}})');
  const pendingManual = run("manualCommand('start')");
  await run("command('emergency')");
  finishManual(response({...status,manual:{active:true,mode:'regulating'}}));
  await pendingManual; await flush();
  assert(safetyPosts.some(([url,action])=>url === '/api/command' && action === 'emergency'));
  assert(safetyPosts.some(([url,action])=>url === '/api/command' && action === 'stop'));
  assert.equal(run('latest.emergency'), true);

  // Reads are serialized, status jumps ahead of history, and STOP bypasses reads.
  await flush();
  const sent = [], releases = [];
  fetchImpl = (url, opts) => { sent.push([url, opts.method]); return new Promise(resolve => releases.push(() => resolve(response({})))); };
  const jobs = run(`[api('/api/logs'), api('/api/history'), api('/api/status'), api('/api/command', {action:'stop'})]`);
  assert.deepEqual(sent.map(x => x[0]), ['/api/logs', '/api/command']);
  releases.shift()(); releases.shift()(); await flush();
  assert.equal(sent[2][0], '/api/status');
  releases.shift()(); await flush();
  assert.equal(sent[3][0], '/api/history');
  releases.shift()(); await Promise.all(jobs);
  console.log('Dashboard: graph anchors/navigation, selectable Y grid, browser-only hover, stale readings/recovery and request priority passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });
