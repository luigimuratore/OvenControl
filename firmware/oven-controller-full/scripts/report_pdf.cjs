// Reuse the dashboard's PDF, summary and step formatting without opening a page.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
global.jspdf = require('../data/vendor/jspdf.umd.min.js');
const Export = require('../data/report-export.js');
const source = fs.readFileSync(path.join(root, 'data/app.js'), 'utf8');
const context = vm.createContext({types:{ramp:'Ramp',hold:'Hold',cooldown:'Cooldown'}});
vm.runInContext(source.slice(source.indexOf('function clock('), source.indexOf('function temp(')) +
  source.slice(source.indexOf('const reportOutcomes'), source.indexOf('function updateReportStatus')), context);
function generate(report, points) {
  context.report = report;
  const summary = vm.runInContext('reportSummaryPairs(report)', context);
  const stepRows = vm.runInContext('reportStepRows(report)', context);
  const html = fs.readFileSync(path.join(root, 'data/index.html'), 'utf8');
  const explanation = html.match(/<p id="reportDefinitions"[^>]*>([^<]+)<\/p>/)[1]
    .replace('±5 °C', `±${report.holdBandC ?? (report.version >= 2 ? 5 : 1)} °C`);
  return Buffer.from(Export.pdf({report,points,summary,stepRows,explanation,
    curveMessage:!points.length ? 'Curva non disponibile; riepilogo conservato.' :
      `${points.length} campioni. ${report.curvePartial ? 'Curva parziale.' : 'Curva acquisita durante ciclo e pause.'}`}));
}
module.exports = {generate};
if (require.main === module) {
  try {
    const input = JSON.parse(fs.readFileSync(0, 'utf8'));
    if (!input.report?.key || !Array.isArray(input.points)) throw Error('Report non valido');
    process.stdout.write(generate(input.report, input.points));
  } catch (error) { console.error('Generazione PDF non riuscita.'); process.exitCode = 1; }
}
