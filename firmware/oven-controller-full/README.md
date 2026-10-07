# Oven Controller · versione completa

Firmware in una **cartella dedicata**, per ESP32-S3 N16R8, due PT100 reali su MAX31865 e comando del driver di riscaldamento. Riprende dashboard, cablaggio e gestione ricette di `oven-controller-site`, aggiungendo diagnostica delle sonde, configurazione dell'attuatore e un supervisore delle uscite indipendente dal server web.

Per il collaudo senza resistenze usa prima [oven-controller-field-test](../oven-controller-field-test/README.md). Questa versione esegue **cicli di riscaldamento**: passa al forno reale dopo aver verificato circuito di comando, cablaggio di potenza e protezioni indipendenti. Il firmware non legge corrente, tensione, feedback dei contatti o un interblocco fisico. Temperatura limite, arresto e sezionamento devono poter agire anche se ESP o attuatore si guastano. Una lettura valida o un LED spento non certificano il circuito di potenza.

## Funzioni disponibili

- Dashboard locale da telefono/PC, senza dipendenze web esterne.
- Due PT100 reali: temperatura, resistenza calcolata, RAW, RREF e fault descritti.
- Fino a **8 ricette**, ciascuna con **12 step** di rampa, mantenimento e raffreddamento passivo.
- Editor, validazione, salvataggio in memoria ESP e importazione/esportazione JSON.
- PID con anti-windup e derivata filtrata, coefficienti modificabili e persistenti.
- Scelta **SSR / relè meccanico**, finestra di comando e limite di tempo ON configurabili e persistenti.
- Avvio, pausa, ripresa e STOP; STOP disponibile anche nelle schede Ricette e Log.
- Limiti su sonde, differenza, temperatura, sovraelongazione, durata e freschezza del controllo.
- Grafico live, navigazione dello storico di circa 12 ore, marcatori di inizio/fine/STOP/allarme e CSV completo.
- Log JSON e seriale; gli ultimi **40 eventi principali** restano dopo un riavvio.
- Rete Wi-Fi diretta più rete del capannone, mDNS, NTP, contatore avvii e rilevazione ciclo interrotto.
- Diagnostica chip ESP, RAM libera/minima, RSSI e età delle letture.

## Caricamento e rete

Apri questa cartella in PlatformIO. Dal terminale nella cartella `firmware`:

```sh
pio run -d oven-controller-full
pio run -d oven-controller-full -t upload
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

## Collegamenti

| Funzione | GPIO |
|---|---:|
| MAX31865 #1 / #2 · CS | 14 / 10 |
| SPI · SCK / MISO / MOSI | 13 / 12 / 11 |
| Comando transistor tramite 5 kΩ | 4 |
| LED rosso / verde | 6 / 7 |

Click a **3,3 V**, con GND comune. SDO delle Click va al MISO ESP, SDI al MOSI. Mantieni il circuito di polarizzazione del transistor, compresa la resistenza verso GND per lo stato spento all'avvio. GPIO35/36 restano esclusi perché occupati dalla PSRAM sul modulo N16R8.

Pin e polarità sono in `include/hardware_config.h`, ripreso dal firmware di collaudo. Valori iniziali: **PT100 100 Ω nominali**, **RREF 470 Ω per ciascuna Click**, **3 fili**, uscite attive alte. Verifica RREF e ponticelli effettivi delle Click: la conversione può sembrare valida anche con una RREF errata. Per cambiare il numero di fili modifica entrambe le chiamate `begin(MAX31865_3WIRE)` in `src/main.cpp` e adegua il cablaggio. Filtro MAX31865 a **50 Hz**, bias disabilitato dopo ogni acquisizione.

Il verde indica server/rete avviati senza allarme. Il rosso segue il **comando** di riscaldamento GPIO4. Non sono presenti prove manuali dei LED o impulsi manuali dell'uscita nella versione completa: per quelle usa la cartella di collaudo.

## Prima prova completa

1. Completa prima il collaudo di sonde, LED, driver e STOP con il firmware di test.
2. Installa questa versione e verifica entrambe le letture reali a riposo.
3. In **Ricette → Attuatore e limite potenza**, scegli il componente realmente installato, una finestra compatibile con il circuito e il limite di comando per la prova. Premi **Salva comando sull'ESP**.
4. Controlla la ricetta iniziale **Prova a 50 C**: rampa a **50 °C a 1 °C/min**, mantenimento **10 min in banda**, raffreddamento a **35 °C con riferimento 1 °C/min**. Questo profilo è un punto di partenza per il collaudo termico, non una ricetta di produzione validata.
5. Conferma nella dashboard **collaudo hardware concluso e protezioni indipendenti verificate**, poi avvia il ciclo sotto sorveglianza. La conferma è richiesta per ogni nuovo avvio e non viene conservata al refresh.
6. Verifica risposta termica, pausa, ripresa, STOP e allarmi; salva CSV e log prima di spegnere. Tara PID e profili con prove sul forno effettivo prima di usare materiali di produzione.

Le impostazioni iniziali del comando sono **relè meccanico**, **finestra 60 s**, **massimo 30% ON**. Non identificano automaticamente il tuo attuatore. Se il circuito usa un SSR, selezionalo e imposta una finestra adatta; puoi partire dal riferimento **10 s** degli altri firmware e tarare la risposta. I valori consentiti sono **1–300 s per SSR**, **60–300 s per relè** e **1–100%** di comando massimo. La scelta della finestra e dell'attuatore deve rispettare i loro limiti reali. Con una finestra di 60 s, un comando del 30% significa 18 s ON e 42 s OFF: non è una misura del 30% della potenza elettrica.

## Ricette e PID

L'editor accetta target **0–190 °C**, rampa almeno **0,1 °C/min**, mantenimento **1–360 minuti interi**. Il rate non ha un massimo software, ma il forno può seguire una rampa più lentamente. Dopo il primo step, una rampa deve salire rispetto al target precedente, un cooldown deve scendere e un hold deve conservare il target precedente. Le modifiche alle ricette, al PID e all'attuatore sono consentite solo a ciclo fermo. Le bozze della pagina non modificano la memoria ESP finché non premi Salva; la pagina impedisce di avviare con ricette non salvate.

**Ramp:** il riferimento cresce dalla media delle temperature misurate all'inizio dello step. Lo step termina quando il riferimento ha raggiunto il target e **la sonda più fredda** è almeno a target −1 °C.

**Hold:** il riferimento resta fisso. Si conta solo il tempo tra **due campioni consecutivi** con entrambe le PT100 entro **±1 °C**. Uscire dalla banda sospende il conteggio senza azzerare i minuti già accumulati. Pause, primo campione al ritorno in banda e intervalli senza letture fresche non aggiungono tempo. Il PID continua a regolare fuori banda.

**Cooldown:** il comando riscaldamento resta spento. Il rate descrive la discesa del riferimento, non una capacità di raffreddamento attivo. Lo step termina quando il riferimento è arrivato al target e la sonda più calda è al massimo a target +1 °C. Il forno può impiegare più tempo del riferimento.

**PAUSA** spegne l'uscita e congela il tempo dello step; **RIPRENDI** acquisisce nuovamente le sonde e continua lo stesso step. **STOP** conclude il ciclo e lascia spento il riscaldamento. La durata massima di **12 ore include anche le pause**. Avviare dopo uno STOP comincia una nuova ricetta dall'inizio.

Il PID lavora sulla media delle PT100. Valori iniziali **Kp 12, Ki 0,015, Kd 50**, limiti impostabili **Kp 0–50, Ki 0–0,5, Kd 0–200**. Anti-windup, filtro della derivata e limite di tempo ON agiscono sul comando; il guadagno adatto dipende dal forno. A una temperatura della sonda più calda pari a setpoint +1 °C o oltre, il comando PID viene azzerato. Il PID non dispone di autotuning automatico.

Il file `examples/recipes.json` contiene la ricetta iniziale e può essere importato nella dashboard. Il formato è un array di ricette con `id`, `name`, `steps`; ogni step ha `type`, `target` e `rate` oppure `duration` per gli hold. Puoi importare le ricette esportate dal progetto in sede. La memoria usa un namespace proprio (`oven-full`), quindi ricette e PID degli altri firmware non sono caricati automaticamente.

## Allarmi e supervisione delle uscite

| Condizione | Comportamento |
|---|---|
| Sonda non valida/fault MAX31865 durante ciclo o pausa | Arresto, allarme da riconoscere |
| Differenza sonde oltre 10 °C | Arresto e allarme |
| Una sonda a 205 °C o più | Arresto e allarme |
| Una sonda a setpoint +8 °C o più, fuori dal cooldown | Arresto e allarme |
| Controllo/letture assenti da 3 s | Supervisore spegne l'uscita e mantiene il blocco |
| Durata totale 12 ore, incluse pause | Arresto e allarme |
| Comando non valido/non finito nel supervisore | Uscita spenta e blocco |

Le letture sono campionate circa ogni **500 ms** e considerate valide senza fault, con RAW non agli estremi e temperatura tra **−20 e 220 °C**. Una lettura valida non certifica accuratezza, posizione della sonda o continuità di tutti i componenti del circuito.

La finestra ON/OFF viene eseguita da un **task indipendente**, con verifica circa ogni **10 ms**. Una richiesta HTTP lenta o un blocco del controllo non tengono acceso il riscaldamento oltre il limite di freschezza. Il blocco del supervisore **non si cancella quando ricompaiono letture valide**: devi riconoscere l'allarme a ciclo fermo, con sonde nuovamente valide e nei limiti.

Il ciclo è autonomo: **chiudere la dashboard o perdere il Wi-Fi non lo ferma**. Usa STOP dalla dashboard raggiungibile o l'arresto fisico indipendente. Questa versione non usa l'heartbeat operatore del firmware di collaudo, né riprende automaticamente dopo un reset. Le protezioni software non sostituiscono quelle hardware.

## Grafico, log e interruzioni di corrente

Grafico live circa **0,5 s**, dettaglio locale degli ultimi **2 minuti**, storico ESP ogni **10 s** fino a **4320 campioni / circa 12 ore**. I marcatori di nuovo ciclo, STOP, fine e allarme restano nello storico in RAM. La navigazione **Indietro / Avanti / Adesso / Tutto** recupera le pagine quando servono. **Esporta CSV completo** recupera automaticamente tutto lo storico ancora disponibile, indipendentemente dalla porzione visibile nel grafico. Temperature non valide sono celle vuote. La colonna `ora_stimata_iso` deriva da NTP, se sincronizzato, oppure dall'orologio del dispositivo che apre la pagina; `uptime_ms` resta il riferimento relativo dell'ESP.

Il log mostra gli ultimi **100 eventi**, compresi cambi uscita e riepiloghi PID; i **40 eventi principali** persistono in NVS. I dettagli rapidi dell'uscita non vengono scritti continuamente in flash. Il JSON esportato recupera gli eventi direttamente dall'ESP. Storico e dettagli in RAM si cancellano togliendo corrente o riavviando; esportali prima.

Durante ciclo o pausa, con NTP sincronizzato, viene salvato un checkpoint ogni **30 s**. Al riavvio un ciclo precedentemente attivo è segnalato come **interrotto**, con uscita spenta e riconoscimento manuale richiesto. Quando torna NTP, l'intervallo tra ultimo checkpoint e avvio è mostrato come **limite massimo approssimativo**, non durata esatta del blackout. Senza checkpoint/ora di rete la durata è sconosciuta. Per ripartire devi avviare una **nuova ricetta dall'inizio**, dopo aver valutato il materiale.

```sh
curl -s http://192.168.4.1/api/status
curl -s http://192.168.4.1/api/recipes
curl -s http://192.168.4.1/api/logs
```

## Verifica del software

Compila con `pio run -d oven-controller-full`. I test C++ possono essere eseguiti senza scheda:

```sh
cd oven-controller-full
c++ -std=c++11 -Wall -Wextra -Werror tests/profile_test.cpp -o /tmp/oven-full-profile-test
/tmp/oven-full-profile-test
c++ -std=c++11 -Wall -Wextra -Werror tests/heater_guard_test.cpp -o /tmp/oven-full-guard-test
/tmp/oven-full-guard-test
```

Coprono profili, finestre ON/OFF, pausa/cooldown/STOP, blocco per controllo vecchio, durata massima, impostazioni non valide, overflow dei timer e conteggio del mantenimento. La compilazione e i test del software non verificano hardware e risposta termica: quelli restano il collaudo sul campo e la successiva taratura sul forno reale.
