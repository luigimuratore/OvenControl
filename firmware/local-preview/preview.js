// Only the localhost server injects this file. Never embedded in ESP firmware.
(() => {
  "use strict";
  const mode = location.pathname.split("/")[1], prefix = `/${mode}`;
  const fetchOriginal = window.fetch.bind(window);
  window.fetch = (input, options) => fetchOriginal(typeof input === "string" && input.startsWith("/api/") ? prefix + input : input, options);
  const beaconOriginal = navigator.sendBeacon.bind(navigator);
  navigator.sendBeacon = (url, data) => beaconOriginal(url.startsWith("/api/") ? prefix + url : url, data);
  document.title = `Anteprima locale · ${document.title}`;
  const banner = document.createElement("aside");
  banner.style.cssText = "position:sticky;top:0;z-index:100;background:#302715;color:#ffe1a0;padding:16px 24px;border-bottom:2px solid #dca740;display:flex;gap:12px;align-items:center;flex-wrap:wrap";
  const text = document.createElement("strong");
  text.textContent = "ANTEPRIMA LOCALE · Dati e uscite simulati · Nessun ESP collegato";
  banner.append(text);
  for (const [path, label] of [["full", "Versione completa"], ["field-test", "Collaudo"]]) {
    const link = document.createElement("a");
    link.href = `/${path}/`; link.textContent = label;
    link.style.cssText = "color:#ffe1a0;text-decoration:underline;padding:6px";
    if (path === mode) link.setAttribute("aria-current", "page");
    banner.append(link);
  }
  const sensor = document.createElement("button");
  sensor.type = "button"; sensor.textContent = "Simula fault PT100 #2";
  let missing = false;
  sensor.onclick = async () => {
    sensor.disabled = true;
    try {
      const response = await window.fetch("/api/preview-sensor", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({missing:!missing})});
      if (!response.ok) throw Error("Simulatore non raggiungibile");
      missing = !missing;
      sensor.textContent = missing ? "Ripristina PT100 #2" : "Simula fault PT100 #2";
    } catch (error) { text.textContent = `ANTEPRIMA LOCALE · ${error.message}`; }
    finally { sensor.disabled = false; }
  };
  window.fetch("/api/status").then(r => r.json()).then(s => {
    missing = !s.sensors[1].valid;
    sensor.textContent = missing ? "Ripristina PT100 #2" : "Simula fault PT100 #2";
  }).catch(() => {});
  banner.append(sensor);
  const hint = document.createElement("small");
  hint.textContent = "Salvataggi e storico solo nella RAM del server. Risposta termica indicativa.";
  banner.append(hint);
  document.body.prepend(banner);
})();
