# OvenControl

Controllo e collaudo di un forno con ESP32-S3, due sonde PT100 su MAX31865, dashboard web locale e comando di riscaldamento.

## Progetti

- [Firmware per il collaudo sul campo](firmware/oven-controller-field-test/README.md): prove delle PT100, dei LED e del comando relè, da usare prima di collegare le resistenze del forno.
- [Firmware completo](firmware/oven-controller-full/README.md): ricette, rampe, mantenimento, raffreddamento passivo, PID, allarmi, supervisione delle uscite e storico esportabile.
- [Firmware in sede](firmware/oven-controller-site/README.md): versione precedente con controllo e ricette.
- [Firmware di test iniziale](firmware/oven-controller-test/README.md).
- [Prototipo dell’interfaccia](interface/oven-controller/README.md), con immagine di riferimento in `interface/prototype.png`.
- [Contesto del progetto](PROJECT_CONTEXT.md), con note e cronologia.
- `media/`: foto dei componenti e dei collegamenti.

## Compilazione

I firmware sono progetti PlatformIO indipendenti. Dalla cartella `firmware`:

```sh
pio run -d oven-controller-field-test
pio run -d oven-controller-full
```

Per caricare sulla scheda, aggiungere `-t upload`. Seguire il README della versione scelta per pin, polarità, configurazione e procedura di collaudo.

Le credenziali della rete locale non sono incluse: copiare `include/wifi_config.example.h` in `include/wifi_config.h` nella cartella del firmware e compilare i valori sul proprio computer. Le cartelle di compilazione, le cache e i file locali di configurazione sono esclusi dal repository.
