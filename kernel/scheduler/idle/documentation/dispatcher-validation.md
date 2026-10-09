# Dispatcher separato e riuso idle: validazione finale

8 ottobre 2026. [Contratto](dispatcher-design.md). Miglioramenti del solo modulo
scheduler con gli agganci timer necessari. [Blocco stop precedente](persistent-validation.md)
resta uno snapshot storico. Nessun nuovo driver, Assembly o registro MMIO.

## Risultato

Il motore dispatcher/engine.c non contiene profili diagnostici, due task
hardcoded, entry point di test o conoscenza del LAPIC. Configurazione privata
bounded per 1..8 task pubblicate, callback native, limiti dispatch/idle e
statistiche possedute. diagnostic/profiles.c mantiene workload, limiti e
verifiche specifiche; integration/kernel.c mantiene contesti, lease, mapping
e mailbox. Rust resta proprietario di queue, generazioni, wait/stop e pending.
Nessun borrow Owner attraversa callback native, switch o HLT.

SELECT viene ripetuto dopo idle: il return della callback non prova un wake.
Limiti esauriti non provocano ulteriori SELECT/arm. Il guard impedisce rientro
ricorsivo sotto contratto BSP/IF=0. Errori di run/reply/collect/release non
inventano un rollback di stato gia pubblicato o liberano risorse. La raccolta
resta EXIT, rilascio native sul dispatcher, COLLECT Rust.

Due sessioni hardware idle, sulla seconda e terza consegna LAPIC: source
consumata/quiescente prima del riarmo, due lease per sessione e HPET indipendente.
Ogni task osserva l'epoch prima di WAIT; l'evento 3 viene riusato con epoch
aggiornato, senza azzerare lo stato Rust. Sei dispatch e due waiter risvegliati
per sessione; chiusura del prestito prima della raccolta e budget restituito.
Il timer riusa entrambe le attese e non arma/draina HPET una seconda volta.

Il workload stop resta a 32 cicli e 136 dispatch. Le policy diagnostiche sono
separate dal motore, ma l'orchestrazione nativa resta una sequenza finita di
profili fidati: non e ancora un servizio generale di creazione task per driver.

## Correzioni nel ciclo debug

Il nuovo test di riuso ha scoperto un mock che conservava Active=1 dopo la
consegna, diversamente dall'handler reale che ferma la source: corretto il mock.
Ha anche evidenziato che una chiamata duplicata a idle_wait sovrascriveva il
codice diagnostico della sessione; ora il rifiuto inattivo/duplicato conserva
il risultato precedente. Le prove prima della correzione non sono quelle finali.

Il budget dispatch viene verificato prima di SELECT; non lascia un nuovo
RUNNING non eseguito quando il limite e esaurito. Callback native verificano
attore libero prima del run e bound dell'indice prima della raccolta. Manifest
host esteso ai sorgenti del motore C e relativa fixture, oltre ai file Rust/FFI.

## Prove host finali

| Prova | Esito | Limite |
| --- | --- | --- |
| Rust | 28/28 debug e 28/28 release; Clippy/warning gate PASS | Core e modelli |
| FFI C/Rust | 545700 controlli; 80000 coppie select/yield | CPU mock, C ASan; Rust/ASM non ASan |
| Engine C + Owner Rust reale | 69078 controlli su nove scenari; 1000 sessioni con 1/2/8 task e 1000 sessioni stop | CPU, context switch e risorse native mock; C ASan |
| CPU/context | 55900 controlli × 10 | Snapshot privilegiati mock; gate CPL3 reale |
| Switch host | 4004 switch × 10; quattro guard fault attesi/run, due reservation rilasciate/run | GPR/XMM/FP reali, separato da ASan fibers |
| Stack resources | 361000 controlli × 10, ASan | Allocatore/mapping mock |
| Native mapping | 554149 controlli × 10, ASan | CPU/PTE mock |
| IRQ bridge | 22 controlli × 10, ASan | Source/backend mock |
| Idle adapter | Otto scenari × 10; 31/20/23/25/24/31/56/49 controlli | Include riuso con epoch aggiornato e rifiuto budget_end nella seconda sessione |
| Loan accounting | 190002 controlli × 10; 10000 scope/run, ASan | Allocatore/CPU mock |
| Cppcheck | FFI, 23 input backend e due interfacce timer; nessun error/warning/performance/portability | Style/information conservati nell'XML |

Engine negativi: idle senza wake entro limite, comando invalido, contesto perso,
collect/reply/release rifiutati e budget esaurito. Reentry viene tentato anche
nei positivi senza toccare il nuovo stats o l'Owner. Gli errori verificano stato
retained e assenza di raccolte indebite, non dichiarano cleanup riuscito.

Evidenze sotto kernel/build/:

- scheduler-host-6e3424080b094d698dc4f44d1be52cb4/: Rust, FFI e engine ASan.
- scheduler-context-192f05e388a04a7f951e3a1f1798080b/: backend e otto adapter idle.
- scheduler-dispatch-review/: manifest, analisi interfacce e trace supplementari.
- scheduler-dispatch-core-checked.log e scheduler-dispatch-backend-final2.log.

Toolchain MSVC/MASM/Rust supportata, warning come errori. Nessun warning
soppresso per ottenere PASS. Owner Rust resta 472 byte senza heap; ASM/context
ABI invariata. Nessun benchmark di overhead dei callback o latenza dichiarato.

## Firmware finale

Immagine kernel/build/scheduler-dispatch-verified/kernel.elf.
SHA256: 1DEBCDD6548522F51C93BD417D2F6BC30C2D37199CABEC8FED57A29A971DBC47.
Dieci boot consecutivi PASS:
kernel/build/scheduler-firmware-3c7b85ca998340c7b71e2bb10a54c880/.
QEMU q35/TCG max, 128 MiB, un BSP, OVMF/ESP privati con hash della copia.

Verifier controlla entrambe le sessioni idle, marker e contatori, budget uguale,
CR3 invariato, RSP finale sullo stack principale e 144 PTE task assenti.
Tre consegne LAPIC reali: prima nel compute task, seconda e terza al ritorno
HLT del dispatcher, con IF=1 nei frame. Verifica supplementare RSP delle due
IRQ idle nello stack principale su tutte le dieci trace:
kernel/build/scheduler-dispatch-review/hardware-idle-stacks.json.

Nove negativi finali PASS, ciascuno con immagine isolata dai sorgenti finali:

| Scenario | Directory sotto kernel/build/ |
| --- | --- |
| guard-low | scheduler-firmware-44a28ce6e9574cf39651de6c342ae47c/ |
| guard-high | scheduler-firmware-f4ed91aaba5f4bde99f2bc089388c265/ |
| nx | scheduler-firmware-fbcdcf49b4734a0983862b8930ededb8/ |
| retired | scheduler-firmware-804e103a47734183afb9135727d24bc4/ |
| rollback | scheduler-firmware-2bef194348e842b490e3bca49d478e4c/ |
| blocked | scheduler-firmware-5486b9d968fc4253adfade7d184e7d04/ |
| irq-timeout | scheduler-firmware-8c7e3d30cdc443d095bb8ab07e77744c/ |
| idle-timeout | scheduler-firmware-c486bab361ba4cde921870ff89489474/ |
| idle-repeat-timeout | scheduler-firmware-4330cc40fce045d18dab19f253bb1557/ |

La nuova fault injection ferma la source prima della terza consegna. HPET
riprende la CPU e classifica TIMEOUT/DEADLINE; marker distinto da READY, IF=0,
due lease ancora pinnate e guardie assenti. Non e recovery o rilascio riuscito.

Ciclo continuo PASS: 18 eventi effettivi, stop esterno e cleanup verificati,
kernel/build/loop-firmware-712fad0db01a4f93980dc4caecd3b371/.
Soglia stop richiesta nove, consegna host asincrona; conteggio verificato 18.
La trace include entrambe le sessioni idle e il workload stop, poi consumo,
quiescenza LAPIC e chiusura HPET. Non e uno stress di durata arbitraria.

## Qualita e limiti

Massimo sorgente 373 righe, massimo cinque file diretti nelle cartelle
funzionali. C alla radice funzionale, test in test/, artefatti in kernel/build/.
Manifest e hash ricontrollati; nessuna modifica eseguibile dopo le prove finali.
Le prove preliminari e la vecchia serie prima del fix diagnostico sono distinte
dalla serie finale sopra. Review del diff/stile/link e dei confini completata.
kernel/main.c, .gitignore e address_ranges.c non modificati in questo blocco.

Interfaccia engine verificata host con 1/2/8 task; backend hardware verificato
con due stack, una source LAPIC e due sessioni finite. Non generalizza ancora
accounting HPET o binding a otto lease/sorgenti arbitrarie. Pending rimane una
notifica coalescente, non una coda dati di driver. La latenza idle include
il drenaggio HPET; nessuna nuova preemption, SMP o user mode.

Coverage linee/branch N/D, Valgrind non eseguito, hardware fisico N/D. ASan
non certifica leak RAM fisica; firmware positivo verifica budget/pin/PTE e
raccolta degli stack. Perdita simultanea di LAPIC e HPET/controller resta fuori
dal modello. Nessuna dichiarazione di scheduler production-ready.
