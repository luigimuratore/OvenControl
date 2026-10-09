# Oven Controller · versione completa

Firmware in una **cartella dedicata**, per ESP32-S3 N16R8, due PT100 reali su MAX31865 e comando del driver di riscaldamento. Riprende dashboard, cablaggio e gestione programmi di `oven-controller-site`, aggiungendo diagnostica delle sonde, configurazione dell'attuatore e un supervisore delle uscite indipendente dal server web.

La scheda **TEST** integra il collaudo di `oven-controller-field-test`: puoi verificare sonde, LED, driver e STOP nello stesso firmware, con **resistenze reali scollegate**. La dashboard esegue anche **cicli di riscaldamento**: passa al forno reale dopo aver verificato circuito di comando, cablaggio di potenza e protezioni indipendenti. Il firmware non legge corrente, tensione, feedback dei contatti o un interblocco fisico. Temperatura limite, arresto e sezionamento devono poter agire anche se ESP o attuatore si guastano. Una lettura valida o un LED spento non certificano il circuito di potenza.

## Funzioni disponibili

- Dashboard locale da telefono/PC, senza dipendenze web esterne, con schede **Dashboard / Programmi / TEST / Log / Report**.
- Collaudo completo: LED, sequenze, relè da 0,1 s a 10 min, heartbeat, checklist, grafico PT100 e CSV diagnostico.
- Aggiornamenti Wi-Fi OTA con password, arresto di test/ciclo e blocco delle uscite durante la scrittura.
- Due PT100 reali: temperatura, resistenza calcolata, RAW, RREF e fault descritti.
- Fino a **8 programmi**, ciascuno con **12 step** di rampa, mantenimento e raffreddamento passivo.
- Editor, validazione, salvataggio in memoria ESP e importazione/esportazione JSON.
- PID con anti-windup e derivata filtrata, coefficienti modificabili e persistenti.
- Scelta **SSR / relè meccanico**, finestra di comando e limite di tempo ON configurabili e persistenti.
- Avvio, pausa, ripresa e STOP; STOP disponibile anche nelle schede Programmi e Log.
- Limiti su sonde, differenza, temperatura, sovraelongazione, durata e freschezza del controllo.
- Report automatico di fine ciclo: esito, tempi e pause, temperature, errori sul setpoint, hold in banda, configurazione PID/attuatore e download PDF/JSON e curva CSV/PNG; ultimo riepilogo persistente.
- Grafico live, navigazione dello storico di circa 12 ore, marcatori di inizio/fine/STOP/allarme e CSV completo.
- Log JSON e seriale; fino a **40 eventi principali** restano dopo un riavvio, secondo lo spazio NVS disponibile.
- Rete Wi-Fi diretta più rete del capannone, mDNS, NTP, contatore avvii e rilevazione ciclo interrotto.
- Diagnostica chip ESP, RAM libera/minima, RSSI e età delle letture.

## Anteprima Python locale per i check

Per controllare la dashboard sul Mac **senza ESP**, avvia il [server locale Python](../local-preview/README.md). Richiede Python 3, senza pacchetti aggiuntivi. Dal terminale:

```sh
cd "/Users/gigi/Documents/PROJECTS/Forno oriana/firmware/oven-controller-full"
python3 ../local-preview/server.py --port 8080
```

Apri **<http://localhost:8080/full/>** nel browser. Lascia aperto il terminale mentre fai i check; premi **Ctrl+C** per fermare il server. Puoi aggiungere `--open` al comando per aprire automaticamente il browser. Se la porta 8080 è occupata, usa `--port 8081` e apri `http://localhost:8081/full/`.

Questa anteprima serve per controllare l’interfaccia: temperature e comandi sono simulati, non comunica con l’ESP e non aziona le uscite del forno. I salvataggi restano nella RAM del server e si perdono quando lo riavvii. Non usarla per valutare il PID o le prestazioni termiche reali.

## Caricamento e rete

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

Il file locale `include/wifi_config.h` è ripreso da `oven-controller-site`. Puoi modificarlo per SSID/password del capannone e server NTP; se manca, copialo da `include/wifi_config.example.h`. La rete diretta resta disponibile. La dashboard mostra anche l'IP sulla rete del capannone. Non ci sono login o accessi cloud: usa una LAN controllata. NTP serve a datare gli eventi e a stimare l'intervallo di interruzione; il controllo termico usa `millis()` e funziona anche senza Internet.

La configurazione Wi-Fi viene applicata in RAM a ogni avvio dalle credenziali incluse nel firmware. Se dopo il passaggio dal field-test compare `NOT_ENOUGH_SPACE` seguito da `softAP(): set AP config failed`, carica questa versione aggiornata normalmente: non serve cancellare tutta la flash. All'avvio, e prima di salvare nuovi log, vengono eliminati i log full più vecchi quando necessario per riservare spazio alle impostazioni e allo stato di sicurezza. Programmi, PID, attuatore e dati degli altri firmware non vengono cancellati. Se lo spazio resta insufficiente, gli eventi restano in RAM e il log seriale lo segnala; il Wi-Fi usa comunque la configurazione in RAM.

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

All'avvio dell'aggiornamento autenticato si fermano ciclo e prove; tutte le uscite restano spente fino al riavvio. Anche un aggiornamento fallito o un errore di autenticazione arresta ciclo/prove, senza ripresa automatica. Dashboard e firmware sono aggiornati insieme nelle partizioni OTA già previste. Per cambiare password, usa un caricamento USB.

## Prima prova completa

1. Installa questa versione e completa il collaudo di sonde, LED, driver e STOP nella scheda **TEST**, con resistenze reali scollegate.
2. Premi **Esci da TEST**, poi verifica entrambe le letture reali a riposo.
3. In **Programmi → Attuatore e limite potenza**, scegli il componente realmente installato, una finestra compatibile con il circuito e il limite di comando per la prova. Premi **Salva comando sull'ESP**.
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

## Programmi e PID

L'editor accetta target **0–400 °C**, rampa almeno **0,1 °C/min**, mantenimento **1–360 minuti interi**. Il rate non ha un massimo software, ma il forno può seguire una rampa più lentamente. Dopo il primo step, una rampa deve salire rispetto al target precedente, un cooldown deve scendere e un hold deve conservare il target precedente. Le modifiche ai programmi, al PID e all'attuatore sono consentite solo a ciclo fermo. Le bozze della pagina non modificano la memoria ESP finché non premi Salva; la pagina impedisce di avviare con programmi non salvati.

**Ramp:** il riferimento cresce dalla media delle temperature misurate all'inizio dello step. Lo step termina quando il riferimento ha raggiunto il target e **la sonda più fredda** è almeno a target −1 °C.

**Hold:** il riferimento resta fisso. Si conta solo il tempo tra **due campioni consecutivi** con entrambe le PT100 entro **±5 °C**. Uscire dalla banda sospende il conteggio senza azzerare i minuti già accumulati. Pause, primo campione al ritorno in banda e intervalli senza letture fresche non aggiungono tempo. Il PID continua a regolare fuori banda.

**Cooldown:** il comando riscaldamento resta spento. Il riferimento parte dal setpoint finale dello step precedente, senza rialzi o ribassi dovuti alla temperatura misurata; se il cooldown è il primo step del programma, parte dalla media iniziale delle PT100. Il rate descrive la discesa del riferimento, non una capacità di raffreddamento attivo. Lo step termina quando il riferimento è arrivato al target e la sonda più calda è al massimo a target +1 °C. Il forno può impiegare più tempo del riferimento.

**Tempo rimanente step attivo:** nell’hold indica il tempo ancora richiesto con entrambe le sonde in banda ±5 °C, senza decrementare fuori banda o in pausa. Per rampa e cooldown è una stima al rate impostato che considera sia il riferimento sia la sonda limitante (più fredda in salita, più calda in discesa); non garantisce il tempo reale del forno. In pausa il tempo del riferimento si congela, mentre una nuova misura può aggiornare la stima termica. **Tempo ciclo totale** comprende le pause e rimane visibile alla conclusione.

**PAUSA** spegne l'uscita e congela il tempo dello step; **RIPRENDI** acquisisce nuovamente le sonde e continua lo stesso step. **STOP** conclude il ciclo e lascia spento il riscaldamento. La durata massima di **12 ore include anche le pause**. Avviare dopo uno STOP comincia un nuovo programma dall'inizio.

Il PID lavora sulla media delle PT100. Valori iniziali **Kp 12, Ki 0,015, Kd 50**, limiti impostabili **Kp 0–50, Ki 0–0,5, Kd 0–200**. Anti-windup, filtro della derivata e limite di tempo ON agiscono sul comando; il guadagno adatto dipende dal forno. A una temperatura della sonda più calda pari a setpoint +1 °C o oltre, il comando PID viene azzerato. Il PID non dispone di autotuning automatico.

### Perfezionare il PID durante il collaudo termico

Una curva vicina alla rampa è un buon punto di partenza; il giudizio richiede anche l'arrivo al target e il mantenimento. Confronta prove con lo stesso programma, carico e condizioni iniziali, modificando un parametro alla volta **a ciclo fermo** e salvandolo sull'ESP. Esporta il CSV della Dashboard: contiene entrambe le PT100, setpoint, PID e comando uscita; annota anche coefficienti, attuatore, finestra e limite comando, che non sono colonne del CSV.

Prima dei guadagni verifica la **finestra di comando**: una finestra lunga può produrre salite a gradini anche con un PID ragionevole. Con un SSR effettivamente verificato, confronta il riferimento iniziale di 10 s con una prova a 5 s, lasciando invariati PID e limite comando. Per il relè meccanico questo firmware mantiene il minimo di 60 s. I tempi vanno scelti per l'attuatore e il circuito reali: [OMRON spiega il rapporto fra periodo, risposta e usura dei contatti](https://www.ia.omron.com/support/guide/53/explanation_of_terms.html).

Dopo aver scelto la finestra, valuta il mantenimento:

- **Errore persistente sotto il target:** se il comando raggiunge spesso il limite massimo, verifica prima capacità di riscaldamento, limite e velocità della rampa. Se non è saturo, valuta piccoli aumenti di Kp per la risposta e di Ki per l'errore a regime, in prove separate.
- **Oscillazioni o superamento del target:** valuta piccoli decrementi di Kp/Ki, uno alla volta; confronta ampiezza, tempo di assestamento e ritardo sulla rampa.
- **Kd:** regola il freno alle variazioni della temperatura. È sensibile al rumore delle sonde; non aumentarlo solo per rendere la curva più liscia.

Nel firmware Kp/Ki/Kd sono guadagni, non tempi integrale/derivativo in secondi: i numeri di un altro regolatore non sono direttamente trasferibili. Riferimenti: [effetti dei termini PID, NI](https://www.ni.com/en/shop/labview/pid-theory-explained.html) e [differenza fra guadagni e tempi, NI](https://knowledge.ni.com/KnowledgeArticleDetails?id=kA00Z0000019L5TSAU). La taratura riguarda il ciclo termico con le protezioni indipendenti verificate; le accensioni dirette della scheda TEST restano prove di cablaggio con le resistenze scollegate.

Il file `examples/recipes.json` contiene il programma iniziale e può essere importato nella dashboard. Il formato è un array di programmi con `id`, `name`, `steps`; ogni step ha `type`, `target` e `rate` oppure `duration` per gli hold. Puoi importare i programmi esportati dal progetto in sede. La memoria usa un namespace proprio (`oven-full`), quindi programmi e PID degli altri firmware non sono caricati automaticamente.

## Allarmi e supervisione delle uscite

Il **fungo EMERGENZA** sempre visibile invia `POST /api/command` con `action: "emergency"`: spegne subito tutti i GPIO gestiti, interrompe ciclo e TEST e blocca nuovi avvii e prove. Il supervisore mantiene il blocco anche dopo STOP; il flag è salvato in NVS e ripristinato al riavvio. **Riconosci allarme / interruzione** lo rimuove solo con sonde sane e salvataggio riuscito. Nessun ciclo riprende automaticamente. **STOP** termina normalmente il ciclo e lascia le uscite spente, senza creare questo blocco: un nuovo avvio manuale resta possibile quando pronto.

È un comando software via rete, non un fungo fisico che seziona la potenza: richiede ESP e collegamento funzionanti. Le protezioni hardware indipendenti restano necessarie.

La sezione **ERRORI** mostra l’allarme/emergenza attuale, l’ultimo errore che ha interrotto un ciclo e l’ultima interruzione da alimentazione/reset registrata. Gli ultimi due dati sono conservati in NVS quando disponibile. Una causa accensione/brownout indica un possibile blackout o calo di alimentazione; un reset software/watchdog resta classificato come reset. Il tempo tra ultimo checkpoint e riavvio è un **limite massimo stimato**, non la durata esatta del blackout; senza ora NTP/checkpoint la durata è sconosciuta.

I target dei programmi arrivano a **400 °C**. Il limite assoluto è **415 °C** e l’intervallo di lettura arriva a **430 °C**, mantenendo i margini di 15 °C del firmware precedente. Le letture oltre 415 °C restano misurabili per la diagnosi, ma non consentono il riscaldamento. Questi valori software non certificano il limite termico delle PT100 complete di cavi, del forno e dei suoi componenti: verificare le specifiche dell’impianto prima dell’uso ad alta temperatura.

| Condizione | Comportamento |
|---|---|
| Sonda non valida/fault MAX31865 durante ciclo o pausa | Uscita subito spenta; arresto e allarme salvo verifica limitata del solo `0x04` isolato descritta sotto |
| Differenza sonde oltre 10 °C | Arresto e allarme |
| Una sonda a 415 °C o più | Arresto e allarme |
| Una sonda a setpoint +8 °C o più, fuori dal cooldown | Arresto e allarme |
| Controllo/letture assenti da 3 s | Supervisore spegne l'uscita e mantiene il blocco |
| Durata totale 12 ore, incluse pause | Arresto e allarme |
| Comando non valido/non finito nel supervisore | Uscita spenta e blocco |

Le letture sono campionate circa ogni **500 ms** e considerate valide senza fault, con RAW non agli estremi e temperatura tra **−20 e 430 °C**. Una lettura valida non certifica accuratezza, posizione della sonda o continuità di tutti i componenti del circuito.

Durante ciclo o pausa, il solo fault **`0x04` (sovra/sottotensione)** permette una verifica con **due nuove conversioni consecutive** della sonda interessata, a riscaldamento spento. Ogni conferma deve essere valida, sotto il limite termico e, quando confrontabile, entro i limiti di differenza sonde e sovratemperatura rispetto al setpoint. Si prosegue soltanto dopo conferma e normali controlli su entrambe le sonde. Una conferma non valida o fuori dai limiti interrompe subito la verifica e causa l'arresto, anche se il bit di fault è rientrato. Un altro `0x04` entro **60 s sulla stessa sonda**, altri fault o combinazioni di bit non vengono ritentati e causano l'arresto. Il limite di 60 s è una scelta software prudenziale, non una specifica del sensore. La verifica non riattiva direttamente l'uscita e non cancella blocchi del supervisore. Gli eventi riportano fault/RAW originali ed esito; il report termico usa le coppie finali, mentre i fault recuperati sono documentati nel log. L'intervallo di verifica interrompe il conteggio hold in banda. Nessun recupero si applica alle prove manuali TEST. La modifica richiede collaudo sul forno e non risolve la causa elettrica del disturbo.

La finestra ON/OFF viene eseguita da un **task indipendente**, con verifica circa ogni **10 ms**. Una richiesta HTTP lenta o un blocco del controllo non tengono acceso il riscaldamento oltre il limite di freschezza. Il blocco del supervisore **non si cancella quando ricompaiono letture valide**: devi riconoscere l'allarme a ciclo fermo, con sonde nuovamente valide e nei limiti.

Il ciclo è autonomo: **chiudere la dashboard o perdere il Wi-Fi non lo ferma**. Usa STOP dalla dashboard raggiungibile o l'arresto fisico indipendente. L'heartbeat operatore è richiesto solo per le prove manuali della scheda TEST. Nessun ciclo o test riprende automaticamente dopo un reset. Le protezioni software non sostituiscono quelle hardware.

## Report di fine ciclo

Al completamento, allo **STOP**, a un arresto per **allarme** o per aggiornamento OTA, l'ESP produce il riepilogo del ciclo. La Dashboard mostra **Apri report**; la scheda **Report** permette di consultarlo e scaricarlo direttamente come **PDF A4**, **JSON**, **PNG** oppure **ZIP con CSV e PNG**. Il PDF è impaginato con curva setpoint/media PT100, riepilogo completo, dettaglio degli step e note; include intestazioni e numeri di pagina. Gli export sono generati nel browser senza Internet, con jsPDF 4.2.1 incorporato nel firmware (licenza in `data/vendor/jspdf-LICENSE.txt`). I report v2 riportano la banda hold ±5 °C; i report v1 già salvati restano leggibili e mantengono l’indicazione ±1 °C. L'API è `GET /api/report`; prima del primo report risponde 404. `/api/status` espone `reportAvailable` e `reportKey`.

Il report include:

- Programma, esito e motivo di conclusione, avvio della scheda e orari NTP se disponibili; senza NTP vengono indicati gli uptime, senza inventare un'ora assoluta.
- Durata totale, tempo attivo, numero/durata delle pause e step completati.
- Kp/Ki/Kd, attuatore, finestra e limite comando **usati nel ciclo**, indipendenti da successive modifiche delle impostazioni.
- Per ciascuna PT100: prima/ultima lettura valida e temperatura minima/massima; differenza massima fra sonde e conteggio dei campioni non validi.
- Errore medio assoluto della media PT100 rispetto al setpoint, massimo ritardo della media, massimo superamento del setpoint da parte della sonda più calda, comando PID medio e percentuale dei campioni al limite.
- Per ogni step: target, rate/durata richiesta, esito, tempi attivo/pausa, hold effettivo in banda, statistiche PID ed escursione max−min di ogni PT100 durante il mantenimento.
- Curva del solo ciclo concluso: setpoint e media delle due PT100, comprese pause e cooldown; CSV con entrambe le sonde, media, setpoint, comando, eventi e tempo trascorso nel ciclo. Il PNG ha risoluzione 2400×1000 e sfondo chiaro; il PDF usa un grafico vettoriale.

Le statistiche vengono accumulate dalle letture sul firmware, circa ogni 500 ms: non richiedono una dashboard aperta e non dipendono dalla rotazione dello storico. Temperature e differenza sonde includono le pause. Errori e comando PID usano solo campioni validi di **rampa e hold attivi**, escludendo pause e raffreddamento passivo. Le medie e la saturazione sono calcolate sui campioni, non pesate sulla durata; il comando è quello richiesto prima del nuovo aggiornamento PID. Il superamento è rispetto al **setpoint istantaneo**, anche durante la rampa: per valutare l'arrivo al target consulta anche lo step hold. Il tempo in banda riporta lo stesso conteggio del controllo, con entrambe le sonde entro ±5 °C e campioni consecutivi freschi. L'escursione hold include le pause dello step. Il comando non è una misura di energia o potenza elettrica.

L'ESP conserva **un solo report finale**, salvato in NVS una volta a ciclo concluso. Il report precedente resta disponibile durante un nuovo ciclo e viene sostituito alla sua conclusione. Il salvataggio è subordinato allo spazio riservato alle impostazioni e allo stato di sicurezza; se fallisce, la pagina lo segnala come **solo RAM** e puoi scaricarlo. Dopo un riavvio viene recuperato l'ultimo report salvato con successo. Un blackout/reset improvviso non permette di completare il report del ciclo in corso: rimane il riepilogo precedente e si applica la segnalazione di ciclo interrotto già prevista. Lo storico dettagliato resta in RAM: esporta anche il CSV se ti serve analizzare la curva completa.

La curva dell'ultimo report è copiata in una memoria separata al termine del ciclo e resta disponibile durante il ciclo successivo. È campionata circa ogni **10 s**, con eventi di inizio/fine aggiunti, mentre le statistiche usano i campioni circa ogni 500 ms. L'API paginata è `GET /api/report/history?key=<reportKey>&after=<seq>`; un cambio report durante la lettura restituisce 409. La curva **resta in RAM** e va scaricata prima di riavviare; un riepilogo NVS precedente può avere PDF/JSON disponibili senza curva, segnalata chiaramente e senza usare campioni di altri cicli. Se lo storico ha perso l'inizio di un ciclo molto lungo, la curva è indicata come parziale. Nessuna linea viene tracciata attraverso campioni non validi.

Nella Dashboard **Scarica CSV + grafico** esporta un singolo ZIP con CSV completo dello storico disponibile e PNG setpoint/media PT100 riferito agli stessi campioni. Il grafico esportato copre tutto il CSV, anche quando la Dashboard mostra una finestra più corta. Un CSV non può contenere un'immagine: entrambi i file sono inclusi nello ZIP per evitare download multipli bloccati dal browser. La scheda TEST conserva la propria esportazione CSV diagnostica.

## Grafico, log e interruzioni di corrente

Ogni nuovo ciclo e ogni nuova prova TEST apre il grafico **dall'istante di avvio sul bordo sinistro**, lasciando a destra lo spazio per le nuove letture. La finestra resta ancorata all'avvio finché si riempie, poi segue le letture più recenti. Vale anche riaprendo la pagina dopo l'avvio. Lo storico non viene cancellato: **Indietro / Avanti** permettono di vedere i dati precedenti; **Adesso** torna alla finestra corrente. TEST usa una finestra di 5 minuti e recupera le pagine precedenti dall'ESP; nella Dashboard puoi scegliere l'ampiezza o **Tutto** per vedere l'intero storico disponibile. Il riavvio della scheda cancella gli storici in RAM.

Nei grafici Dashboard e TEST il selettore **Asse Y** permette di scegliere una griglia ogni **1, 5 o 10 °C**; la scelta è ricordata nel browser separatamente per ciascun grafico. Le linee a 5 e 10 °C restano più marcate. I limiti della scala si adattano alle temperature visibili e al passo scelto, con almeno due intervalli e un minimo di 5 °C. Le etichette seguono il passo scelto quando c'è spazio; sulle escursioni ampie vengono diradate per evitare sovrapposizioni, mantenendo le linee della griglia selezionata.

Passando il mouse sul grafico, un cursore e un tooltip mostrano **ora e temperature del campione più vicino**: media PT100 e setpoint nella Dashboard, PT100 #1 e #2 in TEST. Non viene interpolata una nuova misura e non vengono mostrati valori nella parte futura ancora vuota o per campioni senza letture valide. Il tooltip usa i campioni già caricati e viene aggiornato nel browser, **senza nuove richieste HTTP o lavoro di calcolo sull'ESP**.

Grafico live circa **0,5 s**, dettaglio locale degli ultimi **2 minuti**, storico ESP ogni **10 s** fino a **4320 campioni / circa 12 ore**. I marcatori di nuovo ciclo, STOP, fine e allarme restano nello storico in RAM. La navigazione **Indietro / Avanti / Adesso / Tutto** recupera le pagine quando servono. **Scarica CSV + grafico** recupera automaticamente tutto lo storico ancora disponibile, indipendentemente dalla porzione visibile nel grafico. Temperature non valide sono celle vuote. La colonna `ora_stimata_iso` deriva da NTP, se sincronizzato, oppure dall'orologio del dispositivo che apre la pagina; `uptime_ms` resta il riferimento relativo dell'ESP.

Il log mostra gli ultimi **128 eventi**, compresi cambi uscita e riepiloghi PID; fino a **40 eventi principali** persistono in NVS. Con poco spazio nella NVS condivisa, i log più vecchi vengono eliminati per dare precedenza alle impostazioni e allo stato di sicurezza. Il campo `saved` del JSON indica se la scrittura dell'evento è riuscita, non garantisce che sia ancora conservato dopo la rotazione o la pulizia dei log. I dettagli rapidi dell'uscita non vengono scritti continuamente in flash. Il JSON esportato recupera gli eventi direttamente dall'ESP. Storico e dettagli in RAM si cancellano togliendo corrente o riavviando; esportali prima.

Durante ciclo o pausa, con NTP sincronizzato, viene salvato un checkpoint ogni **30 s**. Al riavvio un ciclo precedentemente attivo è segnalato come **interrotto**, con uscita spenta e riconoscimento manuale richiesto. Quando torna NTP, l'intervallo tra ultimo checkpoint e avvio è mostrato come **limite massimo approssimativo**, non durata esatta del blackout. Senza checkpoint/ora di rete la durata è sconosciuta. Per ripartire devi avviare un **nuovo programma dall'inizio**, dopo aver valutato il materiale.

```sh
curl -s http://192.168.4.1/api/status
curl -s http://192.168.4.1/api/recipes
curl -s http://192.168.4.1/api/logs
```

## Diagnosi allarme PT100 e collegamento intermittente

**«PT100/MAX31865 in errore: uscita spenta»** significa che almeno una lettura non soddisfa i controlli: fault MAX31865 diverso da zero, RAW nullo/a fondo scala, temperatura non finita o fuori intervallo. Durante un ciclo o una pausa l'arresto resta immediato e l'allarme resta memorizzato anche se la lettura successiva torna valida: non c'è ripresa automatica. Il messaggio e il log ora identificano la PT100 e riportano codice fault/RAW del campione che ha causato l'arresto; il log della sonda include anche gli ohm. In TEST i comandi manuali restano utilizzabili con sonde non valide, come previsto per il collaudo a carico scollegato.

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
c++ -std=c++11 -Wall -Wextra -Werror tests/cycle_report_test.cpp -o /tmp/oven-full-report-test
/tmp/oven-full-report-test
c++ -std=c++11 -Wall -Wextra -Werror tests/sensor_recovery_test.cpp -o /tmp/oven-full-sensor-test
/tmp/oven-full-sensor-test
c++ -std=c++11 -Wall -Wextra -Werror tests/report_curve_test.cpp -o /tmp/oven-full-curve-test
/tmp/oven-full-curve-test
node tests/dashboard_test.cjs
node tests/report_export_test.cjs
```

Coprono report ciclo/step, pause, hold, esclusione cooldown, campioni non validi, STOP/allarme e overflow dei timer; prove LED/relè e durate prolungate, heartbeat, esclusione TEST/PID, blocco OTA, profili, finestre ON/OFF, pausa/cooldown/STOP, blocco per controllo vecchio, durata massima, impostazioni non valide, overflow dei timer, conteggio del mantenimento e pulizia selettiva dei log con NVS piena o errori di accesso. La compilazione e i test del software non verificano hardware e risposta termica: quelli restano il collaudo sul campo e la successiva taratura sul forno reale.
