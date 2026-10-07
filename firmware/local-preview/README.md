# Dashboard in localhost, senza ESP

Le dashboard dei firmware **completo** e **di collaudo** sono disponibili insieme, usando gli stessi HTML, CSS e JavaScript dei rispettivi firmware. Non serve compilare, caricare una scheda o accendere l'ESP. Richiede soltanto Python 3, senza pacchetti da installare.

Dalla cartella `firmware`:

```sh
python3 local-preview/server.py --open
```

Su macOS puoi anche fare doppio clic su `Avvia dashboard.command` in questa cartella. Per interrompere il server premi **Ctrl+C** nel terminale.

- Versione completa: <http://localhost:8080/full/>
- Collaudo: <http://localhost:8080/field-test/>

Il banner in alto permette di passare da una dashboard all'altra e di simulare/ripristinare un fault della PT100 #2. Puoi aprirle anche in due schede. Il server accetta connessioni solo dal computer locale.

## Cosa puoi provare

- Letture PT100 simulate con temperatura, Ω, RAW e fault.
- Ricette, editor, import/export JSON, parametri PID e tipo attuatore/finestra/limite comando.
- Avvio, pausa, ripresa, STOP, step e allarme per sonda scollegata.
- Grafici, paginazione dello storico, CSV e log JSON.
- Abilitazione del collaudo, impulsi del relè, sequenze e singoli LED, scadenza dell'abilitazione e heartbeat.

Le conferme di collaudo/resistenze scollegate appartengono alla dashboard originale; qui abilitano soltanto i comandi simulati. Nessuna API contatta l'ESP o comanda un'uscita fisica. Ricette, impostazioni, temperature, stato e log restano **in RAM**: sopravvivono al refresh della pagina, ma ripartono dai valori iniziali riavviando il server. Le schede dello stesso firmware condividono lo stesso dispositivo simulato; i due firmware hanno stati separati. Il firmware reale e le credenziali Wi-Fi non vengono modificati.

La simulazione termica della versione completa va a **10x** per vedere avanzare le rampe; i test dei LED/relè e le relative scadenze vanno sempre a tempo reale. La risposta termica è un modello illustrativo: non riproduce il PID dell'ESP, il forno, i consumi o i guasti del circuito. La simulazione non sostituisce il collaudo fisico.

Porta diversa o velocità termica diversa (1–60x):

```sh
python3 local-preview/server.py --port 8081 --speed 1 --open
```

I file sono riletti dal disco: dopo una modifica a `data/` basta aggiornare il browser. Il server espone solo gli asset della dashboard e le API simulate; non serve le cartelle del progetto o `wifi_config.h`.

Verifica del server e dei flussi:

```sh
python3 -m unittest discover -s local-preview/tests -v
```
