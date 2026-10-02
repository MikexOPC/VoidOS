# Checkpoint: pool iniziale di oggetti

Revisione del 3 ottobre 2026, base Git af2a909. Task completata:
allocazione e rilascio di oggetti in un buffer kernel gia mappato,
con allineamento, handle verificabili, riuso azzerato e ID persistenti.

## Scelte e revisione

Il pool usa record ordinati fuori dal buffer, con limiti di 64 oggetti
e 64 KiB. La ricerca del primo gap non consuma padding di allineamento;
byte e record esauriti falliscono prima della pubblicazione. Rust
verifica richieste, estremi e offset allineati tramite soli parametri
interi. Non introduce puntatori unsafe, heap o dipendenze runtime.

Prima di modificare il pool vengono verificati ordine, sovrapposizioni,
ID unici, limiti e somma dei byte. Gli handle devono stare fuori dai
metadati e dall'arena per evitare che l'azzeramento li sovrascriva.
Il rilascio richiede identita, offset, dimensione e ID esatti, azzera
il dato e annulla l'handle. Un prestito ai dati va annullato dal
chiamante prima di continuare dopo release/detach.

Detach respinge oggetti vivi e annulla il puntatore al buffer. Self e
NextId restano nell'owner: riapertura e riuso non rendono validi vecchi
handle. Anche dopo detach l'esaurimento del contatore non viene aggirato.
Il protocollo richiede owner stabile e un prestito esclusivo del buffer;
non registra globalmente tutti i pool o protegge da codice ring 0 ostile.

Il kernel collega il pool alla finestra da 16 pagine solo mentre questa
e attiva. Il test sporca il primo oggetto, lo libera, riusa lo stesso
offset e controlla l'azzeramento; un vicino da 4097 byte resta integro.
Poi libera tutto, scollega, riapre, respinge un vecchio handle, scollega
di nuovo e solo allora rimuove il mapping e restituisce i frame.

## Evidenza sullo stato finale

- Test scritti prima delle funzioni del pool; compilazione/link senza CRT.
  Una copia ampia dei metadati nel test generava memcpy: e stata sostituita
  da una copia byte esplicita del solo mock, senza aggiungere runtime.
- Host C: allineamenti 1-4096, byte/record esauriti, frammentazione,
  ownership, copie, alias, input nulli, handle alterati, record corrotti,
  sovrapposizioni e contatore esaurito mantenuto dopo riapertura.
- 1024 operazioni confrontate con un modello bitmap indipendente:
  offset del primo gap, conteggi, contenuto degli altri oggetti vivi,
  azzeramento dei rilasci e guardie esterne.
- Tre test Rust nuovi, inclusi 8192 confronti con un modello u128.
  Passano tutti gli undici test Rust e le regressioni host del kernel.
- Build kernel completa C/Rust/assembly e conversione ELF valida;
  sette mutazioni invalide della conversione respinte.
- 7/7 scenari QEMU: 128/256 MiB, panic intenzionale, NX sull'ultima
  pagina, lettura dopo unmap e due guardie. Tutti richiedono KERNEL_OBJECTS_OK,
  stato ExitBootServices riuscito, CR3 e stato terminale CPU corretti.
- Trace `int,cpu_reset` presenti; nessun triple fault rilevato.
- Diff verificato e file della task in LF. Il modulo C ha 204 righe,
  i test C 216; header e validatori piccoli restano compatti.

Gli scenari finali sono in kernel/build/firmware-9027dccbf2c34291af6935b9a0180700.
I log e gli artefatti sono ignorati sotto build/. La ESP contiene il
kernel ordinario verificato; i kernel con fault restano in directory di test.

| Artefatto | SHA-256 |
| --- | --- |
| kernel.elf finale/installato | DEEFF7DFB1552A081ADCA65846ACBEB671822E2C0B3D72B4DCD66D0F7079BD92 |
| BOOTX64.EFI usato nei test | 07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3 |
| objects-host.log | 9424577128847A3D43093FF529B0A06B730F7BCE93DF765CB444A79B0A7B10C1 |
| objects-final-build.log | BB040F9876DA8065ECF424EB350413B1E8B442B996DB361CD2632AC8AF16188A |
| objects-final-firmware.log | D2A451D10609D26DF2F9B9A324B4D474979F5AF60ADA96593A900F32CCC8D3B4 |
| objects-image.log | EF14CA12161259FF99B0A632C721ABE715023BA5EEB10051ED339DFA467F5CBD |

Gli hash identificano queste esecuzioni, non garantiscono build riproducibili.
Il codice del bootloader non e stato modificato da questa task; le prove
QEMU usano la sua immagine del checkpoint precedente.

## Budget e limiti

Owner oggetti: 2096 byte statici, nessun metadato dentro i dati assegnati.
La finestra conserva il massimo di 64 KiB temporanei; i test restituiscono
interamente il budget fisico. Non vengono create pagine di tabelle.
Il kernel ELF e di 28672 byte su disco e 36 KiB residenti arrotondati per
segmento: rispetto al checkpoint precedente cresce di 4 KiB su disco e
8 KiB residenti, includendo codice, dati e arrotondamento delle sezioni.

Solo QEMU/EDK2 q35, CPU max TCG e un processore. Mancano SMP, isolamento
per oggetto, crescita automatica, distruttori C++, reclamazione firmware
e hardware reale. Il pool non impedisce l'uso di puntatori grezzi obsoleti
o unmap prematuri eseguiti dal chiamante. Non sono stati misurati benchmark.

Il kernel puo ora gestire piccoli oggetti durante il bootstrap. Prima
di usarli per servizi concorrenti serve definire ownership e sincronizzazione;
le eccezioni restano terminali e basate sulle strutture del bootstrap.

[API e comandi](../kernel/memory/object-pool.md),
[Rust u64](https://doc.rust-lang.org/stable/core/primitive.u64.html),
[pool generici Linux](https://www.kernel.org/doc/html/latest/core-api/genalloc.html).
