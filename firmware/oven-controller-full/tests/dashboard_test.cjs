// Run with Node.js; exercises the actual dashboard against a small DOM/canvas mock.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'data/index.html'), 'utf8');
const elements = new Map(), strokes = [], texts = [], frames = [], preferences = new Map();
function element() {
  return {textContent: '', value: '', hidden: false, disabled: false, checked: false,
    style: {}, listeners: {}, offsetWidth: 180, offsetHeight: 70,
    classList: {add() {}, remove() {}, toggle() {}}, addEventListener(name, handler) { this.listeners[name] = handler; }, append() {}, replaceChildren() {},
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
  run(`display({...latest, stepRemainingSec:180, stepRemainingEstimated:true, lastCycleError:'Fault PT100',
    lastInterruption:{recorded:true, resetReason:9, upperBoundSec:90}})`);
  assert.equal(elements.get('stepDuration').textContent, '00:03:00');
  assert(elements.get('stepTimeHint').textContent.includes('Stima'));
  assert(elements.get('cycleErrorInfo').textContent.includes('Fault PT100'));
  assert(elements.get('outageInfo').textContent.includes('00:01:30'));
  assert(elements.get('outageInfo').textContent.includes('durata esatta non misurata'));
  run(`display({...latest, phase:'paused', stepType:'hold', stepRemainingSec:120})`);
  assert.equal(elements.get('stepDuration').textContent, '00:02:00');
  assert(elements.get('stepTimeHint').textContent.includes('±5'));
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
  now += 2500; run('connectionLost()');
  assert.equal(elements.get('avgTemp').textContent, '30.1');
  assert(elements.get('statusText').textContent.includes('RITARDO'));
  assert.equal(elements.get('relayState').textContent, 'SCONOSCIUTO');
  assert(elements.get('startBtn').disabled);
  assert(!elements.get('globalStop').disabled);
  now += 4000; run('connectionLost()');
  assert.equal(elements.get('avgTemp').textContent, '—');
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
  assert.equal(requestCount, testRequests);
  run(`show('Dashboard')`);

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
