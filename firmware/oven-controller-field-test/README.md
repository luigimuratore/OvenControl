# Oven Controller · collaudo sul campo

Progetto **separato** dagli altri firmware, per ESP32-S3 N16R8: test di **due PT100 reali**, **LED** e **comando relè / driver SSR**, con le resistenze reali del forno scollegate. Dashboard inclusa nel firmware e utilizzabile da telefono, anche senza Internet. Nessun PID, ricetta o avvio automatico del relè.

## Caricare e accedere

Apri **questa cartella** in PlatformIO, oppure dal terminale:

```sh
cd oven-controller-field-test
pio run
pio run -t upload
pio device monitor -b 115200
```

Collegati al Wi-Fi **`OvenController-FieldTest`**, password **`oven12345`**, e apri **http://192.168.4.1/**. Su telefono, mantieni la connessione a questa rete anche se segnala che non ha Internet. La pagina funziona senza librerie o font esterni; non serve caricare un filesystem separato.

Il file locale `include/wifi_config.h` è stato ripreso dal progetto `oven-controller-test`. Per cambiare rete del capannone modifica SSID/password lì; se manca, copialo da `include/wifi_config.example.h`. La rete diretta dell'ESP resta attiva. Sulla rete del capannone usa l'IP mostrato nella dashboard o, se mDNS è disponibile, **http://oven-field-test.local/**. Non serve NTP: log e campioni riportano il tempo dall'accensione. Chi raggiunge la dashboard può inviare comandi e STOP; non c'è login.

## Collegamenti mantenuti

| Funzione | GPIO |
|---|---:|
| MAX31865 #1 · CS | 14 |
| MAX31865 #2 · CS | 10 |
| SPI · SCK | 13 |
| SPI · SDO delle Click → MISO ESP | 12 |
| SPI · SDI delle Click ← MOSI ESP | 11 |
| Comando transistor/relè, tramite resistenza da 5 kΩ | 4 |
| LED rosso | 6 |
| LED verde | 7 |

Le Click usano **3,3 V e GND comune**. Mantieni la resistenza tra base del transistor e GND prevista dallo schema: lo spegnimento durante reset e prima dell'avvio del firmware dipende anche dal circuito. GPIO35/36 non sono usati, come negli altri progetti N16R8.

In `include/hardware_config.h` trovi tutti i pin, la polarità delle uscite e **RREF distinta per ogni sonda**. I valori iniziali sono **470 Ω**, PT100 nominale **100 Ω**, collegamento **3 fili**, come nei firmware esistenti. Verifica che resistenza di riferimento e ponticelli delle tue Click corrispondano: una RREF diversa può produrre una temperatura plausibile ma errata. Le uscite sono inizialmente attive alte. Il filtro MAX31865 è impostato a **50 Hz**.

## Procedura per il collaudo

1. **A circuito disalimentato**, verifica il cablaggio e lascia scollegate le resistenze del forno. Alimenta ESP e circuito di comando. All'avvio GPIO4 è spento. A dashboard avviata il verde è acceso e il rosso spento.
2. **Sonde:** osserva ciascuna PT100 senza avviare alcun test. La pagina mostra temperatura, resistenza, RAW e codice fault con descrizione. Scalda una sonda con la mano e verifica che reagisca il canale corretto, poi prova l'altra. Il confronto ΔT è diagnostico: non blocca i test delle uscite. Per verificare la segnalazione di sonda assente, scollegala e ricollegala a circuito disalimentato.
3. **Abilitazione:** seleziona “Le resistenze reali del forno sono fisicamente scollegate” e premi **ABILITA TEST PER 60 s**. I comandi appartengono alla pagina che li ha abilitati; un'altra pagina può osservare e premere STOP.
4. **LED:** prova verde, rosso, entrambi o spenti per **3 s**. La sequenza dura **8 s**: spenti → verde → rosso → entrambi, **2 s per stato**. Durante questi test **GPIO4 resta spento**, anche quando si accende il rosso.
5. **Relè/driver:** invia un impulso da **0,1 / 0,5 / 1 / 2 / 5 s**, oppure **3 impulsi da 1 s**, separati da **2 s OFF**. In queste prove il rosso segue GPIO4. Controlla il comando, il transistor e l'attuatore con gli strumenti adatti al tuo circuito, usando solo un eventuale carico di prova a bassa tensione. Le sonde possono essere assenti: il test relè è indipendente dalla temperatura.
6. **Arresto:** premi **STOP** durante un impulso e verifica fisicamente l'uscita spenta. Ripeti lasciando scadere il test, interrompendo il collegamento della dashboard e riavviando la scheda. Al riavvio non riparte alcun test.
7. **Esportazione:** scarica il CSV completo e il log JSON prima di disalimentare. La checklist nella pagina raccoglie le tue verifiche manuali e si azzera ricaricandola.

Il verde a riposo indica server/rete avviati, anche con sonde in fault. Durante il test LED il rosso è indipendente dal relè: guarda anche il riquadro **Comando relè**. Gli indicatori della dashboard sono gli stati richiesti ai GPIO: **non provano che il transistor, il relè o l'SSR abbiano commutato**. Il firmware non dispone di letture di corrente, tensione o feedback dei contatti; queste verifiche richiedono uno strumento esterno.

## Spegnimenti automatici

- Ogni prova ha una durata fissa: un impulso singolo non supera **5 s**.
- L'abilitazione scade dopo **60 s**, anche se la pagina continua a rispondere. Per altre prove devi riabilitare.
- La pagina invia un heartbeat ogni circa **0,7 s**. Se manca per **2,5 s**, tutte le prove si fermano, il relè si spegne e l'abilitazione viene annullata. I semplici aggiornamenti dello stato da altre pagine non rinnovano l'heartbeat.
- Nascondere/chiudere la scheda del browser o bloccare il telefono interrompe l'heartbeat; la pagina tenta anche un STOP immediato. Mantieni la pagina visibile durante le prove.
- **STOP** spegne subito le uscite di test e annulla l'abilitazione. Il verde torna poi al normale indicatore di server avviato.

Il supervisore dei GPIO è un task indipendente, con verifica ogni circa **10 ms**: i limiti temporali non dipendono dalla conclusione delle richieste HTTP o delle letture SPI. Queste sono protezioni software per il collaudo; non rendono questo firmware adatto ad alimentare le resistenze reali.

## Diagnostica e dati

Le sonde vengono interrogate circa ogni **500 ms**. Una lettura è valida solo senza fault, con RAW diverso dagli estremi e temperatura tra **−50 e 220 °C**. La dicitura “lettura valida” non certifica l'accuratezza della sonda o del cablaggio. Senza lettura valida, la temperatura è visualizzata come `—`; RAW, resistenza calcolata e fault restano disponibili per la diagnosi.

Lo storico contiene fino a **3600 campioni**, circa **1 ora** con un campione ogni circa **1 s**; il grafico mostra gli ultimi **5 minuti**. **Esporta CSV completo** recupera tutto lo storico ancora presente sull'ESP, incluse letture non valide come celle temperatura vuote, RAW/fault e comandi GPIO. Lo storico è sovrascritto quando pieno. Impulsi più brevi del campionamento possono non comparire nel CSV: usa il log per i cambi delle uscite.

Il log conserva gli ultimi **128 eventi in RAM**, visibili anche via seriale. Un riavvio cancella log e storico; restano in memoria il contatore degli avvii e un indicatore di abilitazione interrotta. Non viene misurata la durata di un blackout. Temperatura interna ESP, heap libero/minimo e RSSI Wi-Fi aiutano a osservare la scheda; non misurano il regolatore né l'alimentazione delle Click.

```sh
curl -s http://192.168.4.1/api/status
curl -s http://192.168.4.1/api/logs
```

## Verifica del codice

Compilazione ESP32 con `pio run`. Il test della logica temporale, eseguibile anche senza ESP, copre avvio/STOP, impulsi, sequenze LED/relè, heartbeat, scadenza abilitazione e overflow di `millis()`:

```sh
c++ -std=c++11 -Wall -Wextra -Werror tests/output_test.cpp -o /tmp/oven-field-output-test
/tmp/oven-field-output-test
```

La verifica fisica di sonde, polarità, transistor, LED e relè va eseguita sul circuito durante il collaudo.
