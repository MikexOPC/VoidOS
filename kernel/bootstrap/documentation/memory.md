# Bootstrap memoria: profili separati — 9 ottobre 2026

`kernel/main.c` passa da 400 a 193 righe. `kernel/bootstrap/memory.c`
inizializza le risorse; `kernel/test/bootstrap/probes.c` conserva le prove
originali. Un solo modulo funzionale, con aggiornamenti a build e verificatori.

## Comportamento

- Ordinario: allocatore validato, finestra inizializzata e inattiva, una regione
  persistente di 16 pagine, contesto/inventario copiati in due oggetti posseduti.
  Restano i controlli di API, ownership, mapping RW/NX, bounds e accounting.
- Diagnostico: `kernel/scripts/build.ps1 -TestBootstrapMemory` compila e collega
  i probe. Conserva zero/riuso, handle scaduti, allineamento, vicini intatti,
  detach rifiutato con oggetti vivi, limiti e budget. Le immagini window/region
  fault attivano automaticamente questa fixture; niente installazione implicita.
- Il profilo ordinario non alloca la regione diagnostica da 32 pagine: 128 KiB
  di memoria fisica persistente in meno rispetto al profilo diagnostico, oltre
  alle allocazioni temporanee e scansioni eliminate. Nessun benchmark di boot.
- Fallimenti terminali conservano gli owner necessari al fail-stop; questa task
  non aggiunge recovery o modifica driver/scheduler. Il runtime ordinario resta
  una sequenza finita di carichi fidati; non e ancora un sistema di servizi.

## Prove finali

| Prova | Esito / evidenza |
| --- | --- |
| Bootstrap host, ASan C | 330 controlli, 17 fallimenti API iniettati; allocator/mapping mock |
| Cppcheck | Quattro input C, zero error/warning/performance/portability; style/information nell'XML |
| Regressioni host | Suite C e Rust dei contratti riutilizzati; conteggi reali nei singoli risultati |
| Ordinario QEMU | 10/10 consecutivi, probe assenti dal binario e dal log, stato CPU/memoria verificato |
| Diagnostico QEMU | 10/10 consecutivi, marker dei probe richiesti, stato CPU/memoria verificato |
| Matrice diagnostica | 23/23 scenari, incluse RAM 128/256 MiB, quattro vCPU configurate, NX/guardie/NMI/double fault |
| Loop ordinario | 18 eventi, stop esterno e cleanup verificati; immagine separata |

[Host bootstrap finale](D:/OS/kernel/build/bootstrap-host-final.log).
[Regressioni host finali](D:/OS/kernel/build/bootstrap-memory-regressions-host-final.log).
[Boot finali](D:/OS/kernel/build/bootstrap-final-firmware.log) e
[risultati/hash](D:/OS/kernel/build/bootstrap-firmware-21cbb377507e4779acc632bd7d48ed44/results.json).

SHA256 ordinario: `2848DE29D7F26D7FD93DC412425F1E642AD2F23BF6AABC32A1013DF2C89740B2`.
SHA256 diagnostico: `29AE20575000BD60B0AE9E69BC3F7A329F7D5D213C26F76782D95DC7FEA7573E`.

[Matrice finale](D:/OS/kernel/build/bootstrap-diagnostic-regressions-final.log),
directory `kernel/build/firmware-5a6330a4ddfd465baa26359fb889ddb8/`.
Immagini compilate in `firmware-c05b600f0aca48cda30af9cf44ab571a/` e riusate
con `-ImageDirectory`: nuove VM/ESP/log, senza sovrascrivere le prime evidenze.
[Hash delle 23 ESP eseguite](D:/OS/kernel/build/bootstrap-review/matrix-images.json):
copie confrontate con ciascuna immagine originale. La ready della matrice ha
SHA256 `74A7BEE456CB1E77FEEB634CD97D4D87E8A9C06C47D8DF05F0D00EFB74853D03`;
rispetto alla diagnostica dei dieci boot differisce soltanto per due byte del
TimeDateStamp della directory debug PE nel segmento read-only. Segmenti
eseguibile, dati e unwind identici; gli hash distinti restano registrati.
[Loop](D:/OS/kernel/build/bootstrap-ordinary-loop.log), directory
`kernel/build/loop-firmware-cacdb9b119e0498eac4b71a65d89bba5/`.

## Debug e limiti

Le prime tre esecuzioni della matrice si sono fermate su verificatori storici:
Next del registro atteso 3 anziche 4 dopo il binding HPET; tutti gli IRQ LAPIC
attesi in HLT invece di worker+due HLT; 140 IRQ sul vettore 0x40 invece di
140 UART+tre HPET dopo il riuso del vettore. Verificatori corretti senza
allargare arbitrariamente i limiti, con nuove alterazioni worker RIP/IF
rifiutate. Queste esecuzioni non sono PASS. Nessun nuovo registro o Assembly;
la sequenza reale e in watchdog/core.c e nei report scheduler gia verificati.

ASan copre il nuovo C host e i mock, non la RAM fisica o il Rust freestanding.
Mapping/ownership reali sono esercitati in QEMU tramite le API esistenti;
risorse persistenti sono intenzionalmente vive. Valgrind non eseguito, coverage
linee/branch N/D, hardware fisico e SMP/AP non dimostrati.

[Manifest di 14 sorgenti/script](D:/OS/kernel/build/bootstrap-review/sources.json),
whitespace, limiti dimensionali e hash ricontrollati a fine task. Nessuna
modifica a `reuse/`: 460 file presenti coincidono con la vecchia fotografia;
`reuse/LunaroidOS-main/LICENSE` presente in quella fotografia ora e assente.
Non viene attribuita la causa o la data della rimozione, ne ripristinato il file.
[Confronto](D:/OS/kernel/build/bootstrap-review/legacy-comparison.json).
