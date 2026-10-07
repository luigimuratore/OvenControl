# Carlo Gavazzi trifase e comando del forno

Nelle note del progetto il componente è indicato come **RGC3A60D30KGE**. La sigla comunicata `RQC3A60D30KQE` non è stata trovata nel catalogo consultato: la corrispondenza è probabile, ma va confermata leggendo la targhetta. Le informazioni seguenti si riferiscono al **RGC3A60D30KGE**.

La [scheda ufficiale del prodotto](https://www.gavazziautomation.com/en-gb/product/RGC3A60D30KGE) identifica un SSR a tre poli, zero-cross, con ingresso **5–32 V DC** e uscita **42–660 V AC**; la corrente nominale AC-51 è **30 A**. Quindi il modello è statico: non ha una bobina meccanica e il comando della dashboard deve essere configurato come **SSR**.

## Implicazioni per il progetto

- L'ingresso A1+/A2− richiede una tensione nel proprio intervallo: il GPIO a 3,3 V non lo pilota direttamente. Il driver a transistor e alimentazione 12 V già descritto nelle note del progetto serve a comandare questo ingresso. Verificare polarità, massa del circuito di comando e tensione effettiva sull'ingresso durante il collaudo.
- La commutazione zero-cross si presta al comando ON/OFF con finestre temporali del riscaldamento. Per il firmware completo selezionare **Ricette → Attuatore e limite potenza → SSR**, impostare ad esempio **10 s** come riferimento iniziale e conservare il limite iniziale **30%** per la prima prova. Sono riferimenti da tarare, non parametri prescritti dal produttore. Le impostazioni già salvate sull'ESP non cambiano leggendo questa nota.
- Un solo ingresso comanda i tre poli; i GPIO esistenti non consentono la regolazione separata delle tre fasi.
- Il modello base KGE non offre una telemetria della corrente o un'uscita di allarme per il firmware. Le varianti **RGC..M** aggiungono monitoraggio e uscite dedicate: non attribuire queste funzioni al modello base. Il LED di comando e lo stato GPIO non dimostrano che la potenza sia passata al carico.
- La corrente ammissibile dipende dalle condizioni termiche e dal derating. Un SSR spento non costituisce sezionamento della rete e può guastarsi in conduzione; restano necessarie le protezioni indipendenti del forno già previste nel progetto.

Riferimento per ingresso, morsetti, varianti con monitoraggio e derating: [datasheet ufficiale RGC2/RGC3](https://www.gavazziautomation.com/fileadmin/images/PIM/DATASHEET/ENG/SSR_RGC_2_3A.pdf).

Per il test senza resistenze usare `oven-controller-field-test`. Il collaudo del comando a bassa tensione non certifica il comportamento del circuito trifase sotto carico.
