/* Offline exports. jsPDF 4.2.1 is bundled locally; no network services are used. */
const OvenExport = (() => {
  const palette = {ink: '#203442', muted: '#536775', grid: '#dde5e9', target: '#4ba3ff', actual: '#36a982'};
  const escape = v => String(v).replace(/[&<>"']/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'}[c]));
  const elapsed = (ms, start) => (Number(ms) - Number(start)) >>> 0;
  const csvCell = v => /[,"\r\n]/.test(String(v)) ? `"${String(v).replace(/"/g, '""')}"` : String(v);
  const number = (v, n = 2) => Number.isFinite(v) ? v.toFixed(n) : '';
  function csv(points, {boot, offset = null, startMs = null} = {}) {
    const rows = ['boot,seq,uptime_ms,ora_stimata_iso,sonda1_c,sonda2_c,media_c,setpoint_c,pid_percento,comando_uscita,step,evento,tempo_ciclo_s'];
    const names = {16:'INIZIO CICLO', 32:'STOP', 64:'FINE CICLO', 128:'ALLARME'};
    for (const p of points) {
      const epoch = Number.isFinite(offset) ? offset + (startMs === null ? p.ms : startMs + elapsed(p.ms, startMs)) : NaN;
      rows.push([boot, p.seq, p.ms, Number.isFinite(epoch) ? new Date(epoch).toISOString() : '',
        number(p.t1), number(p.t2), number(p.actual), number(p.target), number(p.duty, 1), p.relay ? 1 : 0,
        p.marker & 0xe0 || !Number.isInteger(p.step) || p.step === 255 ? '' : p.step + 1,
        p.marker === 16 && p.operation === 'manual' ? 'INIZIO MANUALE' : names[p.marker] || '',
        startMs === null ? '' : (elapsed(p.ms, startMs) / 1000).toFixed(3)].map(csvCell).join(','));
    }
    return '\ufeff' + rows.join('\r\n') + '\r\n';
  }
  function chartModel(points, options = {}) {
    const start = options.startMs ?? points[0]?.ms ?? 0;
    const rows = points.map(p => ({...p, minutes: elapsed(p.ms, start) / 60000}));
    const values = rows.flatMap(p => [p.actual, p.target]).filter(Number.isFinite);
    const lo = values.length ? Math.min(...values) : 0, hi = values.length ? Math.max(...values) : 1;
    const tick = Math.max(1, Math.ceil((hi - lo + 2) / 10));
    return {rows, min: Math.floor((lo - 1) / tick) * tick, max: Math.ceil((hi + 1) / tick) * tick, tick,
      end: Math.max(1 / 60, options.durationMs / 60000 || 0, ...rows.map(p => p.minutes)),
      gapMinutes: (options.intervalMs || 10000) * 2.5 / 60000};
  }
  // Same geometry feeds on-page SVG, exported PNG and vector PDF.
  function paintChart(draw, points, options = {}, width = 1200, height = 500) {
    const compact=width<700, font=compact?12:16;
    const m = chartModel(points, options), box = {l:compact?46:76, t:compact?80:54, r:width-(compact?18:32), b:height-62};
    const x = v => box.l + v / m.end * (box.r-box.l);
    const y = v => box.b - (v-m.min) / (m.max-m.min) * (box.b-box.t);
    draw.text(18, 25, 'Temperatura (°C)', palette.ink, font);
    const legendY=compact?52:20, targetX=compact?18:width-330, actualX=compact?150:width-190;
    draw.line(targetX, legendY, targetX+30, legendY, palette.target, 3); draw.text(targetX+40, legendY+5, 'Setpoint', palette.ink, font);
    draw.line(actualX, legendY, actualX+30, legendY, palette.actual, 3); draw.text(actualX+40, legendY+5, 'Media PT100', palette.ink, font);
    for (let v=m.min; v<=m.max; v+=m.tick) {
      draw.line(box.l, y(v), box.r, y(v), palette.grid, 1);
      draw.text(box.l-12, y(v)+5, `${v}`, palette.muted, font, 'right');
    }
    const ticks=compact?3:6;
    for (let i=0; i<=ticks; i++) {
      const v=m.end*i/ticks;
      draw.line(x(v), box.t, x(v), box.b, palette.grid, 1);
      draw.text(x(v), box.b+28, v.toFixed(m.end<10 ? 1 : 0), palette.muted, font, 'center');
    }
    draw.text((box.l+box.r)/2, height-12, 'Tempo dall’inizio (min)', palette.ink, font, 'center');
    for (const [field, color] of [['target', palette.target], ['actual', palette.actual]]) {
      let previous = null;
      for (const p of m.rows) {
        if (!Number.isFinite(p[field])) { previous=null; continue; }
        const broken = !previous || p.minutes-previous.minutes>m.gapMinutes || (field==='target' && p.marker & 16);
        if (!broken) draw.line(x(previous.minutes), y(previous[field]), x(p.minutes), y(p[field]), color, 2.5);
        else draw.dot(x(p.minutes), y(p[field]), color, 2);
        previous=p;
      }
    }
    if (!points.length) draw.text((box.l+box.r)/2, (box.t+box.b)/2, 'Curva non disponibile', palette.muted, 20, 'center');
  }
  function svg(points, options = {}) {
    const width=Math.max(320,options.width || 1200), height=width<700?350:500;
    const parts = [`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="Setpoint e media PT100"><rect width="${width}" height="${height}" fill="#fff"/>`];
    paintChart({
      line:(x1,y1,x2,y2,c,w)=>parts.push(`<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="${c}" stroke-width="${w}"/>`),
      dot:(x,y,c,r)=>parts.push(`<circle cx="${x}" cy="${y}" r="${r}" fill="${c}"/>`),
      text:(x,y,t,c,s,a)=>parts.push(`<text x="${x}" y="${y}" fill="${c}" font-family="Arial,sans-serif" font-size="${s}" text-anchor="${a==='right'?'end':a==='center'?'middle':'start'}">${escape(t)}</text>`)
    }, points, options, width, height);
    return parts.join('')+'</svg>';
  }
  async function png(points, options = {}) {
    const canvas=document.createElement('canvas'); canvas.width=2400; canvas.height=1000;
    const c=canvas.getContext('2d'); c.scale(2,2); c.fillStyle='#fff'; c.fillRect(0,0,1200,500);
    paintChart({
      line:(x1,y1,x2,y2,color,w)=>{c.strokeStyle=color;c.lineWidth=w;c.beginPath();c.moveTo(x1,y1);c.lineTo(x2,y2);c.stroke();},
      dot:(x,y,color,r)=>{c.fillStyle=color;c.beginPath();c.arc(x,y,r,0,Math.PI*2);c.fill();},
      text:(x,y,t,color,s,a)=>{c.fillStyle=color;c.font=`${s}px Arial`;c.textAlign=a==='right'?'right':a==='center'?'center':'left';c.fillText(t,x,y);}
    }, points, options);
    return new Promise((resolve,reject)=>canvas.toBlob(b=>b?resolve(b):reject(Error('Immagine non generata.')), 'image/png'));
  }
  // A single ZIP avoids browser restrictions on multiple simultaneous downloads.
  function zip(files) {
    const encoder=new TextEncoder(), chunks=[], central=[]; let position=0;
    const crc32 = bytes => { let c=0xffffffff; for(const b of bytes) {c^=b;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);} return (c^0xffffffff)>>>0; };
    for(const file of files) {
      const name=encoder.encode(file.name), data=typeof file.data==='string'?encoder.encode(file.data):new Uint8Array(file.data);
      const crc=crc32(data), head=new Uint8Array(30+name.length), h=new DataView(head.buffer);
      h.setUint32(0,0x04034b50,true);h.setUint16(4,20,true);h.setUint16(6,0x800,true);
      h.setUint16(12,33,true);h.setUint32(14,crc,true);h.setUint32(18,data.length,true);h.setUint32(22,data.length,true);h.setUint16(26,name.length,true);head.set(name,30);
      const entry=new Uint8Array(46+name.length), e=new DataView(entry.buffer);
      e.setUint32(0,0x02014b50,true);e.setUint16(4,20,true);e.setUint16(6,20,true);e.setUint16(8,0x800,true);
      e.setUint16(14,33,true);e.setUint32(16,crc,true);e.setUint32(20,data.length,true);e.setUint32(24,data.length,true);e.setUint16(28,name.length,true);e.setUint32(42,position,true);entry.set(name,46);
      chunks.push(head,data);central.push(entry);position+=head.length+data.length;
    }
    const directorySize=central.reduce((n,c)=>n+c.length,0), tail=new Uint8Array(22), t=new DataView(tail.buffer);
    t.setUint32(0,0x06054b50,true);t.setUint16(8,files.length,true);t.setUint16(10,files.length,true);t.setUint32(12,directorySize,true);t.setUint32(16,position,true);
    return new Blob([...chunks,...central,tail],{type:'application/zip'});
  }
  // jsPDF's standard font supports Italian accents; normalize extra symbols.
  const pdfText = v => String(v).replace(/[–—−]/g,'-').replace(/→/g,' > ').replace(/Δ/g,'Delta ').replace(/Ω/g,'ohm').replace(/’/g,"'");
  function summaryGroups(summary) {
    return [
      ['Ciclo e tempi', 0, 7], ['Impostazioni di regolazione', 7, 10],
      ['Temperature delle sonde', 10, 15], ['Precisione della media rispetto al target', 15, 18],
      ['Comando PID', 18, 20], ['Qualità delle letture', 20, 22]
    ].map(([title,start,end])=>({title,pairs:summary.slice(start,end)}));
  }
  function phaseCurve(report, points, index) {
    const step=report.steps[index];
    const before=report.steps.slice(0,index).reduce((n,s)=>n+(Number(s.activeMs)||0)+(Number(s.pausedMs)||0),0);
    const startMs=(report.startUptimeMs+before)>>>0;
    const durationMs=(Number(step.activeMs)||0)+(Number(step.pausedMs)||0);
    return {startMs,durationMs,points:step.started ? points.filter(p=>elapsed(p.ms,startMs)<=durationMs &&
      (!Number.isInteger(p.step)||p.step===255||p.step===index)) : []};
  }
  function pdf({report:r, points, summary, stepRows, explanation, curveMessage}) {
    if (!globalThis.jspdf?.jsPDF) throw Error('Generatore PDF non disponibile: ricarica la pagina.');
    const doc=new globalThis.jspdf.jsPDF({unit:'mm',format:'a4',compress:true});
    doc.setProperties({title:`Report ciclo - ${r.recipeName}`,subject:r.key,creator:'OvenController Full'});
    const color=(hex,stroke=false)=>{const v=[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16));doc[stroke?'setDrawColor':'setTextColor'](...v);};
    const tones={white:'#ffffff',blue:'#edf5ff',mint:'#edf9f3',slate:'#f0f4f9',green:'#e9f7ee',amber:'#fff5df',red:'#fff0f0'};
    let y=0;
    const text=(value,x,at,size=10,bold=false)=>{doc.setFont('helvetica',bold?'bold':'normal');doc.setFontSize(size);color(palette.ink);doc.text(Array.isArray(value)?value.map(pdfText):pdfText(value),x,at);};
    const lines=(value,width,size=10,bold=false)=>{doc.setFont('helvetica',bold?'bold':'normal');doc.setFontSize(size);return doc.splitTextToSize(pdfText(value),width);};
    const fill=hex=>doc.setFillColor(...[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16)));
    function header(title,first=false) {
      if(!first)doc.addPage();
      fill('#f4f7fa');doc.rect(0,0,210,297,'F');
      fill('#ffffff');doc.rect(0,0,210,43,'F');
      fill('#387ba8');doc.rect(0,0,210,1.4,'F');
      text('OVEN CONTROLLER / REPORT DI CICLO',16,15,8,true);
      const titleLines=lines(title,178,first?21:17,true);text(titleLines,16,28,first?21:17,true);
      y=31+titleLines.length*(first?7:6);
      color(palette.grid,true);doc.line(16,y,194,y);y+=9;
    }
    const pairHeight=([label,value])=>lines(label,77,8).length*3.2+lines(value,77,10,true).length*4+4;
    function pairsHeight(pairs) {
      let height=0;
      for(let i=0;i<pairs.length;i+=2)height+=Math.max(pairHeight(pairs[i]),pairs[i+1]?pairHeight(pairs[i+1]):0);
      return height;
    }
    function drawPairs(pairs) {
      for(let i=0;i<pairs.length;i+=2) {
        const height=Math.max(pairHeight(pairs[i]),pairs[i+1]?pairHeight(pairs[i+1]):0);
        for(let col=0;col<2&&pairs[i+col];col++) {
          const [label,value]=pairs[i+col], x=22+col*89, labels=lines(label,77,8);
          text(labels,x,y,8);text(lines(value,77,10,true),x,y+labels.length*3.2+2,10,true);
        }
        y+=height;
      }
    }
    function panel(title,height,tone,paint) {
      if(y+height>275)header('Riepilogo / continua');
      const top=y;
      fill(tones[tone]||tones.white);color('#d7e2eb',true);doc.setLineWidth(.2);
      doc.roundedRect(16,top-4,178,height+2,2.5,2.5,'FD');
      fill('#387ba8');doc.roundedRect(20,top+.5,.9,4,0.4,0.4,'F');
      text(title.toUpperCase(),24,top+4,9,true);
      y=top+12;paint();y=top+height+4;
    }
    const pairsPanel=(title,pairs,tone='white')=>panel(title,16+pairsHeight(pairs),tone,()=>drawPairs(pairs));
    function drawPlot(curve,options) {
      const plotY=y, scale=166/1200;
      paintChart({
        line:(a,b,c,d,hex,w)=>{color(hex,true);doc.setLineWidth(w*scale);doc.line(22+a*scale,plotY+b*scale,22+c*scale,plotY+d*scale);},
        dot:(a,b,hex,rad)=>{fill(hex);doc.circle(22+a*scale,plotY+b*scale,rad*scale,'F');},
        text:(a,b,t,hex,size,align)=>{doc.setFont('helvetica','normal');doc.setFontSize(Math.max(8,size*scale*2.83465));color(hex);doc.text(pdfText(t),22+a*scale,plotY+b*scale,{align:align==='right'?'right':align==='center'?'center':'left'});}
      },curve,options);
      y+=500*scale+2;
    }
    function plotPanel(title,curve,options,note='') {
      const noteLines=note?lines(note,166,8):[];
      panel(title,16+500*166/1200+(noteLines.length?3+noteLines.length*3.2:0),'white',()=>{
        drawPlot(curve,options);if(noteLines.length)text(noteLines,22,y+2,8);
      });
    }
    const outcomes=r.operation === 'manual' ? {completed:'CONTROLLO MANUALE COMPLETATO',stopped:'CONTROLLO MANUALE FERMATO',fault:'CONTROLLO MANUALE IN ALLARME'} :
      {completed:'CICLO COMPLETATO',stopped:'CICLO FERMATO',fault:'CICLO IN ALLARME'};
    const outcomeTone=r.outcome==='completed'?'green':r.outcome==='fault'?'red':'amber';
    header(r.recipeName,true);
    const reason=lines(r.reason,166,9);
    panel(outcomes[r.outcome]||r.outcome,16+reason.length*4,outcomeTone,()=>text(reason,22,y,9));
    pairsPanel(r.operation === 'manual' ? 'La sessione in sintesi' : 'Il ciclo in sintesi',[summary[1],summary[2],summary[3],summary[6],summary[7],summary[8]],'blue');
    plotPanel('Andamento completo / target e media PT100',points,{startMs:r.startUptimeMs,durationMs:r.durationMs,intervalMs:r.curveIntervalMs},curveMessage);

    header('Riepilogo completo');
    for(const [index,group] of summaryGroups(summary).entries())
      pairsPanel(group.title,group.pairs,['blue','slate','mint','blue','slate','mint'][index]);
    const notes=lines(explanation,166,8);
    panel('Come leggere i dati',16+notes.length*3.4,'white',()=>text(notes,22,y,8));
    const labels=['Tempo attivo','Pause','Mantenimento in banda / richiesto','Errore medio assoluto',r.version>=3?'Superamento massimo media':'Superamento massimo sonda','Ritardo massimo','PID medio / al limite','Escursione PT100 in mantenimento'];
    for(const [index,row] of stepRows.entries()) {
      const phase=phaseCurve(r,points,index), step=r.steps[index];
      header(`Fase ${index+1} / ${row[0].replace(/^\d+\s*·\s*/,'')} / Target ${row[1]}`);
      text(row[2],17,y,9,true);y+=8;
      const pairs=labels.map((label,i)=>[label,row[i+3]]);
      pairsPanel('Tempi della fase',pairs.slice(0,3),'blue');
      const plotNote=!phase.points.length?(step.started?'Curva della fase non disponibile nello storico conservato.':'Fase non iniziata: nessun dato termico.'):'';
      plotPanel('Grafico della fase / target e media',phase.points,{startMs:phase.startMs,durationMs:phase.durationMs,intervalMs:r.curveIntervalMs},plotNote);
      pairsPanel('Precisione e comando / fase attiva',pairs.slice(3,7),'slate');
      const temperaturePairs=(step.stats?.sensors||[]).map((sensor,i)=>[`PT100 #${i+1} / minimo - massimo`,
        Number.isFinite(sensor.minC)&&Number.isFinite(sensor.maxC)?`${sensor.minC.toFixed(2)} - ${sensor.maxC.toFixed(2)} °C`:'Non disponibile']);
      if(step.type==='hold')temperaturePairs.push(pairs[7]);
      pairsPanel('Temperature / fase e sue pause',temperaturePairs,'mint');
      if(r.curvePartial)text('Storico parziale: alcuni campioni della fase potrebbero non essere disponibili.',17,Math.min(y+1,275),8);
    }
    const pages=doc.getNumberOfPages();
    for(let i=1;i<=pages;i++) {doc.setPage(i);color(palette.grid,true);doc.line(16,281,194,281);
      text(`Ciclo ${r.key} | ${r.firmware}`,16,287,8);text(`${i} / ${pages}`,181,287,8);}
    return doc.output('arraybuffer');
  }
  return {csv,svg,png,zip,pdf,chartModel,paintChart,elapsed,summaryGroups,phaseCurve};
})();
if(typeof module!=='undefined')module.exports=OvenExport;
