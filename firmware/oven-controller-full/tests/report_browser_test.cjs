// Requires Playwright; set OVEN_CHROME_PATH to use an installed Chromium browser.
const fs=require('node:fs');
const path=require('node:path');
const assert=require('node:assert/strict');
const {chromium}=require(process.env.OVEN_PLAYWRIGHT_MODULE || 'playwright');
const {report,points}=require('./report_export_test.cjs');
const root=path.resolve(__dirname,'..');
const output=process.env.OVEN_BROWSER_OUTPUT || '/tmp/oven-report-browser';
fs.mkdirSync(output,{recursive:true});
const r={...report,curveAvailable:true,curveSamples:points.length,curvePartial:false};
const rows=points.map(p=>[p.seq,p.ms,p.t1,p.t2,p.target,p.duty,(Number.isFinite(p.t1)?1:0)|(Number.isFinite(p.t2)?2:0)|8|p.marker,p.step]);
const status={bootCount:1,uptimeMs:2880000,sampleAtMs:2880000,cycleStartedAtMs:0,phase:'complete',target:null,duty:0,relay:false,ready:false,
  sensors:[25,25.1].map(c=>({valid:true,c,ohms:110,raw:7700,rref:470,faultCode:0,message:'Lettura valida'})),
  stepCount:3,stepIndex:3,stepType:'cooldown',stepFinalTarget:25,stepRate:1,actuator:'ssr',maxPower:30,windowSec:5,pid:{kp:12,ki:.015,kd:50},
  cycleElapsedSec:2880,stepElapsedSec:1500,recipeName:r.recipeName,historyLatestSeq:points.length,
  test:{enabled:false,running:false,name:'Lettura sonde',runSeq:0,startedAtMs:0,historyCapacity:3600},
  reportAvailable:true,reportKey:r.key,freeHeap:160000,minFreeHeap:150000,resetReason:1,chipTemperatureC:40};
(async()=>{
  const browser=await chromium.launch({headless:true,executablePath:process.env.OVEN_CHROME_PATH});
  try {
    const page=await browser.newPage({viewport:{width:1400,height:1000},acceptDownloads:true});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.route('**/*',async route=>{
      const u=new URL(route.request().url()), after=Number(u.searchParams.get('after') || 0);
      const json=data=>route.fulfill({contentType:'application/json',body:JSON.stringify(data)});
      if(u.pathname==='/api/command') {
        const body=route.request().postDataJSON();
        if(body.action==='emergency') Object.assign(status,{phase:'fault',emergency:true,ready:false,relay:false,
          fault:'EMERGENZA: tutte le uscite spente',test:{...status.test,enabled:false,running:false}});
        else if(body.action==='reset') Object.assign(status,{phase:'idle',emergency:false,fault:'',ready:true});
        return json(status);
      }
      if(u.pathname==='/api/status')return json(status);
      if(u.pathname==='/api/report')return json(r);
      if(u.pathname==='/api/report/history') {
        const samples=rows.filter(row=>row[0]>after).slice(0,80);
        return json({key:r.key,bootCount:1,available:true,partial:false,samples,more:samples.at(-1)?.[0]<points.length});
      }
      if(u.pathname==='/api/history') {
        const all=rows.filter(row=>row[0]>after), samples=u.searchParams.has('tail')?rows.slice(-80):all.slice(0,80);
        return json({bootCount:1,oldestSeq:1,latestSeq:points.length,samples,more:samples.at(-1)?.[0]<points.length});
      }
      if(u.pathname==='/api/recipes')return json({recipes:[{id:'test-400',name:'Prova 400',steps:[
        {type:'ramp',target:400,rate:1},{type:'hold',target:400,duration:10}]}]});
      if(u.pathname.startsWith('/api/'))return json({bootCount:1,events:[],samples:[]});
      const file=u.pathname==='/jspdf.umd.min.js'?'vendor/jspdf.umd.min.js':u.pathname==='/'?'index.html':u.pathname.slice(1);
      return route.fulfill({contentType:file.endsWith('.js')?'application/javascript':file.endsWith('.css')?'text/css':'text/html',body:fs.readFileSync(path.join(root,'data',file))});
    });
    await page.goto('http://oven.test/');
    await page.screenshot({path:path.join(output,'dashboard-layout.png'),fullPage:true});
    await page.locator('#tabRecipes').click();
    const target=page.locator('#stepsEditor input[type="number"]').first();
    await target.waitFor({state:'visible'});
    assert.equal(await target.getAttribute('max'),'400');
    assert.equal(await target.inputValue(),'400');
    await page.screenshot({path:path.join(output,'programmi-layout.png'),fullPage:true});
    await page.locator('#tabReport').click();
    await page.locator('#exportReportImage').waitFor({state:'visible'});
    await page.waitForFunction(()=>!document.getElementById('exportReportImage').disabled);
    assert((await page.locator('#reportCurveMessage').textContent()).includes('289 campioni'));
    assert.equal(await page.locator('#reportPlot svg').count(),1);
    assert((await page.locator('#reportDefinitions').textContent()).includes('±1 °C'));
    r.version=2;r.holdBandC=5;
    await page.locator('#refreshReport').click();
    await page.waitForFunction(()=>document.getElementById('reportDefinitions').textContent.includes('±5 °C'));
    await page.screenshot({path:path.join(output,'report-screen.png'),fullPage:true});
    for(const [button,file] of [['exportReport','report.pdf'],['exportReportImage','curve.png'],['exportReportCsv','cycle.zip']]) {
      const pending=page.waitForEvent('download');await page.locator('#'+button).click();
      const download=await pending;await download.saveAs(path.join(output,file));
    }
    const png=fs.readFileSync(path.join(output,'curve.png'));
    assert.equal(png.subarray(1,4).toString(),'PNG');assert.equal(png.readUInt32BE(16),2400);assert.equal(png.readUInt32BE(20),1000);
    assert.equal(fs.readFileSync(path.join(output,'report.pdf')).subarray(0,4).toString(),'%PDF');
    await page.locator('#tabDashboard').click();
    const pending=page.waitForEvent('download');await page.locator('#exportSamples').click();
    await (await pending).saveAs(path.join(output,'dashboard.zip'));
    await page.setViewportSize({width:390,height:844});await page.locator('#tabReport').click();
    await page.screenshot({path:path.join(output,'report-mobile.png'),fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>window.innerWidth),false);
    await page.locator('#tabRecipes').click();
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>window.innerWidth),false);
    await page.screenshot({path:path.join(output,'programmi-mobile.png'),fullPage:true});
    await page.locator('#tabTest').click();
    await page.locator('#testEnter').scrollIntoViewIfNeeded();
    await page.screenshot({path:path.join(output,'test-controls-mobile.png')});
    // The fixed emergency action also works from TEST, blocks test entry, and needs acknowledgement.
    await page.locator('#globalStop').click();
    await page.waitForFunction(()=>latest?.emergency === true);
    assert(await page.locator('#testEnter').isDisabled());
    assert.equal(await page.locator('#globalStop').getAttribute('aria-pressed'),'true');
    await page.locator('#tabDashboard').click();
    await page.locator('#resetBtn').click();
    await page.waitForFunction(()=>latest?.emergency === false);
    // Following a reboot a saved summary cannot accidentally use another cycle's curve.
    r.curveAvailable=false;r.curveSamples=0;status.bootCount=2;
    await page.reload();await page.locator('#tabReport').click();
    await page.waitForFunction(()=>document.getElementById('reportCurveMessage').textContent.includes('non disponibile'));
    assert(await page.locator('#exportReportCsv').isDisabled());assert(!(await page.locator('#exportReport').isDisabled()));
    assert.equal(errors.length,0,errors.join('\n'));
    console.log('Browser: report pagination, PDF/PNG/CSV ZIP downloads, dashboard ZIP, mobile and reboot fallback passed.');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
