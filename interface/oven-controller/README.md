# Oven Controller Prototype

Per la versione **ESP32-S3 con due PT100 reali e server Wi-Fi**, vedere il [firmware di prova in sede](../../firmware/oven-controller-site/README.md) o il [test da banco](../../firmware/oven-controller-test/README.md). Questa cartella resta il prototipo browser con simulatore e ricette.

Prototipo della dashboard per un controllore forno basato su ESP32, PT1000 e SSR trifase.

## Funzioni già presenti

- Dashboard temperatura media, target, potenza e ΔT.
- Tre sensori PT1000 simulati.
- Grafico realtime target / temperatura reale.
- Ricette con step:
  - Ramp
  - Hold
  - Cooldown
- Ramping configurabile per ogni fase Ramp/Cooldown in °C/min.
- Editor ricette.
- Import/export JSON.
- START / PAUSA / STOP.
- Simulatore termico con inerzia del forno.
- Accelerazione simulazione 1× / 10× / 30× / 60×.
- Warning se la differenza tra i sensori supera 10 °C.
- Salvataggio delle ricette nel `localStorage` del browser.

## Come aprirlo in VS Code

1. Apri la cartella `oven-controller` con VS Code.
2. Metodo rapido: apri `index.html` nel browser.
3. Metodo consigliato: usa l'estensione **Live Server** e premi `Go Live`.

In alternativa, dal terminale dentro questa cartella:

```bash
python3 -m http.server 8000
```

Poi apri:

```text
http://localhost:8000
```

## Struttura

```text
oven-controller/
├── index.html
├── style.css
├── app.js
├── recipes.json
└── README.md
```

## Passaggio futuro a ESP32

La UI è volutamente separata dalla sorgente dati.

Nella versione hardware:

```text
PT1000 x3
   ↓
front-end RTD / MAX31865
   ↓
ESP32
   ├── controllo profilo
   ├── PID
   ├── time-proportioning SSR
   ├── safety
   └── WebSocket / HTTP
            ↓
        questa dashboard
```

Il simulatore presente in `app.js` verrà sostituito da dati reali ricevuti dall'ESP32.

## Nota sicurezza

Il prototipo software non costituisce una catena di sicurezza per un forno 415 V.
Nella macchina reale il sistema dovrà prevedere protezioni hardware indipendenti,
interblocco/contattore, sovratemperatura indipendente e cablaggio della parte di potenza
realizzato secondo le regole applicabili.
