# Oven Controller · versione completa

Firmware in una **cartella dedicata**, per ESP32-S3 N16R8, due PT100 reali su MAX31865 e comando del driver di riscaldamento. Riprende dashboard, cablaggio e gestione programmi di `oven-controller-site`, aggiungendo diagnostica delle sonde, configurazione dell'attuatore e un supervisore delle uscite indipendente dal server web.

**Comandi da casa:** [aprire la dashboard via Tailscale/SSH](#dashboard-da-casa) · [compilare, copiare e caricare il firmware](#firmware-da-casa). La guida indica per ogni comando se eseguirlo sul **Mac** o nella **PowerShell Windows** e distingue la password Windows dalla password OTA.

La scheda **TEST** integra il collaudo di `oven-controller-field-test`: puoi verificare sonde, LED, driver e STOP nello stesso firmware, con **resistenze reali scollegate**. La dashboard esegue anche **cicli di riscaldamento**: passa al forno reale dopo aver verificato circuito di comando, cablaggio di potenza e protezioni indipendenti. Il firmware non legge corrente, tensione, feedback dei contatti o un interblocco fisico. Temperatura limite, arresto e sezionamento devono poter agire anche se ESP o attuatore si guastano. Una lettura valida o un LED spento non certificano il circuito di potenza.

## Funzioni disponibili

- Dashboard locale da telefono/PC, senza dipendenze web esterne, con schede **Dashboard / Manuale / Programmi / Parameters / TEST / Log / Report / Telegram**.
- Collaudo completo: LED, sequenze, relè da 0,1 s a 10 min, heartbeat, checklist, grafico PT100 e CSV diagnostico.
- Aggiornamenti Wi-Fi OTA con password, arresto di test/ciclo e blocco delle uscite durante la scrittura.
- Due PT100 reali: temperatura, resistenza calcolata, RAW, RREF e fault descritti.
- Fino a **8 programmi**, ciascuno con **12 step** di rampa, mantenimento e cooldown regolato dal PID.
- Editor, validazione, salvataggio in memoria ESP e importazione/esportazione JSON.
- PID con anti-windup e derivata filtrata, coefficienti modificabili e persistenti.
- Scelta **SSR / relè meccanico**, finestra di comando e limite di tempo ON configurabili e persistenti.
- Avvio, pausa, ripresa e STOP; STOP disponibile anche nelle schede Programmi e Log.
- Limiti su sonde, temperatura, sovraelongazione e freschezza del controllo; differenza sonde indicata a colori.
- Report automatico di fine ciclo: esito, tempi e pause, temperature, errori sul setpoint, hold in banda, configurazione PID/attuatore e download PDF/JSON e curva CSV/PNG; ultimo riepilogo persistente.
- Grafico live, navigazione dello storico di circa 12 ore, marcatori di inizio/fine/STOP/allarme e CSV completo.
- Log JSON e seriale; fino a **40 eventi principali** restano dopo un riavvio, secondo lo spazio NVS disponibile.
- Rete Wi-Fi diretta più rete del capannone, mDNS, NTP, contatore avvii e rilevazione ciclo interrotto.
- Diagnostica chip ESP, RAM libera/minima, RSSI e età delle letture.
- Bot Telegram opzionale: `/status` dalla chat privata autorizzata e notifiche di avvio, pausa/ripresa, STOP, completamento, emergenze, allarmi e riavvio.

## Ottimizzazione e verifica — full-1.21

Le funzioni di full-1.20 rimangono disponibili: programmi e manuale, PID e discesa regolata, pausa con mantenimento, RESET & BLOCK / CONTINUE, TEST, report PDF/CSV/PNG/JSON e bot multiutente con modalità personali. Le impostazioni e i registri NVS esistenti conservano formato e chiavi. Nessun aggiornamento viene caricato dall'esecuzione dei test.

| Intervento | Comportamento |
| --- | --- |
| HTTP separato dal controllo | Un task di rete gestisce fino a 3 connessioni con intestazioni/corpi limitati, lettura incrementale e invio socket senza attese. Solo richieste complete entrano nel loop; solo il loop legge/modifica lo stato del forno. POST precede le letture. Richieste in attesa oltre 1,5 s non avviano azioni tardive; STOP, EMERGENZA e uscita da TEST restano accettati. |
| Letture PT100 | Due conversioni one-shot in parallelo, con lo stesso calcolo Adafruit. Le attese iniziali di full-1.21 (10/65/1 ms) sono state corrette in full-1.22 come descritto nella sezione sulle sonde. Il campionamento periodico avanza senza `delay`; avvio/ripresa/reset richiedono ancora una coppia appena completata. I recuperi restano limitati a 10 fault consecutivi e richiedono una nuova coppia valida e sicura; uscite inibite durante la verifica. |
| Memoria | Storico dashboard da 4.320 campioni in PSRAM, come gli storici TEST/report. Se manca, riduzione esplicita dello storico con avviso e capienza esposta dall'API. Buffer plot distinto da quello PDF. |
| Trasferimenti | HTML/CSS/JavaScript incorporati in gzip deterministico. PNG `/plot` compresso senza perdita con memoria limitata, senza servizi esterni. I sorgenti in `data/` e l'anteprima Python restano normali file leggibili. |
| Persistenza | Checkpoint tentato al massimo ogni 30 s anche se NVS fallisce. Un fallimento di salvataggio a fine ciclo produce stato, report e marcatore di allarme coerenti. |
| Pulizia | Eliminati campi inutilizzati nel piano GPIO; driver, framing HTTP, compressione e storage durevole isolati in moduli verificabili. Dipendenze principali fissate alle versioni compilate. Corretta la versione firmware nell'API report. |
| Orologio dashboard | Uptime esteso a 64 bit e ricostruzione dei timestamp live/storico/TEST attraverso il ritorno a zero di `millis()` dopo circa 49,7 giorni. I report mantengono la propria linea temporale. |

Confronto delle compilazioni locali full-1.20 → full-1.21: **RAM statica 193.820 → 73.532 byte** (59,1% → 22,4%). HTML/CSS/JS principali: **165.793 → 48.592 byte** trasferiti con gzip. Il grafico nativo di regressione passa da circa **461 kB a 24.864 byte**, con raster identico verificato tramite decompressione indipendente. Queste misure non sono una misura della RAM dinamica o dei tempi reali del forno; questi ultimi vanno controllati sull'ESP.

### Coda Telegram persistente

Il task Telegram salva eventi ordinari/allarmi e snapshot dei report con la curva, senza effettuare scritture filesystem nel loop di controllo. Usa esclusivamente file `/ovtg*.dat/.ack/.tmp` nella partizione SPIFFS già prevista dal layout da 16 MB; non modifica partizioni, programmi, PID o registro blackout NVS. La partizione viene inizializzata automaticamente **solo se interamente cancellata**. Una partizione non vuota che non si monta viene lasciata intatta e si continua con le code RAM.

- Fino a **96 record** durevoli complessivi; restano i limiti di ammissione dei report e delle code RAM. I dati diventano durevoli quando il worker ha completato il salvataggio: un taglio prima di quel punto può ancora perdere l'ultimo evento.
- Record versionati con checksum, destinatari numerici e proprietario associato. A ogni invio viene ricontrollata l'autorizzazione attuale; la revoca impedisce anche gli invii ancora pendenti.
- Gli avvisi ordinari scadono dopo un'ora e rispettano la modalità personale attuale. Gli allarmi restano pendenti fino alla consegna. Risposte ai comandi e grafici richiesti con `/plot` non vengono riprodotti dopo un riavvio.
- I PDF pendenti vengono ricostruiti dallo snapshot salvato, con lo stesso nome e gli stessi dati. Con orario di fine noto, la scadenza rimane 24 ore dalla fine del ciclo; altrimenti si usa l'ora del salvataggio o, senza orario noto, il limite della sessione corrente.
- Prima di completare l'upload PDF viene salvata l'intenzione di invio per quel destinatario. Dopo un reset con esito ambiguo **quel destinatario viene saltato**, con avviso silenzioso, mentre si prosegue con gli altri. Una risposta Telegram 429 permette il ritentativo. È la stessa scelta conservativa usata per una conferma di rete persa.
- Le notifiche testuali privilegiano la consegna: un taglio tra conferma Telegram e cancellazione del record può ripetere un singolo messaggio. Non esiste una garanzia di consegna esattamente una volta nella Bot API.
- Storage pieno/non disponibile: dati ancora nelle code RAM, ritentativi di salvataggio distanziati; nessun arresto o riavvio automatico del controllo per l'outbox. Le chiavi API `telegram.outboxReady`, `outboxRecords`, `outboxErrors` espongono lo stato. `http.running/served/rejected` mostrano il nuovo server.

### Pipeline locale ripetibile

Prerequisiti: Python 3, compilatore C++17, Node.js; PlatformIO per le due compilazioni finali. Dalla cartella `firmware/oven-controller-full`:

```bash
# Test Python + anteprima, C++ con AddressSanitizer/UndefinedBehaviorSanitizer,
# dashboard/report JavaScript e controllo degli asset incorporati.
python3 scripts/check_all.py

# Stessi test più compilazione USB e OTA. NON esegue upload.
python3 scripts/check_all.py --build
```

La pipeline copre programmi, mantenimento, PID, pause, cooldown, manuale, ripresa da allarme, supervisore, TEST/heartbeat, sonde, blackout/NVS, Wi-Fi, autorizzazioni e invii Telegram, HTTP frammentato, scritture interrotte dell'outbox e round-trip di PNG/gzip. Non invia messaggi Telegram reali. Le prove native e l'anteprima non sostituiscono il collaudo sull'ESP: prima dell'uso reale della nuova versione verificare letture RAW/temperature, STOP/emergenza e temporizzazione uscita, poi un ciclo sorvegliato e una prova di riavvio con allegato pendente. L'aggiornamento rimane esplicito e a forno fermo.

## Controllo manuale

Nella scheda **Manuale**, accanto a **Dashboard**, trovi temperature, grafico e **Controllo manuale**. Le due schede usano lo stesso storico del forno, con comandi distinti. Serve a raggiungere e mantenere una temperatura per il tempo deciso dall'operatore, senza salvare un Programma o eseguire step automatici.

1. Imposta **Temperatura target** da **0 a 400 °C** e **Rate rampa** (minimo **0,1 °C/min**, senza massimo software), conferma **CHECK HARDWARE/AMBIENTE EFFETTUATO** e premi **AVVIA MANUALE**.
2. Per passare da T1 a T2, modifica target e/o rate e premi **APPLICA TARGET**. Il setpoint avanza al rate scelto e resta sul target finale, senza una durata automatica. La sessione, il tempo trascorso e il grafico continuano. Se il nuovo target è sotto la temperatura attuale, il riferimento scende al rate scelto: il PID può dare calore sotto la curva, mentre sopra la curva raffredda naturalmente. Poi mantiene il nuovo target.
3. Imposta **Target cooldown** (default **25 °C**, non superiore al setpoint attuale) e premi **COOLDOWN**: segue la discesa del riferimento con il PID. Può dare calore quando la media PT100 è sotto la curva; a media uguale o superiore al setpoint il comando è zero. Al termine della rampa, quando la sonda più calda è entro target +1 °C, spegne il riscaldamento e resta in monitoraggio fino a STOP. **APPLICA TARGET** può riattivare la regolazione.
4. **STOP MANUALE** chiude la sessione e lascia l'uscita spenta. Il fungo **EMERGENZA** chiude anche il manuale e blocca nuovi avvii fino al riconoscimento.

Il manuale usa gli stessi **PID, attuatore, finestra SSR e limite di potenza** impostati nella scheda Parameters. Queste impostazioni restano bloccate durante una sessione. La rampa parte dalla media PT100 all'avvio. Durante la regolazione, modificare target/rate mantiene continuo il setpoint; ripartire da un cooldown usa la temperatura attuale. Da **full-1.11** la discesa è regolata: il PID può rallentare il raffreddamento naturale usando le resistenze, ma non può accelerarlo. Nel cooldown manuale, raggiunta la fine della rampa e la temperatura finale, il riscaldamento resta spento anche se in seguito la temperatura scende ancora. Programmi, manuale e TEST sono esclusivi: termina l'attività corrente prima di passare all'altra. Da full-1.16 né Programmi né Manuale hanno un tempo massimo di ciclo; restano attivi i controlli su sonde, letture scadute, sovratemperatura e supervisore delle uscite.

Il controllo continua se chiudi la pagina o perdi il Wi-Fi. Dopo reset o blackout **non riparte automaticamente**: lo stesso registro di sicurezza salva l'attività «Controllo manuale», blocca le uscite al riavvio e richiede riconoscimento e nuovo avvio. Telegram `/status` mostra il manuale, il target, il raffreddamento e il tempo trascorso; avvio, cambio target, cooldown e STOP seguono la modalità notifiche scelta, mentre gli allarmi restano prioritari.

Il manuale non genera report PDF di ciclo e non sostituisce l'ultimo report dei Programmi. Puoi scaricare la curva dalla Dashboard con **Scarica CSV + grafico**: i campioni manuali hanno la colonna step vuota e il marcatore **INIZIO MANUALE**. Per i colori del riquadro modifica `--manual-control-bg` e `--manual-control-border` in `data/style.css`; pulsanti e indicatore di stato sono nelle regole `.manual-buttons` e `.manual-state`.

API: `POST /api/manual` con `action: "start"` e `targetC`, `rateCMin`, `hardwareReady: true`; `action: "target"`, `targetC` e `rateCMin` per modificarlo; `action: "cooldown"`, `targetC` e `rateCMin`, oppure `action: "stop"`. `/api/status` espone `operation` e `manual` (`active`, `mode`, `targetC`, `setpointC`, `rateCMin`, `coolingToTarget`, `cooldownComplete`, `elapsedSec`, `startedAtMs`). `cooldownRegulated: true` indica il nuovo comportamento con correzione PID in discesa. I target sono validati anche sul firmware; non si può avviare il manuale durante un programma, TEST, OTA o un blocco di sicurezza.

## Anteprima Python locale per i check

Per controllare la dashboard sul Mac **senza ESP**, avvia il [server locale Python](../local-preview/README.md). Richiede Python 3, senza pacchetti aggiuntivi. Dal terminale:

```sh
cd "/Users/gigi/Documents/PROJECTS/Forno oriana/firmware/oven-controller-full"
python3 ../local-preview/server.py --port 8080
```

Apri **<http://localhost:8080/full/>** nel browser. Lascia aperto il terminale mentre fai i check; premi **Ctrl+C** per fermare il server. Puoi aggiungere `--open` al comando per aprire automaticamente il browser. Se la porta 8080 è occupata, usa `--port 8081` e apri `http://localhost:8081/full/`.

Questa anteprima serve per controllare l’interfaccia: temperature e comandi sono simulati, non comunica con l’ESP e non aziona le uscite del forno. I salvataggi restano nella RAM del server e si perdono quando lo riavvii. Non usarla per valutare il PID o le prestazioni termiche reali.

## Caricamento e rete

In fondo alla pagina, a sinistra, la dashboard mostra la **versione del firmware in esecuzione** e la **data dell'ultima modifica** ricevute da `/api/status`. Versione e data si aggiornano in `include/firmware_version.h` (`kVersion` e `kModifiedOn`, data `AAAA-MM-GG`); la data non dipende da compilazione, NTP o riavvio. Per font, colore e spaziatura modifica `.firmware-footer` in `data/style.css`. L'anteprima indica esplicitamente «Anteprima locale» e legge gli stessi metadati; firmware precedenti senza data mostrano «non disponibile».

Apri questa cartella in PlatformIO. Dal terminale nella cartella `firmware`:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

Il caricamento sostituisce il firmware della scheda collegata. Non serve un upload separato del filesystem: HTML, CSS e JavaScript sono inclusi nella flash.

| Accesso | Valore |
|---|---|
| Rete diretta | `OvenController-Full` |
| Password | `oven12345` |
| Dashboard diretta | `http://192.168.4.1/` |
| Nome mDNS sulla LAN, se supportato | `http://oven-full.local/` |

Il file locale `include/wifi_config.h` è ripreso da `oven-controller-site`. Puoi modificarlo per SSID/password del capannone e server NTP; se manca, copialo da `include/wifi_config.example.h`. La rete diretta resta disponibile. La dashboard mostra anche l'IP sulla rete del capannone. La dashboard non ha login: usa una LAN controllata. Telegram è opzionale e richiede accesso a Internet. NTP serve a datare gli eventi e a stimare l'intervallo di interruzione; il controllo termico usa `millis()` e funziona anche senza Internet.

La configurazione Wi-Fi viene applicata in RAM a ogni avvio dalle credenziali incluse nel firmware. Se dopo il passaggio dal field-test compare `NOT_ENOUGH_SPACE` seguito da `softAP(): set AP config failed`, carica questa versione aggiornata normalmente: non serve cancellare tutta la flash. All'avvio, e prima di salvare nuovi log, vengono eliminati i log full più vecchi quando necessario per riservare spazio alle impostazioni e allo stato di sicurezza. Programmi, PID, attuatore e dati degli altri firmware non vengono cancellati. Se lo spazio resta insufficiente, gli eventi restano in RAM e il log seriale lo segnala; il Wi-Fi usa comunque la configurazione in RAM.

### Accesso e recupero Wi-Fi

**Connessione Wi-Fi e accesso alla pagina sono due verifiche diverse.** Se la dashboard indica **Connesso** con un IP del capannone, l'ESP ha già ottenuto un indirizzo dal router. Apri l'IP attuale mostrato, con `http://`: dopo giorni di spegnimento il DHCP può assegnare un indirizzo diverso da quello salvato nei preferiti. Per un accesso stabile configura nel router una **prenotazione DHCP per il MAC dell'ESP**; IP e MAC sono mostrati in **Accesso alla dashboard**. Il nome `oven-full.local` dipende dal supporto mDNS della rete e del dispositivo: usa l'IP se non viene risolto.

Per la rete diretta collegati davvero a **OvenController-Full**, mantieni la connessione anche se il telefono segnala “nessuna Internet” e apri **http://192.168.4.1/**. Se il telefono devia verso la rete mobile o una VPN, disattivali temporaneamente per questa verifica. La presenza dell'SSID nell'elenco da sola non conferma che il telefono lo stia usando. Questa rete diretta non fornisce accesso a Internet.

Il firmware mantiene la riconnessione della libreria e aggiunge un supervisore in un **task separato**, senza attese nel loop PID. Lascia **30 s** alla prima associazione/DHCP; dopo la perdita di una connessione funzionante attende **5 s**. Se l'ESP resta senza IP valido, ritenta a intervalli di **30, 60 e poi 120 s**, senza esaurire i tentativi. Ogni quarto tentativo riprova applicando le credenziali del firmware, con canale/BSSID automatici. Non scollega una connessione sana, non spegne la radio o la rete diretta già attiva, non cancella credenziali/NVS e non riavvia l'ESP. I tentativi si sospendono durante OTA. Il supervisore non comanda uscite o ripresa dei cicli; i blocchi dopo reset/blackout restano in vigore.

La dashboard mostra **MAC, tentativi di recupero, prossimo tentativo e ultimo errore** quando la rete del capannone non è connessa. `GET /api/status` aggiunge `stationMac` e `wifi` con `recoveryRunning`, `recoveryPaused`, `statusCode`, `retryCount`, `retryInSec`, `lastDisconnectReason` e `lastDisconnectMessage`. Il log riporta gli errori e i tentativi in RAM/seriale, evitando scritture ripetute in flash. `retryCount` conta i tentativi del supervisore, oltre a quelli interni della libreria. Il codice **201** indica rete non trovata; **202** autenticazione rifiutata (password o compatibilità di sicurezza); **200** timeout dei beacon/segnale. Non sono diagnosi definitive del componente o della causa fisica. Il normale distacco richiesto da un tentativo di recupero non sostituisce l'ultimo errore utile. Se Wi-Fi funziona ma NTP iniziale non riesce, la sincronizzazione viene riavviata ogni **60 s** finché non è confermata.

Riferimenti: [riconnessione e cause di disconnessione ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32s3/api-guides/wifi.html), [implementazione Arduino 2.0.17](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/WiFi/src/WiFiGeneric.cpp). L'auto-reconnect della libreria gestisce solo alcune cause: il supervisore recupera anche gli altri casi e le attese per DHCP.

**Prova dopo il caricamento, con riscaldamento fermo:** attendi la connessione, rendi indisponibile il solo router/rete del capannone, verifica dalla rete diretta il conto dei tentativi, poi ripristina il router e controlla connessione, nuovo IP e NTP. Non serve un riavvio dell'ESP. Un fallimento persistente va diagnosticato dal codice d'errore, dal log e dalle impostazioni del router; i ritentativi non correggono password errate o assenza di copertura.

## Collegamenti

Per il Carlo Gavazzi trifase indicato nelle note come **RGC3A60D30KGE**, vedi la [nota sul modello e sulla configurazione SSR](../docs/carlo-gavazzi.md). La sigla recentemente comunicata `RQC3A60D30KQE` resta da verificare sulla targhetta. Se il componente è il RGC3A60D30KGE, seleziona **SSR** nelle impostazioni di comando; il valore iniziale generico “relè meccanico” non identifica il componente installato.

| Funzione | GPIO |
|---|---:|
| MAX31865 #1 / #2 · CS | 14 / 10 |
| SPI · SCK / MISO / MOSI | 13 / 12 / 11 |
| Comando transistor tramite 5 kΩ | 4 |
| LED rosso / verde | 6 / 7 |

Click a **3,3 V**, con GND comune. SDO delle Click va al MISO ESP, SDI al MOSI. Mantieni il circuito di polarizzazione del transistor, compresa la resistenza verso GND per lo stato spento all'avvio. GPIO35/36 restano esclusi perché occupati dalla PSRAM sul modulo N16R8.

Pin e polarità sono in `include/hardware_config.h`, ripreso dal firmware di collaudo. Valori iniziali: **PT100 100 Ω nominali**, **RREF 470 Ω per ciascuna Click**, **3 fili**, uscite attive alte. Verifica RREF e ponticelli effettivi delle Click: la conversione può sembrare valida anche con una RREF errata. Per cambiare il numero di fili modifica entrambe le chiamate `begin(MAX31865_3WIRE)` in `src/main.cpp` e adegua il cablaggio. Filtro MAX31865 a **50 Hz**, bias disabilitato dopo ogni acquisizione.

Il verde indica server/rete avviati senza allarme. Il rosso segue il **comando** di riscaldamento GPIO4. Nella scheda **TEST** il rosso può essere provato separatamente: controlla il riquadro **Comando relè** per conoscere GPIO4.

## Scheda TEST · collaudo nello stesso firmware

Le prove sono disponibili solo a ciclo fermo: anche **PAUSA** blocca TEST. Premi direttamente una prova per attivare la modalità TEST, oppure **Modalità diagnostica** per leggere soltanto le sonde. In questa modalità PID e programmi sono esclusi, e fault/assenza delle sonde o ΔT non interrompono le prove delle uscite. Le temperature diagnostiche ammesse sono −50..430 °C; tornando al controllo termico restano i limiti e gli allarmi del full. Gli allarmi già presenti non vengono riconosciuti o cancellati dai test.

- **LED:** verde, rosso, entrambi o spenti per 3 s; sequenza spenti → verde → rosso → entrambi, 2 s per stato, 8 s totali. GPIO4 resta spento.
- **Relè/SSR:** impulsi 0,1 / 0,5 / 1 / 2 / 5 s oppure accensione continua 30 s / 1 min / 2 min / 10 min. Sequenza di 3 impulsi da 1 s, con intervalli da 2 s OFF. Il rosso segue GPIO4.
- **Arresto:** STOP nella scheda TEST ferma le prove; lo STOP globale ferma anche un ciclo. Una sola prova alla volta. A fine prova o dopo STOP puoi avviarne subito un'altra.
- **Heartbeat:** soltanto la pagina che avvia la prova riceve il token e lo rinnova circa ogni 0,7 s. Dopo 2,5 s senza heartbeat, il supervisore a 10 ms spegne l'uscita. Altre pagine possono osservare e fermare la prova, ma non mantenerla attiva. Nascondere la pagina, bloccare il telefono o uscire dalla scheda TEST arresta la propria prova; uscire dalla scheda ferma e chiude anche la propria modalità TEST. Questo comportamento riguarda soltanto i test, non il ciclo autonomo.
- **Diagnostica:** temperature e ΔT, resistenza, RAW, RREF, fault descritti, LED/comando relè separati, rete, OTA, heap libero/minimo, RSSI, chip ESP, avvii e prova interrotta al reset.
- **Dati:** grafico delle due PT100 sugli ultimi 5 minuti; storico diagnostico separato fino a 3600 campioni / circa 1 ora, a 1 s, con CSV completo di RAW/fault e comandi GPIO. Il ring usa PSRAM; senza PSRAM si riduce a 300 campioni, o si disabilita se manca anche la RAM di fallback, con capacità esposta dall'API e avviso nel log. Lo storico ciclo di circa 12 ore resta disponibile nella Dashboard. Gli impulsi più brevi di 1 s possono comparire soltanto nel log.
- **Checklist e log:** verifiche manuali sul cablaggio, sonde, LED, driver, STOP e riavvio; la checklist si azzera ricaricando la pagina. Log condiviso di 128 eventi in RAM, export JSON, con i principali eventi persistenti del full. Il flag di prova interrotta resta leggibile dopo un reset; nessuna prova riparte automaticamente.

Premi **Esci da TEST** prima di avviare un programma o riconoscere un allarme. Le prove manuali comandano direttamente il driver: finestra PID e limite percentuale di comando si applicano ai cicli, non alle accensioni di collaudo. Esegui le prove con resistenze reali scollegate, usando gli strumenti di verifica previsti nel [collaudo sul campo](../oven-controller-field-test/README.md#procedura-per-il-collaudo).

API diagnostiche: `POST /api/test/command` (`enter`, `exit`, `stop` e le azioni LED/relè), `POST /api/test/heartbeat` con `token`, `GET /api/test/history`. `/api/status` include l'oggetto `test`, gli stati `relay`/`red`/`green` e lo stato OTA. STOP inviato all'API dei test non arresta un programma partito da un'altra pagina; `POST /api/command` con `stop` è lo STOP globale.

## Telegram · configurazione e prova

### Utenti autorizzati dalla dashboard — full-1.20

La nuova scheda **Telegram**, accanto a Parameters, gestisce fino a **8 utenti totali**, incluso il proprietario già configurato in `include/telegram_config.h`. Il token resta nel file privato: non viene mostrato dalla dashboard. Il link del bot viene ricavato con `getMe` quando Wi-Fi, NTP e Telegram sono disponibili.

Per autorizzare una persona:

1. Apri **Telegram** nella dashboard e condividi il link mostrato.
2. La persona apre la chat privata e preme **AVVIA** o invia `/start`.
3. In **Richieste di accesso** compare il suo nome, l'eventuale `@username` e l'ID Telegram. Premi **Autorizza** e scegli la modalità iniziale.
4. La persona riceve la conferma e può usare `/status`, `/plot`, `/help`, `/start` e `/modalita`.

Le richieste non autorizzano alcuna lettura del forno. Restano in RAM per **10 minuti**, poi occorre inviare nuovamente `/start`; puoi anche rifiutarle. L'accesso approvato è legato all'ID stabile, quindi cambiare `@username` non cambia l'autorizzazione. Per evitare associazioni errate, verifica nome e username della richiesta prima di approvarla.

**Ogni utente ha una modalità personale**, salvata con l'elenco in un unico record NVS `tgUsers`: cambiarla dalla chat o dalla dashboard non modifica quella degli altri. Allarmi e avvii previsti restano visibili a tutti, i PDF vengono inviati a tutti gli autorizzati e gli avvisi sonda recuperata restano solo in Dettagliata. I messaggi ordinari e i documenti restano silenziosi. `/status` e `/plot` rispondono soltanto a chi li richiede. Il PDF viene generato una volta e consegnato alle chat una alla volta; un destinatario che blocca il bot non impedisce gli invii agli altri. Resta un solo plot in preparazione alla volta: se è occupato, il bot invita a riprovare.

**Revoca accesso** esclude l'utente dai nuovi comandi e dagli invii ancora da iniziare. Un invio già in corso può completarsi; i messaggi già presenti nella sua chat non vengono cancellati. Il proprietario compilato nel firmware non è rimovibile dalla dashboard. Un salvataggio NVS fallito mantiene le autorizzazioni precedenti; un elenco persistente illeggibile/non valido ripristina l'accesso del solo proprietario. La prima installazione migra la precedente preferenza `tgMode` del proprietario; gli altri utenti partono in Sintetica salvo scelta diversa all'approvazione.

La gestione utenti usa gli stessi accessi locali/VPN della dashboard; non aggiunge una seconda password HTTP. Dal bot nessun utente può comandare riscaldamento, STOP, emergenza o modifiche ai parametri del forno. Non eseguire contemporaneamente il lettore Python `telegram_tool.py --preview` e il firmware con lo stesso bot: contenderebbero i messaggi Telegram.

Nell'**anteprima Python locale**, la scheda mostra **Simula /start di un nuovo utente**: puoi provare approvazione, modalità e revoca senza inviare messaggi reali. Lo script Telegram Python rimane una prova a singola chat; il flusso multiutente reale è nel firmware ESP.

API: `GET /api/telegram/users` restituisce stato del bot, link, utenti e richieste. `POST` sullo stesso percorso accetta `action: "approve" | "reject" | "remove" | "mode"`, `chatId` come **stringa decimale** e `mode: "essenziale" | "sintetica" | "dettagliata"` dove applicabile. Le approvazioni richiedono una richiesta fresca già presente, non accettano un semplice username. Nessun token è incluso nelle risposte. Riferimenti: [avvio della chat del bot](https://core.telegram.org/bots/features#deep-linking), [getMe](https://core.telegram.org/bots/api#getme).

Il bot risponde a **`/status`**, **`/plot`**, **`/start`**, **`/help`**, **`/modalita`** e ai tre comandi di modalità nelle chat private autorizzate, verificando anche il mittente. `/start` mostra l'aiuto agli autorizzati e crea una richiesta per i nuovi utenti; non avvia un programma. Non esegue comandi su uscite, PID, programmi o riconoscimento emergenze. I messaggi usano **🚀** per avvio ciclo, **⏸️** per pausa, **⏯️** per ripresa, **✅** per lo stato regolare e il completamento, **⚠️** per STOP e interruzioni, **🚨** per emergenze e allarmi. `/status` divide i dati aggiornati in blocchi: stato, ciclo/tempi, temperature, uscita, regolazione, eventuali errori registrati e sistema. **Programma, step, target e tempi compaiono soltanto durante un ciclo avviato o in pausa**; a ciclo fermo il programma selezionato in dashboard e i programmi precedenti restano esclusi. Le notifiche di fine ciclo, STOP e allarme possono invece indicare il programma a cui si riferisce l'evento. Sono riportati anche entrambe le PT100/media/differenza, età letture, GPIO/PID, attuatore/limite, uptime, avvio, RAM e RSSI.

**Ora italiana:** da full-1.16 il campo `Aggiornato` di `/status` usa Europe/Rome, con cambio automatico tra ora solare e legale. Non mostra più UTC come se fosse l'ora locale; il timestamp è quello dello snapshot dello stato, non un vecchio evento.

**Grafico `/plot`:** da **full-1.19**, il comando e il pulsante aprono un sottomenu con **📈 15 min**, **📈 30 min** e **📈 60 min**. Scegliendo un intervallo ricevi una **foto PNG aggiornata** di target/media PT100 con lo storico disponibile nella fascia scelta e una lettura corrente. Puoi richiedere direttamente `/plot15`, `/plot30` o `/plot60`; `/modalita` torna alla tastiera principale. Ogni selezione acquisisce una nuova immagine; non è un aggiornamento continuo. Funziona anche in Manuale e a riposo, in tutte le modalità, senza aprire la dashboard. Lo snapshot conserva l'intero intervallo senza troncare l'ora agli ultimi 257 campioni: usa fino a 4321 punti in PSRAM e una sola richiesta grafico pendente; campioni non validi interrompono la curva. Se l'ESP è acceso da meno tempo, mostra solo lo storico già disponibile. Rendering PNG e upload `sendPhoto` sono sul task Telegram in PSRAM, separati dal PID; non vengono usati servizi grafici esterni. Le richieste scadute o non accodabili sono segnalate. `telegram.plotsSent`, `plotsPending` e `plotsDropped` aiutano la diagnosi. Il firmware registra anche i comandi nel menu delle chat autorizzate; `/start` aggiorna la guida e la tastiera. L'anteprima Python usa la libreria standard per creare il PNG. Riferimenti: [sendPhoto](https://core.telegram.org/bots/api#sendphoto), [setMyCommands](https://core.telegram.org/bots/api#setmycommands).

**Avvio sempre visibile:** dopo l'avvio e il ripristino di Wi-Fi/Internet e ora NTP, il bot invia la conferma di avvio firmware con versione e IP Wi-Fi, anche in Essenziale. Anche l'avvio di un Programma o del Manuale è sempre inviato, senza suono. Eventuali emergenze o interruzioni pendenti conservano invece il messaggio di allarme dettagliato. Senza alimentazione o rete l'ESP non può inviare: il messaggio arriva quando la connessione è disponibile.

**Modalità notifiche:** invia `/start` o `/modalita` e scegli uno dei pulsanti nella chat. Sono disponibili anche i comandi diretti:

| Modalità | Comando | Eventi automatici |
| --- | --- | --- |
| 🛡️ Essenziale | `/essenziale` | Emergenze, allarmi, blackout e PDF; avvio ESP e avvio Programmi/Manuale sempre visibili. |
| 🔔 Sintetica | `/sintetica` | Avvio, pausa/ripresa, STOP e completamento in formato breve; allarmi dettagliati e PDF. |
| 📋 Dettagliata | `/dettagliata` | Tutti gli eventi con stato completo, avvisi sonde recuperate, allarmi e PDF. |

La modalità iniziale è **Sintetica**, che mantiene il comportamento precedente. Da full-1.20 la selezione è personale per ciascun utente. La selezione vale da quel momento per i nuovi eventi; passando a Essenziale vengono esclusi anche gli eventi ordinari ancora in coda. Un invio già iniziato può completarsi. **`/status`, `/plot`, aiuto e scelta modalità restano disponibili in tutte le modalità.** Avvio ESP e avvio del controllo non vengono soppressi passando a Essenziale. Questi comandi cambiano solo le notifiche: non avviano o fermano il forno e non riconoscono gli allarmi. Tutti i comandi richiedono la stessa chat privata, mittente e freschezza già verificati per `/status`.

Sull'ESP la preferenza è salvata in NVS (`tgMode`) solo quando cambia, dal loop principale; il task di rete non accede a NVS né a GPIO/PID. Se il salvataggio fallisce resta attiva la modalità precedente e il bot lo segnala. Nell'anteprima viene salvata atomicamente in `include/telegram_preferences.json`, con permessi privati e file escluso da Git. Le preferenze di ESP e anteprima sono separate: il primo avvio sull'ESP usa Sintetica. Riavviare o inviare `/start` non azzera la scelta salvata.

**Messaggi brevi in Sintetica:** avvio e STOP riportano soltanto icona/evento, **nome del programma** e **giorno/ora italiana dell'evento**, inclusa l'ora legale. Lo stesso formato breve è usato per pausa, ripresa e completamento. In Dettagliata ogni evento allega invece temperature, PID, uscite e tempi al momento dell'evento; emergenze, allarmi e blackout mantengono sempre motivo e stato dettagliato. La data viene fissata alla creazione del messaggio, anche se l'invio avviene dopo; sul firmware senza NTP compare **Orario non sincronizzato**. STOP senza un ciclo attivo non genera una notifica di ciclo fermato.

**Notifiche selettive:** avvio, pausa/ripresa, STOP manuale, completamento, risposte `/status`/aiuto e PDF vengono inviati con `disable_notification=true`: restano nella timeline, ma senza suono. Emergenze, allarmi, **🚨 BLACKOUT** e ciclo interrotto da riavvio usano la notifica normale (`false`); il PDF allegato allo stesso allarme resta silenzioso, evitando un secondo suono. Da **full-1.16**, i fault sonda recuperati restano nel log locale e inviano un avviso silenzioso **solo in Dettagliata**. In Essenziale e Sintetica non inviano messaggi. I recuperi falliti conservano il normale allarme in tutte le modalità. La priorità dell'evento determina la notifica, non l'emoji o il testo: chiedere `/status` durante un allarme non ripete il suono dell'allarme.

**Lascia attive le notifiche della chat del bot** e abilita quelle di Telegram sul telefono. Il bot non può scavalcare una chat silenziata, le restrizioni del sistema o Non disturbare, né garantire una consegna immediata senza rete. Secondo la [Bot API](https://core.telegram.org/bots/api#sendmessage), l'invio silenzioso può comunque produrre notifiche visive, banner e badge: elimina il suono, non tutti gli avvisi. Per azzerare anche gli avvisi ordinari servono due destinazioni separate: una timeline silenziata e una chat allarmi con notifiche attive.

Al termine di ogni ciclo avviato (**completamento, STOP o allarme/emergenza**) il bot invia anche un **PDF**, senza richiedere la dashboard aperta. Il file contiene grafico vettoriale completo **target/media PT100**, riepilogo diviso in sezioni e **una pagina per ogni fase, con grafico dedicato** e dati della fase. Il nome include l’esito: `Completed_Nome_programma_2026-10-09_16-30-00.pdf` per un ciclo completato, `Stopped_...pdf` per STOP e `Error_...pdf` per allarme/emergenza, con **data/ora italiana di fine ciclo**, inclusa l'ora legale. Un semplice STOP senza un ciclo avviato non genera PDF. Sull'ESP senza NTP il nome usa `senza_orario_avvio-<boot>_<uptime>` e gli orari restano indicati come non sincronizzati.

Il loop copia soltanto uno snapshot del report e della curva in PSRAM; generazione PDF e upload HTTPS multipart sono eseguiti dal task Telegram, separato dal controllo. Un nuovo ciclo non modifica gli allegati accodati. Ci sono al massimo **4 report pendenti** (3 in coda e uno nel task), con buffer PDF limitato a **768 KiB**; allarmi, risposte e notifiche hanno la precedenza sugli allegati; il polling mantiene un proprio intervallo. Un nuovo allarme interrompe l'upload al successivo blocco di scrittura; una chiamata di rete già in corso può attendere il suo timeout. Dalla versione **full-1.6** la risposta HTTP viene letta anche quando è già stata ricevuta e conservata nel buffer del client insieme alle intestazioni: questo evita di ritentare PDF già consegnati. Uno stesso report già accodato non viene accodato nuovamente (identità boot/avvio/fine, ultime 16 identità). Una continuazione esplicita può produrre un nuovo report finale con un istante di fine diverso. Gli allegati vengono ritentati con lo stesso file per un massimo di **24 ore** solo se l'upload non ha ancora tentato di scrivere il delimitatore multipart finale, oppure se Telegram risponde esplicitamente **429** (rispettando `retry_after`). Se la conferma si perde dopo il possibile invio completo, o arriva un errore 5xx, l'esito è **incerto**: il PDF viene tolto dalla coda e non reinviato automaticamente, con un unico avviso silenzioso. Gli errori 4xx permanenti fermano quel report. Questa scelta evita duplicati ma può lasciare un PDF da scaricare manualmente se il server non lo aveva ricevuto. Fino a full-1.20 le code erano solo in RAM. Da **full-1.21** eventi e report pendenti vengono salvati dal task Telegram nella coda persistente descritta sotto; rimane il ripiego in RAM se lo storage non è disponibile. Il report della dashboard resta scaricabile. `/api/status` espone `telegram.reportsUnconfirmed` e `telegram.duplicateReportsSkipped` oltre ai contatori già presenti; gli esiti incerti sono inclusi anche nei report scartati, senza contarli come inviati. Al riavvio si riprendono solo i report effettivamente pendenti nella coda persistente; non viene riaccodato il semplice ultimo riepilogo della dashboard.

L'anteprima Python riusa **lo stesso generatore jsPDF della dashboard**, tramite `scripts/report_pdf.cjs`: serve Node.js sul Mac (viene rilevato anche il runtime di ChatGPT, se presente). Legge report e curva da localhost con verifica della chiave, evitando invii ripetuti dello stesso report durante la connessione. Anche l'anteprima elimina dalla coda un upload con conferma persa o errore permanente: il solo errore di invio ritentabile è un rifiuto esplicito 429; urllib non permette di distinguere un timeout prima dell'invio da uno dopo la consegna. Senza orari simulati il nome file usa l'ora reale italiana di acquisizione del report. Le notifiche e la didascalia PDF indicano **ANTEPRIMA SIMULATA**. Il firmware genera autonomamente il PDF e non richiede Python, Node.js o un computer acceso.

Le notifiche automatiche includono **avvio del ciclo con nome del programma**, pausa/ripresa, STOP, completamento, arresto per allarme ed **EMERGENZA**. Una lettura PT100 anomala recuperata invia un avviso solo in Dettagliata; se il recupero fallisce, resta il normale allarme in tutte le modalità. All'avvio si segnala anche un ciclo interrotto o un'emergenza rimasta bloccata. Gli allarmi sono accodati dopo lo spegnimento del comando uscite.

### 1. Associare il bot alla propria chat

**Guida fissata:** `/start` mostra la guida completa, la modalità attiva e i pulsanti di selezione e fissa il messaggio nella chat privata, senza suono; `/help` mostra la stessa guida e `/modalita` riapre la scelta. Il fissaggio viene eseguito dal task Telegram dopo l'invio, con ritentativo separato che riusa l'ID del messaggio già inviato. In questo modo un errore temporaneo durante il fissaggio non duplica la guida. `/start` non avvia il forno. Per modificare il testo condiviso tra firmware e anteprima, cambia `kGuideText` in `src/telegram_policy.h`. La descrizione breve del profilo, visibile quando condividi il bot, è **Monitoraggio forno 🔥**.

Crea un **bot dedicato al forno** con `/newbot` nella chat ufficiale [@BotFather](https://t.me/BotFather), scegliendo uno username che finisca in `bot`. Dalla cartella del firmware full:

```sh
cd "/Users/gigi/Documents/PROJECTS/Forno oriana/firmware/oven-controller-full"
python3 scripts/telegram_tool.py --setup
```

Incolla il token nella richiesta del terminale: non viene visualizzato. Apri il link stampato dallo script e premi **AVVIA** nella tua chat privata col bot. Lo script usa un codice casuale per associare questa chat e crea **`include/telegram_config.h`**, con permessi `0600`, escluso da Git. Token e chat non vengono mostrati nella dashboard/API/log. Il token finisce nel firmware compilato: tratta anche il binario come privato. Il template per una configurazione manuale è `include/telegram_config.example.h`.

Serve Python **3.9 o successivo**, senza pacchetti aggiuntivi. Prima prova di consegna dal Mac:

Lo script mantiene la verifica HTTPS. Se il Python portabile di PlatformIO non trova il suo percorso CA predefinito, usa il bundle di sistema (`/etc/ssl/cert.pem` su macOS), oppure `certifi` se già installato. Un eventuale `SSL_CERT_FILE`/`SSL_CERT_DIR` esplicito viene rispettato. Gli errori distinguono certificati, DNS e timeout, senza stampare il token. Le letture di associazione vengono ritentate per timeout/errori temporanei; un token non valido o un errore di verifica certificato interrompono subito la procedura. Se dopo il salvataggio fallisce il messaggio di conferma, la configurazione rimane valida e puoi verificare la consegna con `--check`.

```sh
python3 scripts/telegram_tool.py --check
```

Riceverai **PROVA TELEGRAM DAL MAC**. Questo verifica il bot e la ricezione sul telefono; non verifica l'ESP. Non avviare lo script di associazione o l'anteprima mentre lo stesso bot è già letto da un ESP: Telegram permette un solo lettore `getUpdates`. Un bot con webhook attivo viene rifiutato dall'associazione/anteprima: usane uno dedicato.

### 2. Provare ciclo e allarmi senza ESP

In un terminale avvia la dashboard simulata:

```sh
python3 ../local-preview/server.py --port 8080
```

In un secondo terminale, sempre dalla cartella del full:

```sh
python3 scripts/telegram_tool.py --preview http://localhost:8080/full/
```

Apri `http://localhost:8080/full/`. Le notifiche di questa prova sono marcate **ANTEPRIMA SIMULATA**; `/status` riporta in fondo **Nessun ESP collegato; nessuna uscita fisica**. Invia `/status` al bot, avvia un programma dalla dashboard e verifica nome/step/temperature; poi prova pausa, ripresa e STOP. Avvia un altro ciclo e premi il fungo emergenza; riconoscilo dalla dashboard. Per provare un errore sonda, avvia un ciclo e premi **Simula fault PT100 #2** nel banner della pagina. Ripristina la sonda e riconosci l'allarme prima di un nuovo avvio. Puoi verificare anche il messaggio di completamento lasciando terminare il programma simulato.

Il collegamento Python legge soltanto le API locali di stato/log, conserva il numero dell'ultimo evento per evitare notifiche ripetute e può rilevare avvio e STOP anche tra due letture. Le notifiche ordinarie seguono la modalità scelta e sono datate con l'ora italiana del log dell'evento; in Dettagliata e per gli avvisi critici allegano lo stato **al rilevamento**, distinto dall'istante dell'evento. `/status` esegue una nuova lettura e inizia direttamente da **Stato:**, senza intestazione. **💤 IDLE** indica l'attesa senza un ciclo avviato; **⚠️ STOP · CICLO FERMATO** indica un ciclo avviato e poi fermato. Premere STOP senza un ciclo in corso non trasforma IDLE in un ciclo fermato e non genera una nuova notifica di STOP. Il parametro `--preview` accetta soltanto localhost e non si collega a un ESP. **Ctrl+C** termina il collegamento Telegram; premi Ctrl+C anche nell'altro terminale per fermare la dashboard.

### 3. Provare sull'ESP

Chiudi il collegamento Python al bot. Configura rete del capannone e NTP in `include/wifi_config.h`, quindi compila e carica il full:

```sh
pio run -e esp32-s3-n16r8
pio run -e esp32-s3-n16r8 -t upload
```

Oppure usa l'ambiente OTA già configurato, a ciclo fermo. Con Wi-Fi Internet e NTP sincronizzato riceverai **ESP AVVIATO**; invia `/status`. Fai prima le prove di avvio/STOP/emergenza con resistenze scollegate, poi il collaudo termico sorvegliato. Durante queste prove verifica RAM minima, età letture e regolarità del controllo anche togliendo/ripristinando Internet. Non interrompere il cablaggio PT100 sul forno caldo per generare un allarme.

Il controllo non aspetta Telegram. Un task di priorità 1 sul core 0 esegue HTTPS e polling limitato a 2 s; il loop Arduino e il supervisore uscite restano separati. Il client riusa la connessione quando possibile e verifica il certificato con il bundle CA del SDK; non usa connessioni TLS senza verifica. Attende Wi-Fi/NTP e si sospende durante OTA. Code e buffer JSON/risposta sono in PSRAM; se non si possono allocare il bot resta disabilitato, senza fermare il forno. Prima di una richiesta controlla il margine della RAM interna. Da **full-1.8** distingue un nuovo handshake da una connessione TLS già aperta: per l'handshake richiede 60.000 B liberi e un blocco continuo almeno pari al record TLS del SDK più 2.048 B (18.432 B con il SDK attuale, record da 16 KiB); per riusare la connessione mantiene una riserva di 24.000 B. Il controllo precedente richiedeva 32 KiB contigui e lo stesso margine anche dopo l'allocazione dei record, chiudendo connessioni valide e potendo bloccare permanentemente il bot. Se la riserva non basta, libera la sessione e ricontrolla; nessuna verifica dei certificati viene disabilitata. Polling, invio messaggi/fissaggio guida e upload PDF hanno ritentativi indipendenti: un PDF in attesa non impone il suo ritardo ai comandi o agli allarmi. Le richieste non sono garantite entro un tempo preciso: dipendono da rete e servizio.

Gli allarmi hanno una coda riservata di 4 messaggi, gli eventi ordinari di 6 e `/status` una risposta sostituibile, oltre ai messaggi già presi dal task. Le risposte scadono dopo 10 s; gli eventi ordinari dopo 1 ora; gli allarmi rimangono fino alla consegna o al riavvio. Le richieste vecchie di oltre 120 s e i messaggi estranei vengono scartati. Gli invii falliti vengono ritentati dopo 5/15/30/60 s, rispettando anche `retry_after` di Telegram. Code piene possono perdere messaggi; una risposta di rete persa dopo un invio riuscito può provocare un duplicato. Le code sono in RAM, non vengono salvate ad ogni evento.

`GET /api/status` aggiunge `telegram`: configurazione/task attivi, disponibilità Wi-Fi/NTP, messaggi inviati, tentativi falliti, messaggi scartati, ultimo codice HTTP e uptime dell'ultimo invio. `reportsSent`, `reportsPending` e `reportsDropped` indicano PDF consegnati, pendenti e scartati. `lastHttpCode` negativo indica errore di rete (`-1`), margine RAM insufficiente (`-2`), creazione client HTTP fallita (`-3`) o risposta JSON non valida (`-4`); `401/403` richiedono verifica di token/chat e blocco del bot, `409` può indicare un secondo lettore o webhook, `429` provoca attesa. `telegram.internalFreeBytes`, `largestInternalBlockBytes`, `tlsRecordBytes`, `tlsConnected` e `memoryDeferrals` permettono di diagnosticare il margine TLS. `pollCount` e `lastPollAtMs` indicano letture dei comandi riuscite; `commandsReceived` conta i comandi autorizzati/freschi riconosciuti e `staleCommands` quelli della chat autorizzata scartati perché vecchi. Sono contatori della singola accensione. Dopo un aggiornamento, invia nuovi `/status` e `/modalita`: quelli rimasti in coda per oltre 120 s vengono scartati intenzionalmente. Prima di diagnosticare con `getUpdates` da un computer, ferma l'altro lettore: questa chiamata può contendere il polling del firmware. `getMe` e `getWebhookInfo` non consumano i comandi. I campi `freeHeap` e `minFreeHeap` includono il carico reale della prova sull'ESP: il risultato della compilazione misura solo la RAM statica.

Se ESP o Internet sono spenti, **`/status` non può rispondere** e Telegram non invia automaticamente un avviso di offline. Al ritorno il bot può inviare eventi ancora accodati o segnalare il riavvio/ciclo interrotto, con le limitazioni del registro già descritte. Per rilevare l'assenza mentre è in corso serve un monitor esterno. Le notifiche restano informative e non sostituiscono gli arresti del forno.

Riferimenti: [Telegram Bot API](https://core.telegram.org/bots/api), [creazione del bot](https://core.telegram.org/bots/tutorial#obtain-your-bot-token).

## Aggiornamenti Wi-Fi OTA

È integrato ArduinoOTA, porta **3232**, host **oven-full.local**. La configurazione privata è in `include/ota_config.h`, esclusa da Git; in questa copia è stata ripresa dal field-test. Non cambiare il file durante il passaggio da field-test a full via OTA: la password deve corrispondere al firmware già installato. In una nuova copia crea il file da `include/ota_config.example.h` e imposta una password privata di 16..64 lettere, numeri, `_` o `-`. Senza password OTA è disabilitato; il caricamento USB resta disponibile.

Dalla cartella `firmware`, prima installazione via USB:

```sh
pio run -d oven-controller-full -e esp32-s3-n16r8 -t upload
```

Successivamente, collega PC ed ESP alla stessa rete e usa l'IP mostrato dalla dashboard, oppure mDNS:

```sh
pio run -d oven-controller-full -e esp32-s3-n16r8-ota -t upload
# Rete diretta OvenController-Full:
pio run -d oven-controller-full -e esp32-s3-n16r8-ota -t upload --upload-port 192.168.4.1
```

Per il primo passaggio Wi-Fi da field-test a full, usa `--upload-port oven-field-test.local` oppure l'IP dell'ESP; dopo il riavvio usa `oven-full.local`. Lo script di upload legge la password dal file locale, senza inserirla nei comandi. Il firewall del PC deve consentire la connessione di ritorno dell'uploader sulla rete locale.

All'avvio dell'aggiornamento autenticato si fermano ciclo e prove; tutte le uscite restano spente fino al riavvio. Anche un aggiornamento fallito o un errore di autenticazione arresta ciclo/prove, senza ripresa automatica. Dashboard e firmware sono aggiornati insieme nelle partizioni OTA già previste.

**Cambio della password OTA:** la password è compilata nel firmware. Modificare soltanto `include/ota_config.h` non cambia né l'ESP né un `firmware.bin` già preparato. Conserva la password attualmente installata, imposta quella nuova nel file (16..64 lettere, numeri, `_` o `-`), ricompila e sostituisci il binario sul PC Windows. Nel caricamento con `espota.py` descritto sotto, al prompt inserisci la **vecchia password**, che autentica l'aggiornamento sul firmware ancora installato. Dopo un aggiornamento riuscito e il riavvio, l'ESP usa la **nuova password**. Non usare il normale `pio ... -t upload` dopo aver cambiato il file: lo script locale usa già la nuova password per l'autenticazione. Se non conosci la vecchia password, serve un caricamento USB.

### Accesso da casa: dashboard e aggiornamento firmware

Questa è la procedura per il setup già configurato: **Mac di casa → Tailscale → PC Windows del capannone → ESP**.

| Dato | Valore attuale |
|---|---|
| Utente Windows per SSH | `oriana` |
| IP Tailscale del PC Windows | `100.89.40.5` |
| IP LAN del forno | `192.168.1.190` |
| Dashboard sul Mac, con tunnel aperto | <http://127.0.0.1:8085/> |

Tailscale deve essere connesso sui due computer. Il PC Windows deve essere acceso, non in sospensione e collegato alla rete del forno; OpenSSH Server deve essere attivo. Non occorre reinstallare SSH o Python ogni volta. Se cambiano gli indirizzi, aggiorna i valori nei comandi; conviene riservare l'IP del forno nel DHCP del router.

<a id="dashboard-da-casa"></a>

#### 1. Aprire la dashboard da casa — Terminale A del Mac

Nel **Terminale del Mac** esegui:

```sh
ssh -N -o ExitOnForwardFailure=yes -L 127.0.0.1:8085:192.168.1.190:80 oriana@100.89.40.5
```

Inserisci la **password Windows dell'utente `oriana`**, non il PIN e non la password Tailscale. Durante la digitazione non compaiono caratteri o asterischi. Dopo Invio il terminale rimane fermo senza un nuovo prompt: è normale, **lascialo aperto**.

Apri sul Mac **<http://127.0.0.1:8085/>**; puoi salvarlo nei preferiti. È la dashboard reale del forno, con gli stessi comandi del collegamento locale. Non è l'anteprima Python. Questo collegamento non riavvia né aggiorna l'ESP.

Per chiudere il tunnel premi **Ctrl+C** nel Terminale A. Il ciclo del forno continua autonomamente. La porta 8085 ascolta soltanto sul Mac, sull'indirizzo di loopback. Il tunnel inoltra la dashboard HTTP, non ArduinoOTA; per il firmware segui il punto 2.

<a id="firmware-da-casa"></a>

#### 2. Aggiornare il firmware da casa — Terminale B del Mac

**Prima dell'upload:** dalla dashboard ferma Programmi, Manuale e TEST e scarica report/CSV che vuoi conservare. L'aggiornamento riavvia l'ESP, cancella lo storico in RAM e non riprende il controllo precedente.

**A. Compila sul Mac.** Lascia aperto il Terminale A della dashboard e apri un **secondo Terminale del Mac**, il Terminale B:

```sh
cd "/Users/gigi/Documents/PROJECTS/Forno oriana/firmware/oven-controller-full"
"$HOME/.platformio/penv/bin/pio" run -e esp32-s3-n16r8-ota
```

Aspetta **SUCCESS**. Se la compilazione fallisce, fermati: non copiare né caricare un vecchio binario. Questo comando compila soltanto e non contatta l'ESP. I file privati Wi-Fi, Telegram e OTA devono essere quelli del forno; Git non li recupera.

**B. Prepara e copia i file**, sempre nel Terminale B del Mac, soltanto dopo SUCCESS:

```sh
mkdir -p output/ota-upload
cp .pio/build/esp32-s3-n16r8-ota/firmware.bin output/ota-upload/firmware.bin
cp "$HOME/.platformio/packages/framework-arduinoespressif32/tools/espota.py" output/ota-upload/espota.py
shasum -a 256 output/ota-upload/firmware.bin
scp output/ota-upload/firmware.bin output/ota-upload/espota.py oriana@100.89.40.5:Downloads/
```

Per SCP inserisci la **password Windows di `oriana`**. Conserva il valore SHA256 visualizzato: servirà per confrontare il file ricevuto. Trasferisci `firmware.bin`, non `bootloader.bin`, `partitions.bin` o un'immagine combinata. Il binario contiene anche le configurazioni private compilate: trattalo come un file privato. Sul PC Windows non servono il progetto o PlatformIO.

**C. Entra nella PowerShell del PC Windows**, dal Terminale B del Mac:

```sh
ssh -t oriana@100.89.40.5 powershell -NoProfile
```

Inserisci ancora la **password Windows di `oriana`**. Quando compare un prompt come `PS C:\Users\Oriana>`, stai eseguendo comandi **sul PC Windows del capannone**.

**D. Controlla file, Python e stato del forno**, nella PowerShell Windows appena aperta:

```powershell
Set-Location "$HOME\Downloads"
$ovenPython = "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe"
& $ovenPython --version
Get-Item .\firmware.bin, .\espota.py
Get-FileHash .\firmware.bin -Algorithm SHA256
Invoke-RestMethod "http://192.168.1.190/api/status" |
    Select-Object firmware, phase, relay, otaUpdating, manual, test |
    ConvertTo-Json -Depth 5
```

L'hash deve coincidere con quello del Mac. Per procedere: `phase` diversa da `running` e `paused`, `relay: false`, `manual.active: false`, `test.running: false` e `otaUpdating: false`. Se un controllo è attivo, fermalo e salva i dati prima di proseguire. Se Python o i file non vengono trovati, non avviare l'upload; vedi le istruzioni di prima configurazione sotto.

**E. Carica il firmware**, sempre nella stessa PowerShell Windows:

```powershell
& $ovenPython -c "import getpass,sys,espota; sys.exit(espota.main(['-i','192.168.1.190','-p','3232','-P','3233','-r','-a',getpass.getpass('Password OTA: '),'-f','firmware.bin']))"
```

Al prompt **Password OTA** inserisci la password del firmware attualmente installato sull'ESP, configurata sul Mac in `include/ota_config.h`. **È diversa dalla password Windows.** Non inserirla nella riga di comando o in Git e non abilitare `--debug`, che può stamparla. Se compare un avviso che la password non può essere nascosta, interrompi con Ctrl+C prima di inserirla e usa una console PowerShell interattiva via desktop remoto.

Aspetta la fine di `Uploading` senza chiudere il terminale. Il trasferimento e la connessione di ritorno restano sulla LAN del capannone: non servono altre rotte VPN o modifiche a SNAT per questo metodo.

**F. Verifica il riavvio**, sempre nella PowerShell Windows, dopo che il forno torna raggiungibile:

```powershell
Invoke-RestMethod "http://192.168.1.190/api/status" |
    Select-Object firmware, phase, relay, otaUpdating
```

Controlla la versione appena compilata, `relay: false` e `otaUpdating: false`; aggiorna anche la dashboard nel browser del Mac. Per uscire da Windows scrivi `exit`; se rimane una shell remota, scrivi `exit` una seconda volta. Nel Terminale A premi Ctrl+C per chiudere anche il tunnel della dashboard. Puoi poi disconnettere Tailscale sul Mac. Nessuna di queste disconnessioni interrompe il controllo del forno.

#### Solo la prima volta o se qualcosa non funziona

Python 3.13 e la regola firewall sono già stati configurati sul PC Windows di questo setup. Non ripetere l'installazione a ogni upload; `$ovenPython` va invece impostato ogni volta che apri una nuova PowerShell.

<details>
<summary>Python non trovato su Windows</summary>

Se hai già un altro Python 3, imposta `$ovenPython` al percorso del suo `python.exe`. Per una nuova installazione per l'utente corrente usa [WinGet](https://learn.microsoft.com/en-us/windows/package-manager/winget/install) nella PowerShell Windows:

```powershell
winget install --exact --id Python.Python.3.13 --source winget --scope user --silent --location "$env:LOCALAPPDATA\Programs\Python\Python313" --accept-package-agreements --accept-source-agreements
```

Aspetta il completamento, poi ripeti i comandi del punto D. Il percorso completo evita problemi con il PATH della sessione SSH già aperta. Non servono pacchetti Python aggiuntivi. Se WinGet non è disponibile, installa [Python 3 su Windows](https://docs.python.org/3/using/windows.html) dal PC.

</details>

<details>
<summary>Authenticating...OK, poi No response from device</summary>

La password è corretta ma l'uploader non ha ricevuto la connessione TCP di ritorno dall'ESP; il trasferimento non è ancora iniziato. Il firewall deve consentire TCP 3233 in ingresso a Python dall'IP del forno. Nel setup attuale è bastata questa regola, da creare **una sola volta in PowerShell con privilegi di amministratore sul PC Windows**:

```powershell
New-NetFirewallRule -Name "OvenControl-OTA-TCP3233" -DisplayName "OvenControl OTA dal forno" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 3233 -RemoteAddress 192.168.1.190 -Program "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe" -Profile Any
```

La regola è limitata al programma Python, alla porta 3233 e all'IP del forno anche se la LAN Windows è classificata come pubblica. Se cambia l'IP o il percorso di Python, aggiorna la regola. Non disabilitare il firewall. La porta 3233 deve essere libera; UDP 3232 deve raggiungere l'ESP. Dopo aver corretto il problema, ripeti dal punto D. Riferimento: [New-NetFirewallRule](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule).

</details>

| Problema | Cosa verificare |
|---|---|
| Timeout/connessione rifiutata sulla porta 22 | PC acceso, Tailscale connesso su entrambi e OpenSSH Server attivo. |
| `Permission denied` | Nome utente `oriana`, password Windows e autorizzazioni SSH. |
| Dashboard: `channel ... open failed: connect failed` | Dal PC Windows apri `http://192.168.1.190/` e prova `Test-NetConnection 192.168.1.190 -Port 80`. |
| Dashboard: `administratively prohibited` | Il server SSH deve consentire l'inoltro TCP. |
| Porta locale 8085 occupata | Cambia 8085 sia nel comando del tunnel sia nel link sul Mac. |
| `Authentication Failed` nell'upload OTA | Serve la password installata sull'ESP, non quella Windows; controlla il file privato locale. |

Il tunnel della dashboard non richiede un subnet router. Per aprire direttamente l'IP LAN dell'ESP dal Mac o usare OTA diretto attraverso la VPN, consulta le alternative sotto. Riferimenti: [inoltro locale OpenSSH](https://man.openbsd.org/ssh.1#L) e [OpenSSH su Windows](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh-server-configuration).

#### Alternativa: progetto e PlatformIO anche sul computer al capannone

Da casa accedi via SSH o desktop remoto a un computer collegato alla stessa LAN dell'ESP. Il computer deve avere PlatformIO, una copia aggiornata del progetto e i suoi file privati locali di configurazione. Questi file sono esclusi da Git: un clone o `git pull` non recupera password Wi-Fi/OTA o token Telegram. La password in `include/ota_config.h` deve corrispondere a quella del firmware già sul forno; per compilare conserva anche le configurazioni Wi-Fi e Telegram effettivamente usate. Non inserire credenziali nei comandi o nel repository.

Dal Mac di casa, con Tailscale connesso e accesso SSH già autorizzato:

```sh
ssh utente@pc-capannone
```

**Nel terminale del computer al capannone**, entra nella sua copia del progetto (qui un esempio di percorso Linux), verifica che `/api/status` riporti il controllo fermo e compila:

```sh
cd "$HOME/OvenControl/firmware/oven-controller-full"
curl --fail --connect-timeout 5 --max-time 10 http://192.168.1.190/api/status
pio run -e esp32-s3-n16r8-ota
```

Quando il forno è fermo e hai salvato i dati, avvia l'upload **da quello stesso terminale remoto**:

```sh
pio run -e esp32-s3-n16r8-ota -t upload --upload-port 192.168.1.190
```

L'autenticazione è letta dal file privato locale sul computer al capannone. Il trasferimento OTA e la connessione TCP di ritorno restano sulla LAN del forno; non richiedono di modificare il NAT di Tailscale. Serve comunque che il firewall locale consenta quella connessione. Per questo metodo basta poter accedere al computer via VPN: non serve una rotta verso l'ESP dal Mac di casa.

#### Upload diretto dal Mac attraverso la VPN

Questo metodo richiede una **VPN con instradamento bidirezionale** verso la LAN del capannone. Per Tailscale, la rotta del subnet router deve essere pubblicizzata, approvata e accettata dal client, e le regole di accesso devono consentire il traffico. Per la dashboard usa l'IP LAN dell'ESP; non fare affidamento su `oven-full.local`/mDNS attraverso la VPN. Una VPN con solo un exit node non configura automaticamente l'accesso alla LAN del forno. Vedi [subnet router Tailscale](https://tailscale.com/docs/features/subnet-routers).

**Aprire la dashboard non basta a garantire l'OTA.** L'uploader invia l'invito via **UDP 3232**; poi l'ESP apre una **nuova connessione TCP** verso l'IP sorgente dell'invito e la porta dell'uploader. Il subnet router Tailscale normalmente applica SNAT: l'ESP vede come sorgente il computer ponte, non il Mac di casa, e tenta il collegamento TCP verso quel ponte. È una limitazione dell'OTA diretto in questa configurazione, non della password o della compilazione. Cambiare `--upload-port` o l'indirizzo di ascolto `--host_ip` non risolve da solo il percorso di ritorno.

Per l'OTA diretto occorre una configurazione senza SNAT per questo percorso, una rotta di ritorno dalla LAN dell'ESP verso l'IP VPN del Mac tramite il subnet router, e regole VPN/firewall che consentano la connessione ESP → Mac. Disabilitare SNAT da solo non crea la rotta di ritorno. La configurazione dipende dal router e dal sistema operativo: consulta [SNAT e rotte di ritorno Tailscale](https://tailscale.com/docs/reference/troubleshooting/network-configuration/disable-subnet-route-masquerading). Se queste condizioni non sono già verificate, usa il metodo dal computer al capannone.

Sul Mac, una volta configurato il percorso VPN in entrambe le direzioni:

```sh
cd "/Users/gigi/Documents/PROJECTS/Forno oriana/firmware/oven-controller-full"
curl --fail --connect-timeout 5 --max-time 10 http://192.168.1.190/api/status
pio run -e esp32-s3-n16r8-ota
```

Soltanto con forno fermo e dati scaricati:

```sh
pio run -e esp32-s3-n16r8-ota -t upload --upload-port 192.168.1.190
```

Di default `espota` sceglie una porta TCP di ascolto fra **10000 e 60000**. Per avere una porta precisa da autorizzare nel firewall, aggiungi **solo questa voce all'ambiente OTA esistente** `[env:esp32-s3-n16r8-ota]` in `platformio.ini`, conservando le altre impostazioni:

```ini
upload_flags =
    --host_port=3233
```

Lo script `scripts/configure_ota.py` conserva questo flag e aggiunge password e porta UDP 3232. Il firewall del Mac e le regole VPN devono consentire **TCP 3233 dall'ESP verso il Mac**; UDP 3232 deve raggiungere l'ESP. La porta 3233 è solo un esempio e deve essere libera. Non occorre pubblicare dashboard o OTA su Internet né usare Tailscale Funnel: la dashboard non ha login e deve restare nella rete privata/VPN.

#### Dopo l'upload e diagnosi dei problemi

Attendi che l'ESP torni raggiungibile, ricarica la dashboard e controlla la **versione firmware in basso a sinistra**, l'uscita spenta e le letture delle sonde. Se compare un blocco da riconoscere, usa **RESET & BLOCK** dopo aver ripristinato le condizioni richieste. Nessun nuovo controllo parte automaticamente. In caso di upload fallito, verifica la versione effettivamente in esecuzione prima di riprovare.

| Sintomo | Verifica |
|---|---|
| Dashboard/IP non raggiungibile | ESP e ponte accesi, IP attuale, rotte VPN approvate, permessi e possibili conflitti fra le subnet di casa e capannone. |
| `No response from the ESP` | Raggiungibilità UDP 3232, OTA abilitato nel firmware e IP corretto; la porta HTTP 80 può funzionare anche con OTA non raggiungibile. |
| `Authentication Failed` | Password locale diversa da quella installata: il file privato non arriva da Git. |
| `Waiting for device...` / timeout di ritorno con dashboard funzionante | Percorso TCP ESP → uploader: SNAT, rotta di ritorno e firewall. Esegui l'upload dal computer al capannone se la VPN non consente quel percorso. |
| `Listen Failed` | Porta TCP scelta occupata o indirizzo di ascolto non disponibile sul computer che esegue l'upload. |

## Prima prova completa

1. Installa questa versione e completa il collaudo di sonde, LED, driver e STOP nella scheda **TEST**, con resistenze reali scollegate.
2. Premi **Esci da TEST**, poi verifica entrambe le letture reali a riposo.
3. In **Parameters → Attuatore e limite potenza**, scegli il componente realmente installato, una finestra compatibile con il circuito e il limite di comando per la prova. Premi **Salva comando sull'ESP**.
4. Controlla il programma iniziale **Prova a 50 C**: rampa a **50 °C a 1 °C/min**, mantenimento **10 min in banda**, raffreddamento a **35 °C con riferimento 1 °C/min**. Questo profilo è un punto di partenza per il collaudo termico, non un programma di produzione validato.
5. Conferma nella dashboard **CHECK HARDWARE/AMBIENTE EFFETTUATO**, poi avvia il ciclo sotto sorveglianza. La conferma è richiesta per ogni nuovo avvio e non viene conservata al refresh.
6. Verifica risposta termica, pausa, ripresa, STOP e allarmi; salva CSV e log prima di spegnere. Tara PID e profili con prove sul forno effettivo prima di usare materiali di produzione.

Le impostazioni iniziali del comando sono **relè meccanico**, **finestra 60 s**, **massimo 30% ON**. Non identificano automaticamente il tuo attuatore. Se il circuito usa un SSR, selezionalo e imposta una finestra adatta; puoi partire dal riferimento **10 s** degli altri firmware e tarare la risposta. I valori consentiti sono **1–300 s per SSR**, **60–300 s per relè** e **1–100%** di comando massimo. La scelta della finestra e dell'attuatore deve rispettare i loro limiti reali. Con una finestra di 60 s, un comando del 30% significa 18 s ON e 42 s OFF: non è una misura del 30% della potenza elettrica.

## Personalizzare i colori della dashboard

Modifica le variabili all’inizio di `data/style.css`, nel blocco `:root`:

- `--program-editor-bg`: riquadro nome/step del programma.
- `--control-settings-bg`: sezione PID, attuatore e limite potenza.
- `--hardware-check-bg`, `--hardware-check-border`, `--hardware-check-text`: check hardware giallo.
- `--errors-bg`, `--errors-border`: sezione ERRORI.
- `--cycle-timing-bg`, `--cycle-timing-border`: riquadri azzurri dei tempi del ciclo e dello step.
- `--test-enter-bg`, `--test-exit-bg`: pulsanti della modalità diagnostica.
- `--emergency-red`, `--emergency-yellow`: fungo di emergenza software.

Il server Python legge direttamente i file: salva e ricarica la pagina. Sull’ESP occorre ricompilare e caricare il firmware perché il CSS è incorporato in flash.

## Programmi, Parameters e PID

Da **full-1.18**, la scheda **Programmi** contiene l'editor dei profili. **Parameters**, subito dopo Programmi, raccoglie **Tuning PID**, **Attuatore e limite potenza** (con la stessa grafica precedente) e **Blocchi termici configurabili**. Manuale/Programmi e TEST/Log sono racchiusi in due gruppi con contorno; Dashboard, Parameters e Report restano singoli.

In Parameters puoi abilitare o disabilitare separatamente:

- **ΔT sonde:** `abs(PT100 #1 − PT100 #2) > soglia`; inizialmente **attivo oltre 30 °C**.
- **Media − target:** `(PT100 #1 + PT100 #2)/2 − setpoint istantaneo > soglia`; inizialmente **attivo oltre 10 °C**. Il confronto è con segno: stare sotto il target non causa questo blocco.

Le soglie sono numeriche da 0,1 a 430 °C; l'uguaglianza non fa scattare il blocco. Premi **Salva blocchi sull'ESP**: interruttori e soglie vengono salvati insieme in NVS e ricaricati dopo il riavvio. La riga **Impostazioni applicate** indica la configurazione effettiva, distinta dalle modifiche non salvate. Le modifiche sono bloccate durante Programmi, Manuale, pausa, TEST e OTA. Su un firmware precedente senza questa API il salvataggio è disabilitato.

Da **full-1.19**, il blocco ΔT sonde si applica anche a pausa e cooldown di Programmi/Manuale. Il blocco **media − target** si applica in salita, mantenimento e pausa, ma è escluso durante la discesa: il forno può rallentare il raffreddamento con le resistenze, ma non può accelerare quello naturale. Questo vale anche quando abbassi il target manuale e per RESET & CONTINUE verso un cooldown. Se la media resta sopra il riferimento, il riscaldamento resta spento e il cooldown prosegue senza allarme relativo. La PAUSA è un mantenimento e conserva il blocco media − target. TEST mantiene la diagnostica separata. Il limite assoluto individuale di 415 °C, i controlli di validità/freschezza delle sonde, EMERGENZA e i blocchi del supervisore non sono disattivati da questi due interruttori.

`POST /api/protections` riceve `probeDeltaEnabled`, `maxProbeDeltaC`, `meanOvershootEnabled`, `maxMeanOvershootC`. Le abilitazioni devono essere booleani JSON e le soglie numeri finiti. Un salvataggio fallito lascia applicate le impostazioni precedenti; dati salvati illeggibili/non validi fanno usare i default attivi 30/10 con avviso nel log. `/api/status.protections` e Telegram `/status` mostrano la configurazione. **RESET & CONTINUE** verifica le soglie attualmente abilitate, oltre alle sonde sane e al limite assoluto; modificare questi blocchi a forno fermo non cancella il punto di recupero.

L'editor accetta target **0–400 °C**, rampa almeno **0,1 °C/min**, mantenimento **1–360 minuti interi**. Il rate non ha un massimo software, ma il forno può seguire una rampa più lentamente. Dopo il primo step, una rampa deve salire rispetto al target precedente, un cooldown deve scendere e un hold deve conservare il target precedente. Le modifiche ai programmi, al PID e all'attuatore sono consentite solo a ciclo fermo. Le bozze della pagina non modificano la memoria ESP finché non premi Salva; la pagina impedisce di avviare con programmi non salvati.

**Ramp:** il riferimento cresce dalla media delle temperature misurate all'inizio dello step. Se lo step successivo è un **Hold allo stesso target**, il passaggio avviene quando il riferimento ha raggiunto il target e **entrambe le PT100 sono entro ±5 °C dal target dell'hold**. La rampa non viene saltata solo perché le sonde sono già in banda; il tempo previsto dal rate deve essere trascorso. Questo evita che l'ingresso nel mantenimento resti bloccato da una soglia più stretta del mantenimento stesso. Le rampe che non precedono un hold mantengono la condizione sulla **sonda più fredda a target −1 °C**.

**Hold:** il riferimento resta fisso. Il contatore parte da zero **all'ingresso nello step Hold**; il tempo trascorso in banda durante la rampa non viene contato retroattivamente. Si conta solo il tempo tra **due campioni consecutivi** con entrambe le PT100 entro **±5 °C**. Uscire dalla banda sospende il conteggio senza azzerare i minuti già accumulati. Pause, primo campione al ritorno in banda e intervalli senza letture fresche non aggiungono tempo. Il PID continua a regolare fuori banda.

**Cooldown (da full-1.11):** il PID segue il setpoint in discesa, usando gli stessi coefficienti, finestra SSR e limite di potenza della salita. Può dare calore solo con media PT100 sotto il setpoint istantaneo; a media uguale o superiore il comando è sempre zero, anche se integrale o derivata richiederebbero calore. Se il rate richiesto è più veloce del raffreddamento naturale, l’uscita resta spenta e lo step attende il forno. Il riferimento parte dal setpoint finale dello step precedente, senza rialzi o ribassi dovuti alla temperatura misurata; se il cooldown è il primo step del programma, parte dalla media iniziale delle PT100. Il rate descrive la discesa del riferimento, non una capacità di raffreddamento attivo. Lo step termina quando il riferimento è arrivato al target e la sonda più calda è al massimo a target +1 °C; alla fine del programma le uscite si spengono. Il forno può impiegare più tempo del riferimento.

**Tempo rimanente step attivo:** nell’hold indica il tempo ancora richiesto con entrambe le sonde in banda ±5 °C, senza decrementare fuori banda o in pausa. Per rampa e cooldown è una stima al rate impostato che considera sia il riferimento sia la sonda limitante (più fredda in salita, più calda in discesa); non garantisce il tempo reale del forno. In pausa il tempo del riferimento si congela, mentre una nuova misura può aggiornare la stima termica. **Tempo ciclo totale** comprende le pause e rimane visibile alla conclusione.

La stima per una rampa seguita da hold usa la stessa banda ±5 °C dell'ingresso: considera sia la sonda sotto il limite inferiore sia quella sopra il limite superiore. La dashboard spiega quando il riferimento è arrivato al target ma aspetta ancora le sonde. `/api/status` espone `stepTransitionToHold`, `stepCompletionBandC`, `stepCompletionTarget` e `stepWaitingForTemperature`. La correzione è identificata da **`firmware: full-1.5`** (inclusa anche in `full-1.6`); non modifica un ciclo già in esecuzione sul firmware precedente e richiede un nuovo caricamento a ciclo fermo.

**Da full-1.10, PAUSA mantiene con il PID la media PT100 rilevata quando premi il pulsante**, entro il range di target 0–400 °C, e congela l’avanzamento e il tempo dello step. Il mantenimento del programma non accumula minuti durante la pausa. **RIPRENDI** acquisisce nuovamente le sonde: rampa e cooldown proseguono dalla temperatura di pausa al rate originale, senza perdere il tempo attivo già svolto; un hold torna al target del programma e completa i minuti ancora richiesti. Anche una pausa durante cooldown può accendere il riscaldamento per mantenere la temperatura: riprendendo il cooldown il PID torna a seguire la curva in discesa, con comando zero sopra il setpoint. La pausa e la ripresa conservano la compensazione integrale del PID; STOP e allarmi la azzerano. Sonde, letture fresche, sovratemperatura e registro blackout restano sorvegliati durante la pausa. I firmware precedenti spegnevano invece l’uscita in pausa. **STOP** conclude il ciclo e lascia spento il riscaldamento. Da **full-1.16** non è previsto un arresto per durata massima: Programmi e pause possono proseguire oltre 12 ore. Avviare dopo uno STOP comincia un nuovo programma dall'inizio.

Il PID lavora sulla media delle PT100. Valori iniziali **Kp 12, Ki 0,015, Kd 50**, limiti impostabili **Kp 0–50, Ki 0–0,5, Kd 0–200**. Anti-windup, filtro della derivata e limite di tempo ON agiscono sul comando; il guadagno adatto dipende dal forno. A una media PT100 pari a setpoint +1 °C o oltre, il comando PID viene azzerato senza cancellare l’integrale. Il PID non dispone di autotuning automatico.

### TUNING PID


📉 Azione PROPORZIONALE Kp – La Forza di Reazione

È il motore principale del controllo. Reagisce all'errore attuale.\
• Se lo AUMENTI: Il sistema diventa più veloce a rispondere e riduce l'errore iniziale. Se però esageri, il sistema inizia a oscillare violentemente e diventa instabile.\
• Se lo DIMINUISCI: Il sistema diventa troppo lento, pigro e farà fatica a raggiungere il valore desiderato. \
• 📌 Quando usarlo: Aumentalo se il sistema è troppo lento a reagire; domalo (riducilo) se vedi che oscilla senza mai fermarsi.

⏳ Azione INTEGRALE Ki – La Precisione nel Tempo

Elimina l'errore residuo accumulando la differenza nel tempo. Serve a dare l'ultima "spinta" per raggiungere la perfezione.\
• Se lo AUMENTI: Elimini più rapidamente l'errore finale (il sistema non si ferma "vicino" al bersaglio, ma ci arriva preciso). Se è troppo alto, causa un forte overshoot (il sistema supera di molto il bersaglio prima di tornare indietro) e continue oscillazioni lente.\
• Se lo DIMINUISCI: Il sistema impiegherà moltissimo tempo (o non ci riuscirà mai) a raggiungere l'esatto valore impostato, fermandosi poco prima o poco dopo.\
• 📌 Quando usarlo: Aumentalo se il sistema si stabilizza a un valore vicino a quello voluto ma non ci arriva mai esattamente (es. imposti 100°C e si ferma fisso a 98°C).

🛑 Azione DERIVATIVA Kd – Il Freno Predittivo

Anticipa il futuro misurando la velocità con cui l'errore si sta riducendo. Funziona come un ammortizzatore.\
• Se lo AUMENTI: Funziona da freno, riduce l'overshoot (evita che il sistema superi il bersaglio) e smorza le oscillazioni rendendo tutto più fluido. Se è troppo alto, rende il sistema iper-sensibile al "rumore" elettrico o di lettura dei sensori, provocando piccoli scatti continui e instabilità.\
• Se lo DIMINUISCI: Il sistema farà frenate troppo brusche o in ritardo, superando la linea di arrivo prima di stabilizzarsi.\
• 📌 Quando usarlo: Aumentalo se il sistema arriva velocemente verso il bersaglio ma lo supera di slancio (overshoot). Nota: in molti sistemi lenti (come il riscaldamento) spesso si lascia a zero.

### Perfezionare il PID durante il collaudo termico

Da **full-1.8** il PID conserva integrale, derivata e fase della finestra SSR quando una rampa passa a un mantenimento allo stesso target (anche tra due mantenimenti allo stesso target). Il conteggio del nuovo hold riparte da zero con letture fresche; non eredita tempo dalla rampa. Nuovo ciclo, STOP, allarme e cambio impostazioni azzerano il PID. Da full-1.10 pausa/ripresa conservano il PID, e da full-1.11 lo conservano anche i passaggi continui verso cooldown o da cooldown a hold allo stesso target. Gli altri passaggi di step ripartono dal PID azzerato.

In salita e mantenimento il taglio temporaneo del comando a **target +1 °C** usa la **media PT100**, la stessa grandezza regolata dal PID, e non cancella più l'integrale. Usare la sonda più calda qui poteva mantenere la media sotto target, anche con un Ki maggiore. L'integrale rimane limitato a `0..maxPower` con integrazione condizionata ai limiti dell'uscita; la derivata è sempre sulla misura, senza impulso dovuto al solo cambio di setpoint. In discesa regolata il comando è zero già a **media ≥ setpoint**, senza cancellare l’integrale. Da **full-1.15**, l’arresto relativo usa **media PT100 > setpoint +10 °C**, con confronto stretto: esattamente +10 °C non fa scattare questo allarme. Da full-1.18 questo arresto è configurabile in Parameters; da full-1.19 è escluso durante il raffreddamento di Programmi e Manuale, mentre resta applicabile in pausa. Il limite assoluto di **415 °C resta su ciascuna sonda**. Il messaggio di allarme riporta media, setpoint e scostamento misurati. `/api/status.pidState` espone `integralPct`, `proportionalPct`, `derivativePct` e `meanLimited` per distinguere regolazione e taglio temporaneo. La modifica richiede una nuova prova termica dopo il caricamento: i test del software non misurano inerzia, uniformità o precisione reale delle sonde.

Una curva vicina alla rampa è un buon punto di partenza; il giudizio richiede anche l'arrivo al target e il mantenimento. Confronta prove con lo stesso programma, carico e condizioni iniziali, modificando un parametro alla volta **a ciclo fermo** e salvandolo sull'ESP. Esporta il CSV della Dashboard: contiene entrambe le PT100, setpoint, PID e comando uscita; annota anche coefficienti, attuatore, finestra e limite comando, che non sono colonne del CSV.

Prima dei guadagni verifica la **finestra di comando**: una finestra lunga può produrre salite a gradini anche con un PID ragionevole. Con un SSR effettivamente verificato, confronta il riferimento iniziale di 10 s con una prova a 5 s, lasciando invariati PID e limite comando. Per il relè meccanico questo firmware mantiene il minimo di 60 s. I tempi vanno scelti per l'attuatore e il circuito reali: [OMRON spiega il rapporto fra periodo, risposta e usura dei contatti](https://www.ia.omron.com/support/guide/53/explanation_of_terms.html).

Dopo aver scelto la finestra, valuta il mantenimento:

- **Errore persistente sotto il target:** se il comando raggiunge spesso il limite massimo, verifica prima capacità di riscaldamento, limite e velocità della rampa. Se non è saturo, valuta piccoli aumenti di Kp per la risposta e di Ki per l'errore a regime, in prove separate.
- **Oscillazioni o superamento del target:** valuta piccoli decrementi di Kp/Ki, uno alla volta; confronta ampiezza, tempo di assestamento e ritardo sulla rampa.
- **Kd:** regola il freno alle variazioni della temperatura. È sensibile al rumore delle sonde; non aumentarlo solo per rendere la curva più liscia.

Nel firmware Kp/Ki/Kd sono guadagni, non tempi integrale/derivativo in secondi: i numeri di un altro regolatore non sono direttamente trasferibili. Riferimenti: [effetti dei termini PID, NI](https://www.ni.com/en/shop/labview/pid-theory-explained.html) e [differenza fra guadagni e tempi, NI](https://knowledge.ni.com/KnowledgeArticleDetails?id=kA00Z0000019L5TSAU). La taratura riguarda il ciclo termico con le protezioni indipendenti verificate; le accensioni dirette della scheda TEST restano prove di cablaggio con le resistenze scollegate.

Il file `examples/recipes.json` contiene il programma iniziale e può essere importato nella dashboard. Il formato è un array di programmi con `id`, `name`, `steps`; ogni step ha `type`, `target` e `rate` oppure `duration` per gli hold. Puoi importare i programmi esportati dal progetto in sede. La memoria usa un namespace proprio (`oven-full`), quindi programmi e PID degli altri firmware non sono caricati automaticamente.

## Allarmi e supervisione delle uscite

Il **fungo EMERGENZA** sempre visibile invia `POST /api/command` con `action: "emergency"`: spegne subito tutti i GPIO gestiti, interrompe ciclo e TEST e blocca nuovi avvii e prove. Il supervisore mantiene il blocco anche dopo STOP; il flag è salvato in NVS e ripristinato al riavvio. **RESET & BLOCK** lo rimuove solo con sonde sane e salvataggio riuscito. Nessun ciclo riprende automaticamente. **STOP** termina normalmente il ciclo e lascia le uscite spente, senza creare questo blocco: un nuovo avvio manuale resta possibile quando pronto.

È un comando software via rete, non un fungo fisico che seziona la potenza: richiede ESP e collegamento funzionanti. Le protezioni hardware indipendenti restano necessarie.

Da **full-1.17** la barra errori offre due azioni distinte, disponibili dalla Dashboard e da Manuale:

- **RESET & BLOCK (rosso):** riconosce l'allarme e lascia il forno fermo. Scarta il punto di ripresa: per ricominciare serve un nuovo avvio.
- **RESET & CONTINUE (arancione):** riconosce un allarme del controllo corrente e riprende lo stesso step, target e rate; nel mantenimento conserva il tempo già maturato in banda. La rampa riparte dal riferimento congelato al guasto, senza avanzare durante l'attesa. In Manuale conserva target, rate e modalità di regolazione/cooldown. Se l'allarme era scattato in PAUSA, il pulsante continua lo step attivo dal riferimento di pausa.

La continuazione usa un punto salvato **solo in RAM**, al momento dell'allarme. Non è disponibile dopo blackout/riavvio, EMERGENZA, STOP, OTA, RESET & BLOCK o modifica di PID/attuatore/limite. Un allarme già presente prima dell'installazione non contiene questo punto e richiede RESET & BLOCK. Il pulsante arancione resta disabilitato se manca lo stato o le condizioni non sono valide; la barra ne mostra il motivo.

Prima di riprendere, l'ESP legge nuovamente entrambe le sonde: devono essere valide, fresche e ciascuna sotto 415 °C; devono inoltre essere rispettate le soglie abilitate in Parameters rispetto al riferimento di ripresa e alla differenza fra sonde. Da full-1.19, riprendendo un cooldown, il ritardo del raffreddamento naturale sopra il riferimento non blocca CONTINUE; restano valide le altre condizioni. Il registro persistente dell'attività deve essere salvato prima di riabilitare le uscite. **RESET & CONTINUE può accendere le resistenze:** è una ripresa esplicita richiesta dall'operatore, non un bypass dell'allarme. Il PID riparte con integrale azzerato. Se il problema ricompare, il controllo si arresta nuovamente.

Nei Programmi l'attesa del reset viene contabilizzata come pausa, senza accumulare tempo attivo o minuti in banda; il report finale conserva i dati precedenti. Il PDF dell'allarme già inviato resta in chat e al nuovo termine viene inviato il report aggiornato; l'identità dell'evento include l'istante finale per distinguere questi due report senza duplicare lo stesso invio. Il Manuale conserva il tempo attivo senza l'attesa. Nessun comando Telegram riattiva il riscaldamento. Le API sono `POST /api/command` con `action: "reset"` e `action: "resetContinue"`; `/api/status.faultResume` indica disponibilità, condizioni e riferimento di ripresa.

La sezione **ERRORI** mostra l’allarme/emergenza attuale, l’ultimo errore che ha interrotto un ciclo e l’ultima interruzione da alimentazione/reset registrata. Gli ultimi due dati sono conservati in NVS quando disponibile. Una causa accensione/brownout indica un possibile blackout o calo di alimentazione; un reset software/watchdog resta classificato come reset. Il tempo tra ultimo checkpoint e riavvio è un **limite massimo stimato**, non la durata esatta del blackout; senza ora NTP/checkpoint la durata è sconosciuta.

I target dei programmi arrivano a **400 °C**. Il limite assoluto è **415 °C** e l’intervallo di lettura arriva a **430 °C**, mantenendo i margini di 15 °C del firmware precedente. Le letture oltre 415 °C restano misurabili per la diagnosi, ma non consentono il riscaldamento. Questi valori software non certificano il limite termico delle PT100 complete di cavi, del forno e dei suoi componenti: verificare le specifiche dell’impianto prima dell’uso ad alta temperatura.

| Condizione | Comportamento |
|---|---|
| Sonda non valida/fault MAX31865 durante ciclo o pausa | Uscite subito spente durante la verifica limitata; prosecuzione dopo una nuova coppia valida, arresto e allarme se il recupero fallisce |
| Differenza sonde >10 °C e ≤20 °C | Riquadro ΔT giallo; l’arresto dipende dalla soglia configurata |
| Differenza sonde >20 °C | Riquadro ΔT rosso; blocco oltre la soglia configurata se abilitato (default >30 °C) |
| Una sonda a 415 °C o più | Arresto e allarme |
| Media PT100 − setpoint oltre la soglia abilitata (default >10 °C) | Arresto e allarme in salita, mantenimento e pausa; escluso durante la discesa di Programmi/Manuale |
| Controllo/letture assenti da 3 s | Supervisore spegne l'uscita e mantiene il blocco |
| Durata del ciclo e delle pause | Nessun limite massimo di ciclo; restano guasti sonde, freschezza, temperature e blocchi del supervisore |
| Comando non valido/non finito nel supervisore | Uscita spenta e blocco |

Nella Dashboard e in Manuale il riquadro principale **ΔT media PT100 − target** mostra uno scarto con segno: negativo sotto il target, positivo sopra; senza una media o un target validi mostra «—». **ΔT sonde** è accanto alle due letture PT100 e conserva le soglie di colore. Il colore del ΔT resta diagnostico e indipendente dalle soglie: da **full-1.18** l’arresto per differenza dipende dal blocco abilitabile in Parameters, inizialmente >30 °C; i firmware full-1.7–1.17 non arrestavano per differenza. A ΔT esattamente 10 °C il riquadro resta normale; a 20 °C resta giallo. I colori usano `--delta-warning-bg`, `--delta-danger-bg`, `--warning` e `--danger` in `data/style.css`. Senza due letture valide il valore è «—»; dopo perdita della connessione, gli ultimi dati sono conservati solo nel breve intervallo già indicato come non aggiornato. Restano gli arresti per guasto, temperatura assoluta e sovratemperatura della media rispetto al setpoint quando abilitata, esclusa durante il raffreddamento (default +10 °C): un ciclo può quindi fermarsi per questi motivi anche con ΔT elevato. Il mantenimento richiede ancora entrambe le sonde entro ±5 °C dal target e non conta il tempo fuori banda.

Le letture sono campionate circa ogni **500 ms** e considerate valide senza fault, con RAW non agli estremi e temperatura tra **−20 e 430 °C**. Una lettura valida non certifica accuratezza, posizione della sonda o continuità di tutti i componenti del circuito.

Da **full-1.22**, la verifica dei transitori riguarda **tutti i fault MAX31865**, singoli o combinati (incluso `0x0C`), e le letture anomale senza codice fault: flag di errore nel dato RTD, RAW nullo/a fondo scala, temperatura non finita o fuori intervallo. Vale in **Programmi, Manuale, pausa, TEST e lettura a riposo**. Non si usano mai i campioni difettosi per il PID. Appena viene rilevata una lettura non valida, tutte le uscite vengono inibite; si acquisiscono nuovamente **entrambe le sonde**, anche quella inizialmente sana. Il controllo può proseguire soltanto dopo **una nuova coppia con entrambe le sonde valide e sotto il limite assoluto**, seguita dai normali controlli termici della coppia. L'integrale PID e lo stato del programma/manuale sono conservati; il tempo di verifica non conta come mantenimento in banda.

Con **10 letture non valide consecutive sulla stessa sonda**, contando quella iniziale, il recupero fallisce: cambiare codice fault non azzera il conteggio. Una lettura valida interrompe la serie della singola sonda; per riprendere basta una nuova coppia in cui entrambe le sonde siano valide e sicure nello stesso tentativo. Non si riutilizza la lettura precedente della sonda inizialmente sana. L'alternanza fault/valido è comunque limitata a **12 nuove coppie** dopo quella iniziale. Le costanti sono in `src/sensor_recovery.h`: `kMaxConsecutiveFaults` e `kMaxConfirmationReads`. Una temperatura numerica attendibile (senza fault e con RAW utilizzabile) a **415 °C o più** non viene trattata come un transitorio: interrompe subito la verifica. Non vengono eliminati gli altri allarmi termici, i blocchi già memorizzati o il controllo di freschezza a 3 s.

L'acquisizione non blocca il loop e usa ora **12 ms di assestamento bias, 70 ms per la conversione a 50 Hz e diagnostica manuale in due fasi con 5 ms di assestamento e 1 ms finale** (`src/rtd_conversion.h`). I 65 ms precedenti erano inferiori ai 66 ms massimi del datasheet; la diagnostica manuale concede ai filtri esterni più tempo dei 100 µs del modo automatico. Una coppia richiede almeno circa 88 ms, oltre alla cadenza del loop; la verifica resta limitata e non attende un minuto. Questi margini non sostituiscono la verifica dei componenti e dei filtri effettivamente montati. Riferimento: [datasheet MAX31865, tempi e diagnostica manuale](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31865.pdf).

Nei TEST la durata e l'heartbeat continuano durante la verifica: il recupero non prolunga né riavvia una prova scaduta; un recupero fallito termina il TEST e lascia l'allarme da riconoscere. Le normali prove diagnostiche restano possibili con sonde assenti dopo il ripristino dei blocchi applicabili. La verifica non cancella blocchi del supervisore. Gli eventi riportano fault/RAW iniziali ed esito; lo stesso errore persistente già registrato non riscrive il log/NVS a ogni verifica. Il report termico usa le coppie finali. I recuperi riusciti restano nel log locale e sono notificati su Telegram solo in **Dettagliata**, senza suono; il normale allarme per recupero fallito viene inviato in tutte le modalità. Il firmware filtra i transitori ma non risolve la loro causa elettrica: il comportamento va collaudato sul forno reale.

**RESET & BLOCK** è nella barra sotto le schede, visibile anche in Manuale, TEST, Programmi e Report e mantenuta in vista durante lo scorrimento. In presenza di allarme, emergenza, interruzione o blocco del supervisore, barra e pulsante diventano rossi e spiegano perché l'avvio è bloccato. L'avviso resta anche dopo il ritorno delle letture valide e si elimina soltanto dopo il riconoscimento riuscito. Il reset richiede il controllo fermo, letture valide e registrazione dello sblocco in memoria; termina eventuali TEST, lascia tutte le uscite spente e non riavvia alcun controllo. Non cancella log, report o la registrazione storica del blackout. Senza collegamento rimane visibile l'ultimo blocco noto e il pulsante è disabilitato. `/api/status.resetRequired` indica il blocco da riconoscere. Per i colori modifica `--error-reset-active-bg` (rosso), `--error-continue-bg` (arancione) e `--error-banner-bg` in `data/style.css`.

La finestra ON/OFF viene eseguita da un **task indipendente**, con verifica circa ogni **10 ms**. Una richiesta HTTP lenta o un blocco del controllo non tengono acceso il riscaldamento oltre il limite di freschezza. Il blocco del supervisore **non si cancella quando ricompaiono letture valide**: devi riconoscere l'allarme a ciclo fermo, con sonde nuovamente valide e nei limiti.

Il ciclo è autonomo: **chiudere la dashboard o perdere il Wi-Fi non lo ferma**. Usa STOP dalla dashboard raggiungibile o l'arresto fisico indipendente. L'heartbeat operatore è richiesto solo per le prove manuali della scheda TEST. Nessun ciclo o test riprende automaticamente dopo un reset. Le protezioni software non sostituiscono quelle hardware.

## Report di fine ciclo

Al completamento, allo **STOP**, a un arresto per **allarme** o per aggiornamento OTA, l'ESP produce il riepilogo del ciclo. La Dashboard mostra **Apri report**; la scheda **Report** permette di consultarlo e scaricarlo direttamente come **PDF A4**, **JSON**, **PNG** oppure **ZIP con CSV e PNG**. Da **full-1.16** il PDF usa riquadri arrotondati, sfondi chiari distinti per tempi/regolazione, temperature e precisione, ed esito evidenziato in verde, ambra o rosso. Contiene curva completa setpoint/media PT100, riepilogo diviso in sezioni (**ciclo/tempi, impostazioni, temperature, precisione, comando e qualità letture**) e una pagina per ogni fase con grafico autonomo e scala adattata; include intestazioni e numeri di pagina. Gli export sono generati nel browser senza Internet, con jsPDF 4.2.1 incorporato nel firmware (licenza in `data/vendor/jspdf-LICENSE.txt`). I report v2 riportano la banda hold ±5 °C; i report v1 già salvati restano leggibili e mantengono l’indicazione ±1 °C. L'API è `GET /api/report`; prima del primo report risponde 404. `/api/status` espone `reportAvailable` e `reportKey`.

Il report include:

- Programma, esito e motivo di conclusione, avvio della scheda e orari NTP se disponibili; senza NTP vengono indicati gli uptime, senza inventare un'ora assoluta.
- Durata totale, tempo attivo, numero/durata delle pause e step completati.
- Kp/Ki/Kd, attuatore, finestra e limite comando **usati nel ciclo**, indipendenti da successive modifiche delle impostazioni.
- Per ciascuna PT100: prima/ultima lettura valida e temperatura minima/massima; differenza massima fra sonde e conteggio dei campioni non validi.
- Errore medio assoluto della media PT100 rispetto al setpoint, massimo ritardo della media, massimo superamento del setpoint da parte della media PT100, comando PID medio e percentuale dei campioni al limite.
- Per ogni fase: target, rate/durata richiesta, esito, tempi attivo/pausa, hold effettivo in banda, statistiche PID ed escursione max−min di ogni PT100 durante il mantenimento. La sua pagina PDF contiene anche il grafico limitato ai campioni della fase e delle sue pause, con tempo dall’inizio della fase. Le fasi non iniziate non hanno dati inventati; le curve mancanti o parziali sono dichiarate.
- Curva del solo ciclo concluso: setpoint e media delle due PT100, comprese pause e cooldown; CSV con entrambe le sonde, media, setpoint, comando, eventi e tempo trascorso nel ciclo. Il PNG ha risoluzione 2400×1000 e sfondo chiaro; il PDF usa un grafico vettoriale.

Le statistiche vengono accumulate dalle letture sul firmware, circa ogni 500 ms: non richiedono una dashboard aperta e non dipendono dalla rotazione dello storico. Temperature e differenza sonde includono le pause. Errori e comando PID usano solo campioni validi di **rampa e hold attivi**, escludendo pause e cooldown. Questa esclusione statistica resta anche nei cooldown regolati di full-1.11; il loro comando PID è visibile nella curva/CSV. Le medie e la saturazione sono calcolate sui campioni, non pesate sulla durata; il comando è quello richiesto prima del nuovo aggiornamento PID. Da **full-1.12**, il superamento è il massimo positivo di **media PT100 − setpoint istantaneo**, anche durante la rampa: per valutare l'arrivo al target consulta anche lo step hold. I nuovi report usano la versione dati 3; i report salvati con versioni 1/2 conservano il valore della sonda più calda e sono indicati come tali, senza ricalcoli dai soli massimi. Da full-1.15 anche l’arresto relativo al setpoint usa la media; da full-1.18 la sua soglia e abilitazione sono configurabili in Parameters (default >10 °C). Il limite assoluto resta individuale a 415 °C. Il tempo in banda riporta lo stesso conteggio del controllo, con entrambe le sonde entro ±5 °C e campioni consecutivi freschi. L'escursione hold include le pause dello step. Il comando non è una misura di energia o potenza elettrica.

L'ESP conserva **un solo report finale**, salvato in NVS una volta a ciclo concluso. Il report precedente resta disponibile durante un nuovo ciclo e viene sostituito alla sua conclusione. Il salvataggio è subordinato allo spazio riservato alle impostazioni e allo stato di sicurezza; se fallisce, la pagina lo segnala come **solo RAM** e puoi scaricarlo. Dopo un riavvio viene recuperato l'ultimo report salvato con successo. Un blackout/reset improvviso non permette di completare il report del ciclo in corso: rimane il riepilogo precedente e si applica la segnalazione di ciclo interrotto già prevista. Lo storico dettagliato resta in RAM: esporta anche il CSV se ti serve analizzare la curva completa.

La curva dell'ultimo report è copiata in una memoria separata al termine del ciclo e resta disponibile durante il ciclo successivo. È campionata circa ogni **10 s**, con eventi di inizio/fine aggiunti, mentre le statistiche usano i campioni circa ogni 500 ms. L'API paginata è `GET /api/report/history?key=<reportKey>&after=<seq>`; un cambio report durante la lettura restituisce 409. La curva **resta in RAM** e va scaricata prima di riavviare; un riepilogo NVS precedente può avere PDF/JSON disponibili senza curva, segnalata chiaramente e senza usare campioni di altri cicli. Se lo storico ha perso l'inizio di un ciclo molto lungo, la curva è indicata come parziale. Nessuna linea viene tracciata attraverso campioni non validi.

Nella Dashboard **Scarica CSV + grafico** esporta un singolo ZIP con CSV completo dello storico disponibile e PNG setpoint/media PT100 riferito agli stessi campioni. Il grafico esportato copre tutto il CSV, anche quando la Dashboard mostra una finestra più corta. Un CSV non può contenere un'immagine: entrambi i file sono inclusi nello ZIP per evitare download multipli bloccati dal browser. La scheda TEST conserva la propria esportazione CSV diagnostica.

## Grafico, log e interruzioni di corrente

Ogni nuovo ciclo e ogni nuova prova TEST apre il grafico **dall'istante di avvio sul bordo sinistro**, lasciando a destra lo spazio per le nuove letture. La finestra resta ancorata all'avvio finché si riempie, poi segue le letture più recenti. Vale anche riaprendo la pagina dopo l'avvio. Lo storico non viene cancellato: **Indietro / Avanti** permettono di vedere i dati precedenti; **Adesso** torna alla finestra corrente. TEST usa una finestra di 5 minuti e recupera le pagine precedenti dall'ESP; nella Dashboard puoi scegliere l'ampiezza o **Tutto** per vedere l'intero storico disponibile. Il riavvio della scheda cancella gli storici in RAM.

Nei grafici Dashboard e TEST il selettore **Asse Y** permette di scegliere una griglia ogni **1, 5 o 10 °C**; la scelta è ricordata nel browser separatamente per ciascun grafico. Le linee a 5 e 10 °C restano più marcate. I limiti della scala si adattano alle temperature visibili e al passo scelto, con almeno due intervalli e un minimo di 5 °C. Le etichette seguono il passo scelto quando c'è spazio; sulle escursioni ampie vengono diradate per evitare sovrapposizioni, mantenendo le linee della griglia selezionata.

Passando il mouse sul grafico, un cursore e un tooltip mostrano **ora e temperature del campione più vicino**: media PT100, setpoint e **ΔT (media PT100 − target)** nella Dashboard e in Manuale, PT100 #1 e #2 in TEST. Lo scarto è negativo se la media è sotto il target e positivo se è sopra; compare solo quando entrambi i valori del campione sono validi. Non viene interpolata una nuova misura e non vengono mostrati valori nella parte futura ancora vuota o per campioni senza letture valide. Il tooltip usa i campioni già caricati e viene aggiornato nel browser, **senza nuove richieste HTTP o lavoro di calcolo sull'ESP**.

Grafico live circa **0,5 s**, dettaglio locale degli ultimi **2 minuti**, storico ESP ogni **10 s** fino a **4320 campioni / circa 12 ore**. I marcatori di nuovo ciclo, STOP, fine e allarme restano nello storico in RAM. La navigazione **Indietro / Avanti / Adesso / Tutto** recupera le pagine quando servono. **Scarica CSV + grafico** recupera automaticamente tutto lo storico ancora disponibile, indipendentemente dalla porzione visibile nel grafico. Temperature non valide sono celle vuote. La colonna `ora_stimata_iso` deriva da NTP, se sincronizzato, oppure dall'orologio del dispositivo che apre la pagina; `uptime_ms` resta il riferimento relativo dell'ESP.

Il log mostra gli ultimi **128 eventi**, compresi cambi uscita e riepiloghi PID; fino a **40 eventi principali** persistono in NVS. Con poco spazio nella NVS condivisa, i log più vecchi vengono eliminati per dare precedenza alle impostazioni e allo stato di sicurezza. Il campo `saved` del JSON indica se la scrittura dell'evento è riuscita, non garantisce che sia ancora conservato dopo la rotazione o la pulizia dei log. I dettagli rapidi dell'uscita non vengono scritti continuamente in flash. Il JSON esportato recupera gli eventi direttamente dall'ESP. Storico e dettagli in RAM si cancellano togliendo corrente o riavviando; esportali prima.

### Blackout e riavvio durante un ciclo

Prima di avviare il riscaldamento, il firmware salva in un unico registro NVS lo stato **ciclo attivo**, il nome del programma e il primo checkpoint UTC, se NTP è disponibile. Durante ciclo e pausa aggiorna il checkpoint ogni **30 s**. Se la scrittura iniziale fallisce, il ciclo non parte; un checkpoint successivo fallito genera un allarme Telegram e mantiene il precedente salvataggio.

Al riavvio i GPIO gestiti vengono inizializzati **spenti prima di Wi-Fi e caricamento delle impostazioni**. Un ciclo rimasto attivo genera un **blocco persistente delle uscite**, anche dei TEST: non riprende automaticamente. Il blocco viene salvato prima di cancellare il vecchio flag di attività e **resta dopo ulteriori riavvii**, fino a **RESET & BLOCK** nella dashboard, con sonde sane e salvataggio riuscito. Anche un registro illeggibile mantiene le uscite bloccate. Il riconoscimento non accende il riscaldamento: per ripartire devi avviare un **nuovo programma dall'inizio**, dopo aver valutato il materiale.

Da **full-1.14**, una normale accensione **a controllo fermo**, dopo STOP o completamento regolarmente salvati e senza blocchi pendenti, riparte in **IDLE con uscita spenta**: non è classificata come blackout, non richiede RESET ERRORI e genera soltanto l'evento ordinario silenzioso **ESP AVVIATO** (da full-1.16 visibile anche in Essenziale, con conferma Wi-Fi e avvio firmware). Vale anche dopo accensioni ripetute. Lo storico di un'interruzione già riconosciuta rimane consultabile nella dashboard e in `/status`, ma è indicato come **Storico**, senza presentarlo come un blocco attuale.

Se l'alimentazione manca mentre un **Programma, una pausa o il Manuale sono attivi**, quando tornano Wi-Fi/Internet e sincronizzazione NTP il bot invia **🚨 BLACKOUT · ALIMENTAZIONE RIPRISTINATA**, con programma interrotto, ultimo checkpoint, intervallo stimato e istruzioni di riconoscimento. Accensione/brownout indicano un'interruzione o calo dell'alimentazione **dell'ESP**: non distinguono un blackout della rete da uno spegnimento manuale durante il controllo. Per spegnere normalmente, usa prima STOP e attendi la conferma del controllo fermo. Un reset software/watchdog durante il controllo invia invece **🚨 ALLARME · RIAVVIO CON CICLO INTERROTTO**. Se il blocco era già pendente prima di questo avvio, il messaggio è **ALLARME · INTERRUZIONE PRECEDENTE NON RICONOSCIUTA**: non dichiara un nuovo blackout e conserva il primo intervallo registrato. Un'emergenza persistente conserva il proprio allarme e blocco. Un TEST rimasto attivo al reset viene segnalato come **TEST INTERROTTO**, con uscite spente e nessuna ripresa; non dispone della stima del programma. Un vero reset **brownout** resta un allarme di alimentazione anche a controllo fermo, senza inventare un ciclo interrotto.

La durata è il **limite massimo approssimativo tra ultimo checkpoint salvato e riavvio**, calcolato sottraendo l'uptime dall'ora NTP: non comprende il tempo successivo necessario a riconnettersi. Con checkpoint regolari include normalmente fino a circa 30 s precedenti alla perdita di alimentazione; un salvataggio fallito può ampliare lo scarto. Il primo orario di ritorno salvato resta conservato anche dopo un altro reset. Senza checkpoint/ora di rete, o con orologio incoerente, la durata è **SCONOSCIUTA**. Non viene presentata come misura esatta del blackout. Finché l'ESP è senza alimentazione non può inviare nulla: per un avviso durante l'interruzione serve un monitor esterno alimentato indipendentemente.

**Prova sul dispositivo reale, con resistenze scollegate:**

1. Carica il firmware, attendi NTP e verifica il bot; avvia un programma di prova.
2. Dopo almeno 30 s interrompi l'alimentazione dell'ESP, attendi qualche minuto e ripristinala.
3. Verifica il comando riscaldamento spento, il ciclo interrotto, il messaggio **🚨 BLACKOUT** con nome e stima e il rifiuto di AVVIA e TEST.
4. Riavvia nuovamente senza riconoscere: il blocco deve restare e non deve comparire alcuna ripresa automatica.
5. Riconosci dalla dashboard: il comando resta spento. Solo un nuovo AVVIA manuale può iniziare un programma.
6. Con controllo fermo e blocco riconosciuto, spegni e riaccendi: deve tornare IDLE, senza un nuovo allarme BLACKOUT o richiesta di RESET ERRORI. Ripeti dopo STOP e dopo il completamento di un programma.

Questa verifica riguarda il comando GPIO. Il comportamento dei contatti e delle uscite durante il boot iniziale richiede il collaudo delle protezioni hardware già previste; non è verificabile con i soli test software.

```sh
curl -s http://192.168.4.1/api/status
curl -s http://192.168.4.1/api/recipes
curl -s http://192.168.4.1/api/logs
```

## Diagnosi allarme PT100 e collegamento intermittente

**«PT100/MAX31865 in errore: uscita spenta»** significa che almeno una lettura non soddisfa i controlli: fault MAX31865 diverso da zero, RAW nullo/a fondo scala, temperatura non finita o fuori intervallo. Durante la verifica limitata le uscite restano spente; da full-1.22 il controllo prosegue senza allarme se una nuova coppia contiene entrambe le sonde valide e sicure. Se la verifica fallisce, il ciclo si arresta e l'allarme resta memorizzato anche se una lettura successiva torna valida: occorre un reset esplicito. Il messaggio e il log ora identificano la PT100 e riportano codice fault/RAW del campione che ha causato l'arresto; il log della sonda include anche gli ohm. In TEST i comandi manuali restano utilizzabili con sonde non valide, come previsto per il collaudo a carico scollegato.

Per distinguere le cause consulta **TEST → Diagnostica PT100** e i Log al momento del fault:

| Bit del fault | Indicazione MAX31865 |
| --- | --- |
| `0x80` / `0x40` | RTD oltre soglia alta / sotto soglia bassa |
| `0x20` | REFIN− maggiore dell'85% di VBIAS |
| `0x10` / `0x08` | REFIN− / RTDIN− sotto l'85% di VBIAS; possibile circuito FORCE− aperto |
| `0x04` | Sovra/sottotensione sugli ingressi del circuito RTD |

I bit possono sommarsi. `0x00` con lettura non valida può indicare RAW anomalo, temperatura fuori intervallo o comunicazione SPI non corretta. Il codice non identifica da solo il componente guasto; cablaggio, morsetti, ponticelli 3 fili, SPI, alimentazioni e disturbi richiedono verifica. In particolare `0x04` **non è una misura della tensione di alimentazione dell'ESP**. Riferimenti: [datasheet MAX31865, registro fault](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX31865.pdf) e [esempio di diagnosi Adafruit](https://learn.adafruit.com/adafruit-max31865-rtd-pt100-amplifier/arduino-code).

**«ESP non raggiungibile»** riguarda invece la risposta HTTP della dashboard. Prima bastava una richiesta fallita per cancellare immediatamente tutti i valori. Le letture HTTP sono ora serializzate, con priorità allo stato; STOP e heartbeat TEST non attendono questa coda. Lo storico viene aggiornato per la vista aperta. Dopo un errore breve, gli ultimi valori rimangono per meno di 6 s dalla risposta valida, con dicitura **«Aggiornamento in ritardo» / «dati non aggiornati»**; il comando uscita diventa subito **sconosciuto**. Dopo 6 s i valori vengono rimossi. Una risposta valida ripristina la visualizzazione. STOP resta tentabile dopo aver ricevuto almeno uno stato: solo la risposta al comando conferma che è stato ricevuto. La perdita dell'heartbeat continua ad arrestare i TEST dopo 2,5 s.

Se **Avvii** resta uguale e l'uptime prosegue, non risulta un reset: controlla segnale Wi-Fi, eventi RETE e prova l'indirizzo IP diretto per distinguere un problema mDNS dai ritardi Wi-Fi/HTTP. Se Avvii aumenta, consulta **Ultimo reset** e gli eventi di avvio; un brownout/reset richiede una verifica dell'alimentazione. Se fault sonda e disconnessione coincidono con la commutazione del relè, alimentazione e disturbi sono ipotesi da verificare, non una diagnosi certa. Il PID del ciclo resta autonomo dalla pagina; il supervisore mantiene il blocco delle uscite se i campioni di controllo invecchiano oltre 3 s.

## Verifica del software

Compila con `pio run -d oven-controller-full`. I test C++ possono essere eseguiti senza scheda:

Con Node.js, `node oven-controller-full/tests/dashboard_test.cjs` dalla cartella firmware verifica la dashboard reale con DOM/canvas simulati: avvio del grafico, navigazione, selezione della griglia Y, tooltip sulle due viste senza richieste aggiuntive, coordinate con canvas ridimensionato, letture vecchie/riconnessione e priorità delle richieste. `python3 -m unittest discover -s local-preview/tests -v` verifica anche le API simulate e la paginazione dello storico TEST.

```sh
cd oven-controller-full
c++ -std=c++11 -Wall -Wextra -Werror tests/profile_test.cpp -o /tmp/oven-full-profile-test
/tmp/oven-full-profile-test
c++ -std=c++11 -Wall -Wextra -Werror tests/heater_guard_test.cpp -o /tmp/oven-full-guard-test
/tmp/oven-full-guard-test
c++ -std=c++11 -Wall -Wextra -Werror tests/log_storage_test.cpp -o /tmp/oven-full-log-test
/tmp/oven-full-log-test
c++ -std=c++11 -Wall -Wextra -Werror tests/output_test.cpp -o /tmp/oven-full-output-test
/tmp/oven-full-output-test
c++ -std=c++11 -Wall -Wextra -Werror tests/output_supervisor_test.cpp -o /tmp/oven-full-supervisor-test
/tmp/oven-full-supervisor-test
c++ -std=c++11 -Wall -Wextra -Werror tests/wifi_recovery_test.cpp -o /tmp/oven-full-wifi-test
/tmp/oven-full-wifi-test
c++ -std=c++11 -Wall -Wextra -Werror tests/power_recovery_test.cpp -o /tmp/oven-full-recovery-test
/tmp/oven-full-recovery-test
c++ -std=c++17 -Wall -Wextra -Werror tests/fault_resume_test.cpp -o /tmp/oven-full-fault-resume-test
/tmp/oven-full-fault-resume-test
c++ -std=c++11 -Wall -Wextra -Werror tests/cycle_report_test.cpp -o /tmp/oven-full-report-test
/tmp/oven-full-report-test
c++ -std=c++11 -Wall -Wextra -Werror tests/sensor_recovery_test.cpp -o /tmp/oven-full-sensor-test
/tmp/oven-full-sensor-test
c++ -std=c++11 -Wall -Wextra -Werror tests/report_curve_test.cpp -o /tmp/oven-full-curve-test
/tmp/oven-full-curve-test
node tests/dashboard_test.cjs
node tests/report_export_test.cjs
c++ -std=c++17 -Wall -Wextra -Werror tests/telegram_policy_test.cpp -o /tmp/oven-full-telegram-test
/tmp/oven-full-telegram-test
c++ -std=c++11 -Wall -Wextra -Werror tests/telegram_delivery_test.cpp -o /tmp/oven-full-delivery-test
c++ -std=c++11 -Wall -Wextra -Werror tests/pid_control_test.cpp -o /tmp/oven-full-pid-test
/tmp/oven-full-pid-test
c++ -std=c++11 -Wall -Wextra -Werror tests/telegram_runtime_test.cpp -o /tmp/oven-full-tg-runtime-test
/tmp/oven-full-tg-runtime-test
/tmp/oven-full-delivery-test
c++ -std=c++17 -Wall -Wextra -Werror tests/report_pdf_test.cpp -o /tmp/oven-full-pdf-test
/tmp/oven-full-pdf-test
c++ -std=c++17 -Wall -Wextra -Werror tests/plot_png_test.cpp -o /tmp/oven-full-plot-test
/tmp/oven-full-plot-test
c++ -std=c++17 -Wall -Wextra -Werror tests/plot_snapshot_test.cpp -o /tmp/oven-full-plot-snapshot-test
/tmp/oven-full-plot-snapshot-test
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Coprono report ciclo/step, pause, hold, esclusione cooldown, campioni non validi, STOP/allarme e overflow dei timer; prove LED/relè e durate prolungate, heartbeat, esclusione TEST/PID, blocco OTA, profili, finestre ON/OFF, pausa/cooldown/STOP, blocco per controllo vecchio, prosecuzione oltre 12 ore, impostazioni non valide, overflow dei timer, conteggio del mantenimento e pulizia selettiva dei log con NVS piena o errori di accesso. La compilazione e i test del software non verificano hardware e risposta termica: quelli restano il collaudo sul campo e la successiva taratura sul forno reale.
