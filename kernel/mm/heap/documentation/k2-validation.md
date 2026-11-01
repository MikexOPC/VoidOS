# K2-C: heap con prenotazione e commit

Snapshot del blocco heap verificato il 28 ottobre 2026. Il gate complessivo
K2 è stato poi chiuso il 29 ottobre: [campagna finale](../../../sync/documentation/k2-integration.md).
Le prove sotto restano quelle del blocco originale; questo rapporto non
attesta K3/SMP guest o il lifecycle concorrente dei mapper.

## Contratto implementato

Scelta A dell’utente in [k2-design.md](k2-design.md): un Gate per owner,
rango 200, contesto CPU tramite accessor già approvato. Il pool fisico ha
rango 300; nessuna chiamata a pool/mapping/device, logging o callback
all’interno della sezione critica heap. Le atomiche Rust di kernel/sync
sono riusate, senza duplicarne l’implementazione.

Facade in heap.c, contratto privato in internal.h, cinque sorgenti in core/:
guard, state, reserve, release e fill. Le API pubbliche non si richiamano
con un guard già acquisito. Stats e valid acquisiscono il lock; il validatore
completo attraversa i metadati senza leggere byte o canary in fase Busy.
Le mutazioni usano controlli strutturali e verifiche della risorsa interessata,
non una scansione completa di tutto l’heap per ogni oggetto.

Pagine: FREE -> INITIALIZING -> LIVE -> RETIRING -> FREE. Slab: Used conserva
la prenotazione e Busy distingue gli slot durante azzeramento o poison.
La memoria non è disponibile per altre allocazioni finché il fill termina
e il commit è verificato. Pending comprende le operazioni in volo; Objects
le comprende fino al commit. Vale FreePages+RunPages+SlabPages=Pages anche
durante una scrittura sospesa. Snapshot coerente non è una prenotazione.

Init su storage privato azzerato, con range distinto da owner e contesto;
reinit di owner pubblicato rifiutata. Vita e mapping del backing sono stabili
e garantiti dal chiamante. Nessun recupero dai page fault viene introdotto.
Commit impossibile nel percorso pubblico è terminale e trattiene la risorsa.

## Correzioni e verifiche aggiunte

- Overflow preesistente bytes+8 nella scelta classe: dimensioni estreme
  potevano apparire piccole. Ora il limite del range viene controllato prima
  delle somme; UINT64_MAX e UINT64_MAX-7 sono rifiutati nei test.
- RunLength, SlabOf e SlabClass verificati prima di limiti, indici e shift;
  test di corruzione con canary e range invariati sul rifiuto pertinente.
- Range sovrapposto ai metadati e reinizializzazione vietati.
- Commit con dimensione o flag incoerenti rifiutato mantenendo Pending e
  metadati validi; il successivo commit corretto conclude la prenotazione.
- Il primo harness esteso non creava la cartella del report cppcheck:
  corretto lo script, senza sopprimere diagnostiche del codice.
- Fixture legacy reinizializzate solo dopo invalidazione esplicita e privata
  del modello, senza presentare tale reset come lifecycle operativo lecito.

## Prove eseguite

| Prova | Risultato | Evidenza in kernel/build/ |
|---|---|---|
| Baseline del vecchio heap | PASS | test-run-7b479d96fd6e43c1a172613d40031292 |
| Suite heap finale: MSVC /W4 /WX, ASan, cppcheck | PASS | test-run-8d4e7706ee8345b8a5ceba3888e7c2fb; heap-sync-6674f44c4cf0440596f66f781732457b |
| 200.000 operazioni casuali legacy con oracolo, zero/poison/canary e rifiuti | PASS, 2.301.204.209 check del runner | stesso log finale |
| Contract: overflow, alias, reinit, contesto assente, IF0/IF1, corruzioni e commit rifiutati | PASS | contract.exe, stesso artefatto |
| Limite intero 16 MiB: zero all’allocazione e poison dopo free | PASS | contract.exe |
| 2/4/8 thread Windows, 2.000/4.000/8.000 cicli misti | PASS, 14.000 totali | threads.exe; payload e claim indipendenti |
| Trasferimento sospeso prima del fill, slab/run, alloc/free | PASS, 40 scenari | transfer.exe, altri oggetti progrediscono, Busy non riusato |
| Kernel con TestBootstrapMemory + TestKernelHeap | PASS, 10/10 boot consecutivi | kernel-heap-ab0b38789fa246b59e6d02d020c98eeb |
| Console ordinaria GOP: input, pixel, cleanup | PASS, 1/1 | qemu-run-33fbad0c95b445f18e27fad07551da80 |

Il kernel verifica KERNEL_HEAP_SYNC_OK prima di KERNEL_HEAP_READY; il runner
richiede il marker e KERNEL_READY, layout corretto e assenza di FAIL inattesi.
Hash ELF del diagnostico: 984B24D172C1C6BB3CF9D344316A77806830482572DACF5C3AF3B9FDF597D481.
Il self-test diagnostico mantiene intenzionalmente un oggetto con canary
corrotto: Objects=1, SlabPages=1, Pending=0, 65 rifiuti previsti. Non viene
presentato come heap completamente svuotato. Nelle prove concorrenti e di
trasferimento, invece, Objects=Pending=0 e tutto il budget ritorna libero.

## Limiti delle prove

ASan copre il C del nucleo e dei test; il supporto sync del helper è
compilato separatamente e ha le proprie prove precedenti. IRQ e contesto
CPU sono modellati sull’host; la contesa usa vere atomiche e thread distinti.
Il guest continua ad avviare solo il BSP; quattro vCPU configurate non sono
una prova di SMP. Il clock di contesa host è modellato; il bootstrap nativo
ammette ancora il percorso non conteso e gli AP non predisposti sono rifiutati.

Accounting e handle host posseduti chiusi sono verificati separatamente;
Valgrind non eseguito, coverage linee/branch N/D. ASan non dimostra assenza
di leak di RAM fisica. Nessuna prova su hardware reale o benchmark di latenza.

IF viene ripristinato al valore del chiamante: non viene forzato a 1 se era
0, e non si rilasciano eventuali lock esterni del chiamante. Su BSP con
scheduler cooperativo il fill non crea automaticamente punti di yield.
Raw pointer e canary non impediscono use-after-free o ABA dopo riuso; i
caller mantengono ownership esclusiva dell’oggetto durante uso e free.
Holdings e mapper non diventano concorrenti per questa modifica.

## Review finale

Limiti sorgenti, whitespace, conteggio diretto dei file e hash in
kernel/build/k2-heap-review.json. Cinque file in core/, sorgenti del modulo
e test fino a 144 righe; adattatore x86 sotto 400. Nessun sorgente di reuse/
modificato, nessun commit/push effettuato in questo blocco.
