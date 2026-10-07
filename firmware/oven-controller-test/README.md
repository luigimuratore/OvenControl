# Test ESP32-S3 N16R8

Firmware PlatformIO per provare **2 sonde PT100**, i LED e l'uscita relè **senza collegare le resistenze del forno**. L'ESP crea la rete Wi-Fi `OvenController-Test` (password `oven12345`); la dashboard è su `http://192.168.4.1`. Se si collega al Wi-Fi del capannone, la stessa dashboard è raggiungibile anche dall'IP assegnato all'ESP su quella rete.

## Collegamenti dello schema

| Funzione | GPIO |
|---|---:|
| RTD Click 1: CS | 14 |
| RTD Click 2: CS | 10 |
| Entrambe: SCK / SDO→ESP / SDI←ESP | 13 / 12 / 11 |
| Base del transistor, tramite 5 kΩ | 4 |
| LED **verde** | **7** |
| LED **rosso** | **6** |

Le Click usano **3,3 V e GND**. Il verde acceso indica ESP e server avviati senza allarmi, anche se le PT100 non sono collegate; il rosso si accende quando GPIO4 comanda il relè. Lo schema mostra anche una resistenza fra base e GND: va mantenuta per tenere il transistor spento durante l'avvio. I GPIO35/36 non vanno usati sui moduli N16R8 perché sono occupati dalla PSRAM.

## Caricare e provare

Apri questa cartella in PlatformIO, collega l'ESP via USB e carica:

```sh
pio run -t upload
```

La pagina web è già inclusa nel firmware. Connettiti alla rete dell'ESP e apri l'indirizzo sopra.

- **Senza PT100:** scegli **Sonde simulate**, poi **AVVIA TEST**. La temperatura finta sale quando il relè è acceso; i pulsanti **±1 °C finto** permettono di provare subito lo spegnimento a 36 °C. Il GPIO4 comanda comunque il relè fisico: usa solo un carico di prova a bassa tensione.
- **Con le PT100:** ferma il test, scegli **PT100 reali** e riavvialo. Questa è la modalità predefinita dopo ogni riavvio dell'ESP.

Il target è **35 °C fisso**. Il PID dosa il relè in finestre di 5 secondi: per esempio, un comando del **18%** significa circa **0,9 s acceso e 4,1 s spento** in ogni finestra. È normale che il LED rosso segua questi impulsi. Se una lettura arriva a **36 °C**, il relè e il LED rosso si spengono; il controllo riprende quando entrambe tornano a **35,5 °C o meno**. Con un relè meccanico, prima dell'uso continuativo va scelta una commutazione meno frequente o un attuatore adatto.

Il grafico live si aggiorna circa ogni **0,5 secondi** e mostra con più dettaglio gli ultimi **2 minuti** della pagina aperta. L'ESP conserva un campione ogni **10 secondi** fino a **6 ore**: dopo un refresh della pagina la curva resta, ma il dettaglio degli ultimi 2 minuti torna alla risoluzione di 10 secondi. **Indietro** recupera i dati precedenti e **Tutto** carica l'intero storico solo quando serve. La barra, **Avanti / Adesso** e **15 min / 30 min / 1 ora** permettono di navigare. **Riavviare o togliere corrente all'ESP cancella lo storico**, che è in RAM. Il CSV esporta i campioni caricati; per includere tutte le 6 ore scegli prima **Tutto** e attendi il caricamento.

Se una sonda dà errore, le due letture differiscono oltre 10 °C o premi **STOP**, l'uscita si spegne. Il registro nella dashboard conserva gli eventi principali. Per il test usa solo un carico a bassa tensione, mai le resistenze del forno.

La dashboard mostra anche la **temperatura interna dell'ESP** (campionata ogni 10 s) per osservare se sale durante l'uso. Non è la temperatura dell'aria attorno al modulo né quella del regolatore 3,3 V. Il loop ora lascia più tempo alla CPU e aggiorna il LED verde solo quando cambia stato; campionamento PT100 e finestra PID non cambiano.

## Wi-Fi capannone e corrente

Apri `include/wifi_config.h` (se manca, copialo da `include/wifi_config.example.h`), inserisci SSID/password del capannone e un server NTP raggiungibile (Internet o locale), poi ricarica il firmware. La rete privata `OvenController-Test` resta attiva.

Quando l'ESP è connesso, la sezione **Accesso alla dashboard** mostra il suo indirizzo `http://IP-ESP/`; lo trovi anche nel log seriale `[RETE]`. Aprilo da telefono o PC collegato al **Wi-Fi del capannone**, senza collegarti alla rete dell'ESP. Puoi provare anche `http://oven-controller.local/`, se la rete supporta mDNS; l'IP è il riferimento più sicuro e può cambiare dopo un riavvio. Se l'IP non si apre da un dispositivo sulla stessa rete, verifica l'isolamento dei client nel router. Questo firmware di test non richiede un login per comandare il relè dalla LAN.

Durante il test l'ESP salva l'ora NTP ogni **30 secondi**. Se si spegne o si resetta, al riavvio **lascia il relè spento** e segnala “test interrotto”. Quando NTP torna disponibile mostra l'intervallo tra l'ultima ora salvata e il riavvio: è **un limite massimo approssimativo**, non la durata esatta del blackout. Se NTP manca o il test finisce prima del primo salvataggio, la durata resta sconosciuta. Un reset software produce lo stesso avviso. Per la misura esatta serve un RTC con batteria.

## Log e tuning PID

Apri la scheda **Log** della dashboard: mostra cambi modalità, avvio, commutazioni del relè, riepiloghi PID, target, STOP/errori e riavvii. Visualizza gli ultimi **100 eventi**; i **40 eventi principali** restano salvati anche dopo il riavvio, mentre i dettagli rapidi del relè/PID sono validi solo per l'accensione corrente. Puoi esportare i log in JSON. Nel futuro firmware a ricette, “ciclo finito” sarà registrato alla fine dell'ultimo step; questo test continua invece a regolare.

Dal terminale, mentre sei collegato al Wi-Fi dell'ESP: `curl -s http://192.168.4.1/api/logs | python3 -m json.tool`. Per vedere gli stessi messaggi sulla seriale USB: `pio device monitor -b 115200`.

I coefficienti `PID_KP`, `PID_KI`, `PID_KD` sono in `src/main.cpp` e sono **solo valori iniziali**. Il pulsante **Esporta CSV** scarica temperature e comando PID per analizzare la risposta. Toccare le sonde verifica letture e spegnimento; **non permette di tarare il PID del forno**, perché manca la risposta termica reale delle resistenze. Per quella taratura serviranno prove controllate sul forno con le protezioni indipendenti già installate.
