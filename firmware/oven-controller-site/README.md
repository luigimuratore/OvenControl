# Oven Controller · prova in sede

Firmware separato dal [test da banco](../oven-controller-test/README.md) per **ESP32-S3 N16R8**, due PT100 a 3 fili con due MAX31865 e uscita per il driver SSR. La dashboard è inclusa nel firmware: rete `OvenController-Site`, password `oven12345`, indirizzo `http://192.168.4.1/`. Se il Wi-Fi del capannone è configurato, la stessa pagina è disponibile anche all'IP mostrato nella dashboard, da un dispositivo sulla stessa rete; può funzionare anche `http://oven-site.local/`.

## Collegamenti

| Funzione | GPIO |
|---|---:|
| MAX31865 #1 CS / #2 CS | 14 / 10 |
| SCK / SDO→ESP / SDI←ESP | 13 / 12 / 11 |
| Base transistor tramite 5 kΩ | 4 |
| LED rosso / verde | 6 / 7 |

Le Click sono alimentate a **3,3 V** con massa comune. Il verde indica ESP/server avviati senza allarme; il rosso segue **GPIO4 alto**, cioè il comando al driver SSR. Il rosso spento non prova da solo che il circuito di potenza sia aperto: l'SSR può guastarsi chiuso. Prima di alimentare le resistenze occorrono protezione di sovratemperatura, arresto/interblocco e sezionamento indipendenti dall'ESP, oltre alla verifica del cablaggio di potenza da personale qualificato. Il firmware non legge il feedback del contattore né misura la corrente alle resistenze.

## Caricamento e accesso

Apri questa cartella in PlatformIO, collega **questa** ESP via USB e usa `pio run` per compilare; `pio run -t upload` solo quando intendi sostituire il firmware sulla scheda. Il progetto da banco resta separato. Per il Wi-Fi del capannone usa `include/wifi_config.h` (se manca, copia `include/wifi_config.example.h`); inserisci SSID, password e un server NTP raggiungibile. Non serve un PIN operatore. Chiunque raggiunga la dashboard sulla rete può inviare comandi: usa una rete del capannone controllata.

## Ciclo e ricette

La dashboard permette di modificare fino a **8 ricette**, con **12 step** ciascuna, salvate nella memoria dell'ESP. Puoi importarle/esportarle in JSON. La ricetta iniziale è una prova prudente: **rampa a 50 °C a 1 °C/min**, **hold 10 min**, **cooldown a 35 °C con riferimento 1 °C/min**. I limiti software sono target fino a **190 °C**, rate minimo **0,1 °C/min senza massimo software**, hold **1–360 min** e ciclo massimo **12 ore**. Un rate elevato fa avanzare più rapidamente il setpoint, ma non garantisce che il forno riesca a seguirlo. Controlla il materiale e i limiti reali del forno prima di scegliere il profilo.

- **Ramp:** il setpoint cresce dalla temperatura misurata all'inizio dello step secondo il rate impostato in **°C/min**. Lo step termina quando il tempo di rampa è trascorso **e** la temperatura reale è entro 1 °C dal target.
- **Hold:** il tempo avanza solo quando **entrambe** le PT100 sono entro **±1 °C** dal target. Fuori banda il tempo non avanza; il PID continua a regolare.
- **Cooldown:** l'SSR rimane **spento**. Il rate disegna un riferimento temporale e impedisce di dichiarare lo step concluso prima del tempo minimo; il forno può raffreddarsi più lentamente e questo cablaggio non può forzare la velocità di raffreddamento.
- **PAUSA** spegne l'SSR e congela il tempo dello step; **RIPRENDI** continua lo stesso step. **STOP** spegne l'uscita e conclude il ciclo. Dopo un riavvio non c'è ripresa automatica.

Il PID comanda l'SSR con una finestra di **10 secondi**. I coefficienti iniziali **Kp 12, Ki 0,015, Kd 50** sono un punto di partenza: nella scheda **Ricette** puoi tararli a ciclo fermo e salvarli sull'ESP. Per la taratura usa prima una ricetta a bassa temperatura, log/CSV e sorveglianza; nessun valore preimpostato garantisce precisione sul forno reale. Una sonda non valida, una differenza tra PT100 oltre **10 °C**, una temperatura di **205 °C** o più, oppure oltre **8 °C** sopra il setpoint corrente bloccano l'uscita e richiedono il riconoscimento dell'allarme. Le protezioni software non sostituiscono quelle fisiche.

## Log, grafico e corrente

La scheda **Log** mostra avvio/fine ciclo, passaggi di step, pausa, STOP, PID, rete e allarmi; gli ultimi **40 eventi principali** restano in memoria dopo il riavvio. Dalla seriale USB: `pio device monitor -b 115200`. Dal Wi-Fi: `curl -s http://192.168.4.1/api/logs | python3 -m json.tool`.

Il grafico si aggiorna dal vivo circa ogni **0,5 s**. L'ESP mantiene campioni ogni **10 s** per **12 ore**: ricaricare la pagina recupera lo storico dall'ESP, mentre togliere corrente all'ESP cancella i campioni del grafico. Usa **Tutto** per caricare lo storico completo prima di esportare il CSV.

Nel grafico una **linea viola verticale** segna ogni nuovo ciclo; una linea tratteggiata segna **STOP, fine o allarme**. Il setpoint riparte con un tratto separato, mentre la temperatura reale resta continua. I marcatori sono salvati insieme ai campioni nella RAM dell'ESP, quindi restano dopo il refresh della pagina e compaiono anche nel CSV; spariscono solo quando lo storico viene sovrascritto o l'ESP si riavvia.

La dashboard mostra la **temperatura interna dell'ESP** ogni 10 s come diagnostica. Non misura il regolatore 3,3 V né l'aria attorno al modulo: per verificare il riscaldamento della scheda misura anche questi punti con uno strumento adatto. Il loop lascia più tempo alla CPU e aggiorna il LED verde solo quando cambia stato; campionamento PT100, PID e SSR mantengono gli stessi tempi.

Durante un ciclo con NTP sincronizzato, l'ESP salva l'ultima ora ogni **30 s**. Dopo un'interruzione lascia l'SSR spento e registra **ciclo interrotto**. Quando NTP ritorna, mostra il tempo tra l'ultimo salvataggio e il riavvio: è un **limite massimo approssimativo**, non la durata esatta del blackout, e anche un reset software può produrre l'avviso. Senza NTP o senza checkpoint la durata è sconosciuta. Dopo aver valutato il materiale, riconosci l'interruzione e avvia manualmente **un nuovo ciclo dall'inizio**; questo firmware non riprende uno step interrotto.
