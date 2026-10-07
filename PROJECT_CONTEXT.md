# Contesto progetto — Forno Oriana

> Aggiornamento 28 settembre 2026: `firmware/oven-controller-test` usa CS1=14, CS2=10, SCK=13, MISO/SDO=12, MOSI/SDI=11, driver=4, **LED verde=7 e rosso=6** secondo il nuovo schema. I GPIO35/36 sono occupati dalla PSRAM octal. Il test ha target fisso 35 °C, PID in finestre di 5 s e spegnimento a 36 °C. Offre modalità sonde simulate per il collaudo senza PT100 e scheda Log dedicata; al riavvio torna sempre alle sonde reali. Il grafico mostra i campioni live circa ogni 0,5 s e conserva sull'ESP fino a sei ore di campioni ogni 10 s in RAM, ricaricabili dopo un refresh della pagina e scorribili nel tempo; un riavvio dell'ESP cancella quei campioni. Dopo un reset durante il test l'uscita resta spenta; con Wi-Fi capannone e NTP opzionali può mostrare l'intervallo massimo approssimativo dall'ultimo salvataggio dell'ora al riavvio, non la durata esatta del blackout. La mappa e i dettagli del vecchio prototipo riportati sotto sono storici; fare riferimento al [README del firmware di test](firmware/oven-controller-test/README.md).

Contesto recuperato il 23 settembre 2026 dalla [conversazione ChatGPT condivisa](https://chatgpt.com/share/6ab3f65e-221c-83eb-b813-d4eacd650990) e confrontato con i file presenti in questa cartella.

## Obiettivo

Realizzare un controllore programmabile per un forno industriale usato per la cura/post-cura termica di pezzi 3D.

Il sistema dovrà:

- eseguire ricette composte da rampe, mantenimenti e raffreddamenti;
- controllare il rateo di salita/discesa, per esempio 2 °C/min;
- lavorare fino a circa 200 °C;
- confrontare curva richiesta e temperatura reale;
- modulare la potenza tramite finestre temporali lente sull'SSR;
- mostrare stato, temperature, target, potenza e grafico su una dashboard;
- offrire in futuro interfaccia fisica, ricette per materiali e registrazione/esportazione dei cicli.

## Hardware identificato

### Forno e potenza

- Forno COVEN.
- Targhetta letta nella conversazione: 415 V 3N~, 50 Hz, 6,8 kW; è riportata anche una configurazione 240 V da verificare sulla macchina.
- Sono presenti resistenze elettriche interne e un contattore Siemens, ma il circuito di potenza effettivo non è ancora stato tracciato completamente.
- SSR trifase Carlo Gavazzi `RGC3A60D30KGE`:
  - comando A1+/A2-: 5–32 V DC;
  - commutazione trifase tramite L1/L2/L3 e T1/T2/T3;
  - zero-crossing, adatto al time-proportioning lento del riscaldamento.

La collocazione definitiva dell'SSR rispetto a protezioni, contattore e resistenze non è ancora una decisione chiusa. Deve essere verificata sullo schema reale del forno e realizzata da una persona qualificata per impianti a 415 V.

### Controllore

- Scheda `HW-678 V0.0.0` con modulo ESP32-S3-WROOM-1 N16R8.
- 16 MB flash e 8 MB PSRAM.
- USB nativa/JTAG rilevata in precedenza come `/dev/cu.usbmodem101`.
- Ambiente di sviluppo: VS Code + PlatformIO, framework Arduino.

### Sensori reali

La scelta iniziale di tre PT1000 è stata superata dall'hardware realmente disponibile:

- 2 sonde PT100, 3 fili, classe A;
- ogni sonda ha due fili rossi e uno bianco;
- rosso–rosso ≈ 0 Ω;
- bianco–rosso ≈ 110 Ω a temperatura ambiente;
- 2 MikroE RTD Click con MAX31865 integrato, predisposte per PT100;
- le due schede condividono il bus SPI e hanno Chip Select separati.

La conversazione propone bianco su `F+` e i due rossi su `R+`/`R-`, lasciando `F-` non usato nella configurazione a 3 fili. Prima del cablaggio definitivo questo punto va verificato sul manuale esatto della revisione RTD Click: nella chat sono comparse descrizioni non perfettamente coerenti dei quattro morsetti.

## Architettura concordata

```text
PT100 #1 ─→ RTD Click/MAX31865 ─┐
                                ├─SPI─→ ESP32-S3
PT100 #2 ─→ RTD Click/MAX31865 ─┘          │
                                           ├─ ricetta e setpoint nel tempo
                                           ├─ controllo temperatura
                                           ├─ dashboard Wi-Fi
                                           └─ GPIO4
                                                │
                                                ▼
                                           BC547B / driver 12 V
                                                │
                                                ▼
                                      A1+/A2- Carlo Gavazzi SSR
                                                │
                                                ▼
                                         circuito di potenza forno
```

Il controllo di potenza previsto è time-proportioning, non PWM veloce. Il firmware attuale usa una finestra di 5 secondi; il valore definitivo andrà validato sul forno e sull'SSR.

## Pin assegnati al prototipo

| Funzione | ESP32-S3 |
|---|---:|
| SPI MOSI → SDI delle RTD | GPIO11 |
| SPI SCK | GPIO12 |
| SPI MISO ← SDO delle RTD | GPIO13 |
| RTD Click 1 CS | GPIO10 |
| RTD Click 2 CS | GPIO9 |
| LED verde RUN | GPIO6 |
| LED rosso HEATING | GPIO7 |
| Comando driver SSR | GPIO4 |

Questa mappa è stata “congelata” nella conversazione per mantenere coerenti firmware e millefori, ma va ancora verificata sul pinout preciso della scheda HW-678 prima della saldatura definitiva.

## Driver a bassa tensione per l'SSR

Per il prototipo è stato scelto un BC547B come interruttore low-side:

```text
+12 V ───────────────────────────── A1+ Carlo Gavazzi
A2- Carlo Gavazzi ───────────────── C  BC547B
GPIO4 ── 5 kΩ ────────────────┬──── B
                              └─ 10 kΩ ── GND
GND ESP32 = GND alimentatore 12 V ── E
```

- Alimentatore suggerito per il prototipo: 12 V / 1 A; 2 A è accettabile ma non necessario per il solo ingresso SSR.
- Il +12 V non deve mai andare a 3V3, 5V o a un GPIO dell'ESP32.
- Con questo circuito non isolato il negativo del 12 V e il GND dell'ESP32 sono comuni.
- Pinout tipico BC547B TO-92, lato piatto verso l'osservatore e piedini in basso: C–B–E da sinistra a destra. Va verificato sul datasheet del componente effettivamente acquistato.
- Per una versione installata nel quadro è preferibile un'interfaccia galvanicamente isolata e un'alimentazione da quadro; il BC547B è la soluzione di prototipo, non la catena di sicurezza industriale finale.

## Millefori

Impostazione prevista:

- ESP32-S3 centrale e rimovibile su header femmina;
- USB-C rivolta verso il bordo;
- due RTD Click vicine tra loro, rimovibili, con morsetti PT100 verso l'esterno;
- bus rettilineo comune per 3V3, GND, SCK, MOSI e MISO;
- linee separate solo per CS1 e CS2;
- LED RUN e HEATING sul bordo frontale, ciascuno con 200 Ω;
- area vicina a GPIO4 riservata a BC547B, resistenze e morsetti;
- un morsetto 2 poli per ingresso `+12 V / GND` e uno per uscita `A1+ / A2-` verso l'SSR;
- nessun collegamento L1/L2/L3/T1/T2/T3 sulla millefori.

## Stato del software presente nella cartella

### `interface/oven-controller`

Prototipo browser autonomo già realizzato. Include:

- dashboard;
- grafico realtime;
- ricette Ramp/Hold/Cooldown ed editor;
- import/export JSON;
- START/PAUSA/STOP;
- simulatore con inerzia e velocità accelerata;
- warning sul delta fra sensori.

Questo progetto usa ancora il vecchio modello con **3 PT1000 simulate**.

### `oven-controller-esp32s3`

Progetto PlatformIO già caricato e provato sull'ESP32-S3. Dalla vecchia conversazione risulta verificato questo flusso:

```text
ESP32-S3 → rete Wi-Fi propria → web server → dashboard visibile dal telefono
```

Configurazione attuale:

- Access Point `OvenController`;
- password `oven12345`;
- indirizzo `http://192.168.4.1`;
- dashboard servita da LittleFS;
- ricetta demo hard-coded;
- tre temperature simulate;
- uscita GPIO4 che simula il comando SSR;
- finestra di comando da 5 s;
- limite software a 205 °C.

Il firmware attuale **non è ancora un controllore reale**:

- non legge i due MAX31865;
- non usa le due PT100 reali;
- mantiene ancora tre sensori simulati e un terzo CS su GPIO8;
- non contiene un PID vero: usa una semplice legge proporzionale nel simulatore;
- non gestisce ancora i LED RUN/HEATING su GPIO6/GPIO7;
- non ha ancora fault completi per sonda aperta/cortocircuitata, plausibilità, watchdog e arresto indipendente;
- `platformio.ini` include ArduinoJson ma non ancora una libreria MAX31865.

## Prossimo passo tecnico concordato

Prima di collegare SSR o forno:

1. collegare una sola RTD Click e una PT100 all'ESP32;
2. verificare configurazione e cablaggio 3 fili dal manuale della RTD Click;
3. aggiungere la libreria MAX31865 e ottenere una lettura stabile sul monitor seriale;
4. sostituire nella dashboard una temperatura simulata con la PT100 reale;
5. ripetere con la seconda RTD usando CS separato;
6. aggiungere diagnostica dei sensori e comportamento fail-safe;
7. solo dopo integrare LED e uscita BC547B, inizialmente con un carico di prova a bassa tensione;
8. progettare e verificare separatamente la parte di potenza del forno.

## Sicurezza e punti ancora aperti

- Il forno lavora a 415 V trifase e 6,8 kW: la parte di potenza non deve essere improvvisata né portata sulla millefori.
- Un SSR può guastarsi in conduzione; non può essere l'unica protezione.
- Servono protezioni, contattore/interblocco, arresto di emergenza e limitatore di sovratemperatura indipendente dal software.
- Rimangono da identificare il modello e la tensione della bobina del contattore Siemens e il cablaggio reale di resistenze, neutro, protezioni e SSR.
- Va deciso come usare le due temperature nel controllo: media, massimo per sicurezza e soglia massima di disaccordo.
- Ricette, PID, tempi di finestra e limite massimo devono essere tarati e collaudati sul forno reale.
