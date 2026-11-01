# K2: campagna finale di integrazione

29 ottobre 2026. **K2 verificato e chiuso nel profilo BSP concordato:**
smoke set 17/17 e dieci boot consecutivi aggiuntivi dell’immagine heap corrente.
Perimetro: verifica con gli harness esistenti, review di hash/evidenze e
aggiornamento dei documenti di stato. Nessuna nuova architettura K3.

## Immagini e sorgenti

Comando: powershell.exe -NoProfile -File scripts/test/qemu.ps1 -Smoke -Parallel 1.
Una campagna/VM alla volta per evitare competizione fra VM emulata e
soglie dei timer. Ogni campagna ricompila il profilo previsto dal registro.
Directory: kernel/build/qemu-run-ca089f62314e48a999c39e68d38a9800.
Log principale: kernel/build/k2-final-smoke.log.

HEAD di riferimento: b3eb7e2412173cd9c00ca0fb475580d6cbd0eb42, con working
tree non committata già presente. La campagna verifica questa working tree,
non il solo commit. Nessun reset, commit, push o modifica a reuse/.

Confrontati 35 file di sorgente/harness dei precedenti manifest heap,
pool e sync: nessuna differenza. Dettaglio in
kernel/build/k2-smoke-source-check.json. Questo controllo non afferma
che tutti i file del repository siano identici a HEAD o coperti da test.

## Profili richiesti

Il registro scripts/test/campaigns.json comprende 17 campagne smoke:

- kernel-boot: 23 scenari di RAM, mapping, stack, guardie, NX ed eccezioni;
  include vCPU configurate a quattro senza attestare AP avviati;
- kernel-heap: self-test slab/run, canary, rifiuti e marker sync;
- paging-tamper: modifica PTE intenzionale rilevata dall’audit;
- v2c: console VirtIO con traduzione DMA;
- fallback: errore present intenzionale e fallback GOP;
- stall: present rallentato, timeout e recupero;
- plain: console GOP interattiva;
- absent: GPU priva di DMAR e percorso previsto dal profilo;
- framebuffer-draw, console-active, shell-text e shell-novideo;
- paging-retirement e paging-retirement-fault;
- sync, sync-wait e sync-irq.

Un esito positivo richiede l’uscita zero del runner e i riscontri specifici:
marker, stato CPU/memoria o screenshot, secondo il profilo. I fault e i
panic intenzionali dei negativi non sono conteggiati come crash inattesi;
non è sufficiente l’assenza di output o un avvio senza verifica.

## Risultati finali

La campagna completa termina con RESULT PASS qemu 17/17, senza correzioni
al codice eseguibile o ripetizioni di campagne fallite. I guasti intenzionali
sono riconosciuti dai rispettivi runner; nessun crash inatteso rilevato.
Il riepilogo automatico è in results.json nella directory della campagna.

| Campagna | Esito smoke |
|---|---|
| kernel-boot | PASS |
| kernel-heap | PASS |
| paging-tamper | PASS |
| v2c | PASS |
| fallback | PASS |
| stall | PASS |
| plain | PASS |
| absent | PASS |
| framebuffer-draw | PASS |
| console-active | PASS |
| shell-text | PASS |
| shell-novideo | PASS |
| paging-retirement | PASS |
| paging-retirement-fault | PASS |
| sync | PASS |
| sync-wait | PASS |
| sync-irq | PASS |

La riga kernel-boot comprende i 23/23 scenari verificati dal proprio
harness, non un solo scenario. Il conteggio 17/17 indica campagne e non
viene presentato come numero totale di boot positivi.

Per le altre 16 campagne, l’hash di ogni ELF ricompilato corrisponde al
report del runner associato: kernel/build/k2-smoke-image-checks.json.
Input, screenshot, budget e failure/recovery sono verificati dai runner
specifici, senza attribuire a tutti i profili le stesse misure.

### Dieci boot dell’ELF corrente

La ricompilazione heap produce hash
`392B208FD5C8AAF614B39E9D1A4284C8B32576991CCD833BF88031BEB30B511C`,
diverso dall’ELF della campagna precedente. Non si attribuiscono a questo
binario i vecchi dieci PASS: sono stati eseguiti dieci nuovi boot consecutivi,
dopo il smoke e senza altre VM in parallelo. Esito 10/10, con marker sync,
self-test, layout, 65 rifiuti intenzionali e KERNEL_READY verificati.
Evidenze: kernel/build/kernel-heap-20d7e7d570dd467c90c3bba3938a3c0a e
kernel/build/k2-final-heap-ten-boots.log. Hash confrontato con l’ELF di
campagna conservato; la differenza binaria non viene attribuita a una
causa non indagata. Continuità registrata in k2-heap-image-continuity.json.

## Verifica delle copie ESP del primo gruppo

Completati 23/23 scenari kernel-boot, sono confrontati offline gli hash
SHA-256 degli ELF nelle ESP con le rispettive immagini diagnostiche e gli
hash del loader con il file usato dal runner. Confermate anche le variabili
OVMF isolate. Tutte le 23 copie corrispondono; dettaglio in
kernel/build/k2-kernel-boot-hashes.json. È una verifica delle copie conservate
oltre alle verifiche runtime del runner, non una misura di memoria letta
dal processore. Gli altri runner registrano/verificano gli hash nei propri
results.json: alcuni usano scripts/lib/qemu.py, altri il provider framebuffer.

## Prove precedenti conservate

- [Primitive sync](validation.md): contesa host, ordine dei guard, IF e
  fail-stop intenzionali; BSP nativo, nessun SMP guest.
- [Pool fisico](../../mm/physical/documentation/k2-pool-validation.md):
  query coerenti e rilascio con ownership trattenuta, 14.000 cicli concorrenti
  host, ASan/accounting e boot indicati nel rapporto.
- [Heap](../../mm/heap/documentation/k2-validation.md): 200.000 operazioni
  casuali, 14.000 cicli concorrenti, 40 trasferimenti sospesi, limite di
  16 MiB e dieci boot diagnostici consecutivi. Il self-test kernel mantiene
  intenzionalmente un oggetto con canary corrotto, non un heap svuotato.

Queste prove non sono state rieseguite tutte in questa campagna smoke:
restano evidenza dei blocchi indicati, con sorgenti pertinenti verificati.
La copertura linee/branch è N/D; Valgrind non eseguito. ASan host non
certifica leak di RAM fisica; accounting e lifetime sono prove distinte.

## Limiti che restano dopo il gate

K2 riguarda lock/atomiche e sincronizzazione dei nuclei pool/heap. Non
rende concorrenti holdings, mapper, console, lifecycle device o tutte le
strutture kernel. Il backend nativo accetta ancora il BSP verificato;
GS/per-CPU, AP e il clock di contesa per CPU sono lavoro K3. Prima del
clock il percorso nativo ammette solo lock non contesi; una contesa dopo
prenotazione senza completamento resta terminale, secondo il contratto.

Nessuna prova su hardware fisico. Le scelte di user mode, syscall, TLB
shootdown e preemption non sono implementate da questa verifica.
Il difetto della misura LAPIC corretto e la causa non catturata del vecchio
panic rimangono distinti nel
[report timer](../../../arch/x86/kernel/timer/lapic/documentation/wait/k2-failure.md).
Un smoke verde non è una prova retroattiva della causa di quel singolo log.


## Passaggio successivo

È chiuso il gate K2 della roadmap: primitive, pool e heap hanno le prove
richieste per il profilo attuale. K3 deve ora avere un contratto concreto
per dati per CPU, clock di attesa per CPU e avvio controllato degli AP,
con fonti hardware e discussione prima del codice. Nessun AP viene avviato
e nessuna implementazione K3 viene dichiarata da questa task.
