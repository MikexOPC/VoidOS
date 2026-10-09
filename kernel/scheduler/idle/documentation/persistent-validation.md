# Arresto cooperativo e attese ripetute: validazione v0

Snapshot storico. Separazione del dispatcher e riuso idle sono verificati nel
[report successivo](dispatcher-validation.md).

8 ottobre 2026. [Contratto scelto](persistent-design.md). Perimetro: scheduler,
core Rust/FFI e integrazione nativa. Profilo cooperativo BSP, arresto al
checkpoint dopo lavoro corrente e cleanup; nessuna distruzione forzata.

## Implementazione

Owner conserva otto flag stop legati ai propri handle/generazioni. Lo storage
passa da 464 a 472 byte, senza heap. request_stop richiede assenza di RUNNING:
READY resta in coda una sola volta; WAITING perde il token e diventa READY.
Un duplicato valido e un no-op senza incremento del contatore delle mutazioni.
Gli errori preservano lo stato; overflow del contatore rifiuta la prima richiesta.
Flag incoerenti con FREE/RESERVED/WAITING/RETIRED invalidano l'Owner.

QUERY non modifica stato. WAIT valida evento/token prima del flag e, quando
stop e richiesto, restituisce STOP_REQUESTED e READY senza parcheggiare la task.
EXIT rimane esplicito; FINISHED conserva il flag fino a COLLECT. Il rilascio
azzera il flag e la generazione impedisce che un handle precedente arresti la
task che riutilizza lo slot. Stop non modifica l'epoch dell'evento.

La nuova API kernel_scheduler_stop_apply restituisce l'EventReply esistente
(48 byte, layout invariato). REQUEST=0, QUERY=1; outcome indica il flag 0/1,
woken il numero di waiter resi READY (0/1), epoch=0. Errori/BUSY/DENIED non
pubblicano un flag valido. Guard esclusivo e verifica del contesto precedono
qualsiasi riferimento all'Owner; ISR post rimane indipendente dal core.

La mailbox C cede prima il controllo al dispatcher. Questo applica YIELD
prima di REQUEST/QUERY, senza un RUNNING nell'Owner per REQUEST. Il risultato
e controllato rispetto all'handle target, distinto dall'handle del chiamante.
Nessun borrow Rust attraversa switch o attese hardware.

Il nuovo workload nativo esegue 32 attese su eventi 4/5 alternati con una
condizione persistente aggiornata prima del segnale. Il consumer poi attende
l'evento 6: il produttore ne chiede l'arresto, lo risveglia e termina. Il consumer
verifica il flag, prova che una nuova WAIT produce STOP_REQUESTED, esegue il
proprio checkpoint cleanup e termina. Totale 136 dispatch, 33 parcheggi reali,
32 segnali e una richiesta stop che risveglia un waiter.

Il cleanup del consumer e quello di una fixture senza payload di driver: non
dimostra teardown di un dispositivo. La raccolta nativa degli stack e reale:
EXIT, ritorno dispatcher, rilascio lease/pin/mapping, COLLECT. Budget uguale
prima/dopo; tutte le 144 PTE task assenti al termine.

Il profilo viene eseguito dopo la prova idle, prima che il timer consumi la
seconda consegna. Non aggiunge scadenze hardware: i tre IRQ LAPIC e la prova
compute/IF=1 precedenti restano verificati. L'attesa supervisionata HPET resta
nel suo profilo esistente; queste nuove attese sono servite dal produttore
cooperativo. Non sono un idle hardware generale per driver arbitrari.

## Review e correzioni

- I sei test Rust nuovi coprono stop/wait, stop/signal in entrambi gli ordini,
  duplicati, generazioni, overflow/rollback, argomenti invalidi, corruzione e
  1000 lifecycle con 32 attese ciascuno su otto eventi.
- Un primo test non compilava per accesso a campi Token privati: corretto
  costruendo WAITING attraverso l'API, senza ampliare la visibilita dei campi.
- I 1000 lifecycle FFI stop verificano ABI, contesto negato, reentrancy BUSY,
  target invalido, duplicati, divieto di raccolta anticipata, query e rilascio.
- Fault injection spostata dal controller in test/fault/fault.c; il controller
  resta a 377 righe. Nessun nuovo Assembly o accesso a registro/porta/MMIO.

## Test host finali

| Prova | Esito | Limite |
| --- | --- | --- |
| Rust | 28/28 debug e 28/28 release; Clippy/warning gate PASS | Core bounded e modelli, non hardware |
| FFI C/Rust | 545700 controlli, 80000 coppie select/yield, 1000 lifecycle stop | C con ASan; callback CPU mock; Rust/ASM non ASan |
| Backend CPU/context | 55900 controlli × 10 | Snapshot privilegiati mock; gate CPL3 reale |
| Switch host | 4004 switch × 10, quattro guard fault attesi/run, due reservation rilasciate/run | GPR/XMM/FP reali; separato da ASan fibers |
| Stack resources | 361000 controlli × 10, ASan | Allocatore/mapping mock |
| Native mapping | 554149 controlli × 10, ASan | CPU/PTE mock |
| IRQ adapter | 22 controlli × 10, ASan | Source/backend mock |
| Idle adapter | Sei scenari × 10; 28/19/22/24/23/28 controlli | Include rifiuto della sessione successiva; hardware/gateway mock |
| Loan accounting | 190002 controlli × 10; 10000 scope/run, ASan | Allocatore/CPU mock |
| Cppcheck | FFI e 20 input backend, nessun error/warning/performance/portability | Style/information conservati nell'XML |

Evidenze: kernel/build/scheduler-host-d70f01a103254917bc8c93f58747b4ed/
e kernel/build/scheduler-context-080f98c5bf8643f587de7f491b4f57f1/.
Log principali: scheduler-stop-core-final.log e scheduler-stop-backend-final.log
sotto kernel/build/. Toolchain MSVC/MASM/Rust supportata, warning come errori.

## Firmware finale

Immagine: kernel/build/scheduler-stop-final/kernel.elf.
SHA256: C2B06D2F3C6BC611719ACE073F7191603F5A5D47305E5EB32916103B0CC751CF.
Dieci boot consecutivi PASS:
kernel/build/scheduler-firmware-273ee095dcbd45d7a2bc33345db88d32/.
QEMU q35/TCG max, 128 MiB, un BSP, OVMF/ESP privati e hash della copia
controllato. Ogni trace contiene 32 cicli e 136 dispatch del nuovo profilo.
Verifier richiede marker, budget, stato finale CPU/CR3/RSP, 144 PTE assenti,
tre IRQ LAPIC e preservazione delle prove IRQ/task e HLT del dispatcher.

La regressione continua e PASS con 18 eventi consegnati e consumati, stop
esterno e cleanup: kernel/build/loop-firmware-63bb78476f3f4363915470df42a877f0/.
La soglia richiesta era nove: la consegna dello stop host e asincrona, quindi
il conteggio effettivo verificato e diciotto, non nove. La trace include il
marker PERSISTENT_OK, 32 cicli e budget restituito, poi quiescenza LAPIC e
chiusura HPET. Non e uno stress di durata arbitraria.

Otto scenari negativi finali PASS, uno per immagine isolata, compilati dai
sorgenti finali. Fault previsti distinti da crash inattesi; il timeout conserva
le lease secondo il contratto terminale esistente.

| Scenario | Directory sotto kernel/build/ |
| --- | --- |
| guard-low | scheduler-firmware-e73443c17d1247b79d790de98c78135b/ |
| guard-high | scheduler-firmware-4c7d86ce04484d67b587a2a1349f4a37/ |
| nx | scheduler-firmware-5a3715dd185d4066be5eb86b0258234a/ |
| retired | scheduler-firmware-6b8d8b439d964ba2971605bb1714831b/ |
| rollback | scheduler-firmware-f3a83ea2ea934963b441d3dbcf2e9382/ |
| blocked | scheduler-firmware-98c10fc7c9144be791cdbef6b277103c/ |
| irq-timeout | scheduler-firmware-a8f140aa4eed46a28d76e77033bdd6c6/ |
| idle-timeout | scheduler-firmware-5062574772ab44d285904f3729217bc3/ |

Manifest di 54 sorgenti/script e hash immagine sotto
kernel/build/scheduler-stop-review/. Nessun sorgente eseguibile modificato
dopo le prove finali; manifest host e firmware ricontrollati. Massimo 377 righe
per sorgente e cinque file diretti per cartella funzionale; whitespace, CR
isolati, link documentali e diff verificati. kernel/main.c, .gitignore e
address_ranges.c non modificati in questo blocco.

Totale finale: dieci boot positivi della stessa immagine, otto negativi di
varianti dedicate e un ciclo continuo. Un boot positivo preliminare e distinto
dalla serie finale. Review conclusa nel perimetro dichiarato, senza nuove
diagnostiche bloccanti. Non e una dichiarazione di scheduler production-ready.

## Limiti e prossimo passo

Coverage linee/branch N/D, Valgrind non eseguito, hardware fisico N/D. ASan
host non dimostra assenza di leak fisici; i boot positivi controllano budget
e raccolta degli stack. I fault terminali conservano risorse secondo il loro
contratto e non costituiscono cleanup o recovery riusciti.

Nessuna preemption, SMP o user mode. Una task che non raggiunge un checkpoint
non viene fermata forzatamente. Il profilo e finito e monouso; restano da
separare completamente le policy diagnostiche dal dispatcher e generalizzare
idle/binding/budget a piu sorgenti o task persistenti reali. Nessuna coda dati
di driver e stata implementata; pending resta una notifica coalescente.
