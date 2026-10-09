const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const {jsPDF} = require('../data/vendor/jspdf.umd.min.js');
const Export = require('../data/report-export.js');
const source = fs.readFileSync(path.join(root, 'data/app.js'), 'utf8');
const context = vm.createContext({types:{ramp:'Ramp',hold:'Hold',cooldown:'Cooldown'}});
vm.runInContext(source.slice(source.indexOf('function clock('), source.indexOf('function temp(')) +
  source.slice(source.indexOf('const reportOutcomes'), source.indexOf('function updateReportStatus')), context);
const stats = {samples:300,invalidPairs:1,sensors:[0,1].map(i=>({validSamples:299,startC:30+i*.1,endC:25+i*.1,minC:25,maxC:50+i*.1})),
  maxDeltaC:.5,meanAbsoluteErrorC:.46,maxOvershootC:.29,maxLagC:1.02,meanCommandPct:8.42,saturationPct:0};
const report = {key:'example-1-2880000',firmware:'full-1.2',bootCount:1,recipeName:'ESEMPIO - curva dimostrativa',outcome:'completed',
  reason:'Anteprima di impaginazione con dati dimostrativi. Non rappresenta misure del forno.',
  startUtc:1791469158,endUtc:1791472038,startUptimeMs:0,endUptimeMs:2880000,durationMs:2880000,activeMs:2880000,pausedMs:0,pauseCount:0,
  completedSteps:3,stepCount:3,curveIntervalMs:10000,settings:{kp:12,ki:.015,kd:50,actuator:'ssr',windowSec:5,maxPower:30},stats,
  steps:['ramp','hold','cooldown'].map((type,index)=>({index,type,target:type==='cooldown'?25:50,rate:1,durationMin:3,started:true,completed:true,
    activeMs:[1200000,180000,1500000][index],pausedMs:0,holdInBandMs:type==='hold'?180000:0,stats}))};
const points = Array.from({length:289},(_,i)=>{
  const m=i/6, target=m<=20?30+m:m<=23?50:Math.max(25,50-(m-23));
  const actual=target-.35+.1*Math.sin(i/8);
  return {seq:i+1,ms:i*10000,t1:actual-.1,t2:i===80?NaN:actual+.1,actual:i===80?NaN:actual,target,duty:m<=23?8:0,
    relay:i%4===0,step:m<=20?0:m<=23?1:2,marker:i===0?16:i===288?64:0};
});
const csv = Export.csv(points,{boot:1,offset:report.startUtc*1000,startMs:0});
assert.equal(csv.split('\r\n').length,291);
assert(csv.includes('media_c,setpoint_c'));
assert(csv.includes('ALLARME')===false);
assert.equal(csv.split('\r\n')[81].split(',')[6],''); // Invalid mean stays missing.
assert.equal(Export.elapsed(200,4294967000),496);
assert.equal(Export.chartModel([{ms:4294967000,actual:1,target:2},{ms:200,actual:2,target:3}],{startMs:4294967000}).rows[1].minutes,496/60000);
assert(!Export.svg(points,{startMs:0}).includes('NaN'));
assert(Export.svg([]).includes('Curva non disponibile'));
const marks=[];
Export.paintChart({text(){},dot(){},line(...args){marks.push(args);}},[
  {ms:0,target:30,actual:30},{ms:10000,target:31,actual:NaN},{ms:20000,target:32,actual:32}],{startMs:0});
assert.equal(marks.filter(m=>m[4]==='#36a982' && m[1]>54).length,0); // No line bridges a missing reading.
const measurements=[];
global.jspdf = {jsPDF:class extends jsPDF {
  constructor(...args) {
    super(...args); const draw=this.text;
    this.text=(value,x,y,options)=>{
      const list=Array.isArray(value)?value:[value];
      const width=Math.max(...list.map(line=>this.getTextWidth(line)));
      const right=options?.align==='right'?x:options?.align==='center'?x+width/2:x+width;
      measurements.push({value:list.join(' '),y,right,bottom:y+(list.length-1)*this.getLineHeight()/this.internal.scaleFactor});
      return draw.call(this,value,x,y,options);
    };
  }
}};
function generate(r, curve) {
  context.report=r;
  const summary=vm.runInContext('reportSummaryPairs(report)',context), stepRows=vm.runInContext('reportStepRows(report)',context);
  const explanation=fs.readFileSync(path.join(root,'data/index.html'),'utf8').match(/<p id="reportDefinitions"[^>]*>([^<]+)<\/p>/)[1];
  return Export.pdf({report:r,points:curve,summary,stepRows,explanation,
    curveMessage:curve.length?'Curva dimostrativa - 289 campioni, circa 10 s.':'Curva non disponibile dopo riavvio; riepilogo conservato.'});
}
module.exports = {report,points};
if (require.main === module) (async()=>{
  const pdf=generate(report,points);
  assert.equal(Buffer.from(pdf).subarray(0,4).toString(),'%PDF');
  const longReport={...report,recipeName:'Ricetta di collaudo prolungata con carico completo',reason:'PT100/MAX31865 in errore: uscita spenta · PT100 #2 Fault 0x4, RAW 8303 · #2: Sovra/sottotensione sugli ingressi RTD;',
    stepCount:12,steps:Array.from({length:12},(_,i)=>({...report.steps[i%3],index:i}))};
  generate(longReport,points);
  generate({...report,startUtc:null,endUtc:null},[]);
  assert(measurements.every(m=>m.bottom<=289 && m.y>=10 && m.right<=195),JSON.stringify(measurements.filter(m=>m.bottom>289 || m.y<10 || m.right>195)));
  assert(measurements.some(m=>m.value.includes('max-min')));
  assert(measurements.every(m=>!m.value.includes('−')));
  const zip=Export.zip([{name:'curva.csv',data:csv},{name:'curva.svg',data:Export.svg(points)}]);
  assert.equal(Buffer.from(await zip.arrayBuffer()).readUInt32LE(0),0x04034b50);
  if(process.env.OVEN_EXPORT_PREVIEW_DIR) {
    fs.mkdirSync(process.env.OVEN_EXPORT_PREVIEW_DIR,{recursive:true});
    fs.writeFileSync(path.join(process.env.OVEN_EXPORT_PREVIEW_DIR,'report-example.pdf'),Buffer.from(pdf));
    fs.writeFileSync(path.join(process.env.OVEN_EXPORT_PREVIEW_DIR,'export-test.zip'),Buffer.from(await zip.arrayBuffer()));
  }
  console.log('Report export: CSV, invalid gaps, timer wrap, SVG, ZIP, PDF, 12-step pagination and missing-curve fallback passed.');
})().catch(e=>{console.error(e);process.exitCode=1;});
