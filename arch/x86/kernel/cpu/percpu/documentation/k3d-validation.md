# K3-D — risorse private AP predisposte, validazione

29 ottobre 2026. Scelta approvata: arena completa con rollback, prima di
avviare qualunque AP. [Contratto e fonti](k3d-design.md).
Stato: gate del blocco passati nel profilo BSP/QEMU, AP ancora fermi.

## Cosa è implementato

Arena supervisor nello slot 487, con 64 unità virtuali da 256 KiB dopo
2 MiB di guardia. Unità zero vuota, BSP sulle proprie risorse precedenti.
Ogni AP abilitato ha stack principale 64 KiB, tre IST da 16 KiB, guardie
assenti prima/dopo ogni stack e una pagina separata per GDT/TSS. Pagine
WB, RW/NX, U/S=0; nessuna foglia grande o globale. La TSS di 104 byte è
interamente nella pagina dei descrittori; GDT e descrittore TSS sono
preparati ma non caricati, tipo available, nessun LTR/INIT/SIPI nuovo.

Due lease complessive pinned: dati contigui e tabelle. Con quattro CPU
configurate, tre AP predisposti: 87 pagine dati (348 KiB) e tre tabelle
(12 KiB). Con 64 CPU il modello richiede 1.827 pagine dati e dieci tabelle.
Con solo BSP non viene allocata alcuna risorsa AP né installato il ramo.

Rust valida la geometria scalare senza puntatori o blocchi unsafe; C la
confronta durante la costruzione e possiede lease, mapping e descrittori.
Snapshot interno paging da 3.944 a 4.000 byte, record AP 56 byte, tag
60–69 e kind RAM 10. Aggiornati i controlli espliciti di inventario,
video e GPU; nessun allentamento dei permessi o delle verifiche di alias.

Il mapper prepara e verifica le tabelle prima di pubblicare il ramo,
inizializza tutta la RAM, verifica i descrittori inattivi e ricattura
l’inventario. Il wrapper nativo preserva IF, bind/unbind richiedono BSP
con IF=0. Oggetti C validi e vivi del kernel restano un prerequisito:
i controlli numerici di range/alias non costituiscono user-copy sicura.

## Ownership e rollback

Prima dell’avvio AP, un errore ritira il link PML4, ricarica CR3 sul BSP,
ritira l’autorità dall’inventario e rilascia i frame solo dopo unmapping.
Le foglie non globali e l’assenza di AP attivi sono prerequisiti del flush
locale: non viene presentato come TLB shootdown SMP.

Le lease pinned usano il rilascio transazionale K2: se una restituzione
è rifiutata, i pin e l’ownership delle risorse rimaste vengono conservati.
Dopo un cleanup parziale non viene dichiarato successo; il chiamante deve
fermarsi. Il percorso di rollback del bind arresta il kernel se il cleanup
non è completato. Nessun free di frame ancora raggiungibili.

Il diagnostico prepara/verifica/rilascia con budget originario ripristinato,
poi prepara di nuovo per le prove. Il normale bootstrap conserva le
risorse PREPARED. Ripristinare il budget in pagine non significa riportare
indietro contatori monotoni di ID o generazione.

## Test host finali

Nuova suite ap-resources:
`kernel/build/test-run-55fcfa069760496788f3c5d04e66dac8/ap-resources.log`.

- Due test Rust in debug e release (quattro esecuzioni): capacità, guardie,
  limiti ed estremi. Il C confronta tutte le 4.096 posizioni per ciascuno
  dei 64 conteggi CPU con la geometria Rust (262.144 confronti).
- C ASan con ciclo di vita e costruzione reali; hardware, allocatore e
  cattura inventario modellati. Tutti i 63 record descrittori AP sottoposti
  a sweep di ogni bit dei 176 byte (88.704 mutazioni rifiutate).
- 101 failure point nel percorso di preparazione: allocazione, pin,
  prestiti di pagine, CPU/flush e cattura. Ogni rifiuto ripristina il budget
  e ritira il mapping senza lasciare lease/pin nel modello.
- Rifiuto di alias dell’owner con contesto, finestra, allocatore e inventario,
  overflow del puntatore owner e alias fisico di un frame già mappato.
- Doppio bind rifiutato. Flush fallito conserva le due lease pinned;
  rifiuto del primo rilascio conserva entrambe; rifiuto del secondo, dopo
  avere restituito le tabelle, conserva la lease dati pinned. Retry di
  teardown riuscito con accounting ripristinato.
- Modello completo da 64 CPU: 1.827 pagine dati, dieci tabelle, cleanup
  completo. Non equivale a un boot hardware con 64 CPU.

MSVC /W4 /WX e ASan; Rust con warning come errori. La suite host VirtIO
usa ora nomi oggetto derivati dal percorso completo e warning di link
come errori, per evitare collisioni fra file registry.c di moduli diversi.

| Suite di regressione | Evidenza in kernel/build/ |
|---|---|
| kernel | test-run-7395cb040fb044ff8a4107745ee45df5 |
| paging | test-run-a9ef4c7e9fd248d7a7cef2cb64a18455 |
| x86-framebuffer | test-run-8ff187fff96a4ebeb7def76a0f9b9edd |
| x86-virtio | test-run-794bafd50d99457e9da4b03998776848 |
| virtio-resources | test-run-1098e164755c449a893ee4a347e98463 |
| dma-arena | test-run-c0d751e9cdba44ddbaa2e8fbb6aa3573 |
| percpu-identity | test-run-27694c11fe0544f9a1ff078c44051f42 |
| percpu-clock | test-run-a987f9804f8144bea5e50675914cf9e8 |
| bootstrap | test-run-e8d0505a17084788ad8391e8416a4755 |
| layout | test-run-71b4e32a8c80444c8b2707be652ca444 |

Sono undici suite passate includendo quella nuova, non una dichiarazione
che ogni suite del repository sia stata rieseguita. ASan riguarda il C
host strumentato; non misura leak di RAM fisica o il codice privilegiato.
Valgrind non eseguito su Windows. Coverage linee/branch N/D.

Analisi statica nuova produzione: `kernel/build/k3d-static/cppcheck.xml`,
zero error/warning/performance/portability. Tre suggerimenti style:
azzeramento con ciclo freestanding, puntatore root const nel reader e
readback volatile dopo la scrittura. Mantenuti senza soppressioni globali.
Gli header di sistema non risolti dal checker sono informazioni; MSVC
compila con gli header effettivi. Gli adattatori paging/video/GPU sono
anche coperti dalle rispettive suite di analisi e compilazione.

## QEMU: immagini finali dopo le correzioni

QEMU 11.1.0, q35/TCG/OVMF, 128/256 MiB. Boot positivi alternano una e
quattro vCPU configurate; **un solo core OS attivo**, BSP.

| Profilo | Risultato | Evidenza in kernel/build/ |
|---|---|---|
| Diagnostico, incluso teardown e ripristino budget con AP presenti | 10/10 consecutivi | percpu-firmware-4fa7a6fc7223469297cacd46e071486d/results.json |
| Ordinario, risorse AP conservate | 10/10 consecutivi | percpu-firmware-0bbb26b5efa94aee9a086cc7cbdb582e/results.json |
| Guardia e NX, fault intenzionali sul BSP con quattro vCPU configurate | 2/2 attesi | qemu-run-83657883265941d0aff729d02efd7a9b/results.json |
| Console VirtIO/VT-d con tre AP predisposti | 1/1 PASS | qemu-run-7e40b16f1ce640d89d284d4a09915a53/results.json |

ELF diagnostico: `44E89DEF67EFE8DB70798514DAA808B97B4E31CC1A624EEFFC4F5E93564B7432`.
ELF ordinario: `B0309B199389C1F3F1CCF0D065B1E142F60447CF1DBCA4944EF4598198FFD9AD`.
Loader: `B6C055AB17CC5CA1E0F017A35E4E9722273EB7B49CB0191F5E759CA84CC23AA1`.

Il monitor verifica indipendentemente PML4/PDPT/PD/PT, tutte le foglie e
guardie, RW/NX/U/S, lease esatte nel Live[] dell’allocatore e pin, indirizzi
GDTR/TSS/RSP0/IST, tipo available, I/O map e campioni di stack zero. Il
kernel controlla inoltre tutti i byte iniziali non descrittori. Continuano
le verifiche GS, clock e matching dello snapshot CPU con la MADT reale.

Il negativo guard deve produrre #PF, errore 0 e CR2 sulla guardia del primo
AP. Il negativo NX deve produrre #PF, errore 0x11 e CR2 sul suo stack dati.
Richiesti marker di preparazione/iniezione, frame valido e assenza di
KERNEL_READY dopo il fault. Fault intenzionali distinti dai crash inattesi.

La sessione video finale registra ConfiguredVcpus=4 e OsCpusReady=1:
`kernel/build/pointer-interactive-check-b22bd13be6244e9bb679bd3c895e3923/results.json`.
Sono verificati 600 comandi mouse, scene/pixel, input e cleanup. Una sessione
completa, non dieci sessioni video e non una misura di prestazioni.

## Errori trovati e correzioni, senza riscrivere le prove

1. Primo boot con tre AP predisposti: FRAMEBUFFER_MAPPING_FAILED. Il
   validatore video non riconosceva la nuova autorità AP. Corretto il
   riconoscimento esatto, mantenendo tutti i controlli; rieseguiti i gate.
2. Video con quattro vCPU: VGPU_MAPPING_FAILED. Il validatore GPU trattava
   i tag >=48 come heap, includendo quelli AP. Corretto il dispatch prima
   del ramo heap e ricompilate tutte le immagini della campagna finale.
   La VM di test già ferma nel panic è stata identificata dal percorso
   univoco e terminata; non è contata come PASS.
3. Script ASan iniziale: separatore di percorso Windows non sostituito.
   Corretto il nome dei file oggetto. Poi cppcheck ha rilevato aritmetica
   intermedia fuori array nel modello, corretta calcolando prima l’offset.
4. Collisione di oggetti registry.c nel test host VirtIO: compilazione
   separata con percorsi univoci; link finale verificato con /WX.
5. Hook fault: codice irraggiungibile dopo fail-stop, rifiutato da /WX.
   Separati i rami condizionali. Runner negativo iniziale: l’attesa
   generica trattava l’eccezione prevista come guasto; sostituita solo
   nel runner dedicato con attesa delimitata e verifica completa del fault.
6. Review: rinforzati alias/range del nuovo owner e rilascio transazionale.
   I boot finali sono quelli ricompilati dopo tali modifiche, non quelli
   delle prime immagini sperimentali.

## Limiti e seguito

Sorgenti del modulo e adattatori entro 400 righe, massimo 391; nuove
cartelle funzionali entro cinque file diretti. Manifest con hash finali:
`kernel/build/k3d-source-review.json`. Nessun codice esterno copiato;
riutilizzato e adattato il protocollo di mapping posseduto già nel progetto.
Nessuna modifica a scheduler, politica watchdog o vecchio OS.

L’arena richiede memoria fisica contigua: un rifiuto precede qualsiasi
avvio AP. Risorse persistenti PREPARED sono memoria posseduta, non leak.
Non esistono ancora ingresso AP, caricamento dei descrittori su AP, clock/
GS AP né commit operativo che impedisca il teardown dopo il primo avvio.
Queste garanzie vanno introdotte prima di INIT/SIPI; serve anche riservare
prima delle allocazioni ordinarie la memoria bassa del futuro trampolino.
Nessuna prova SMP o su hardware fisico, nessuna misura di prestazioni GUI.
