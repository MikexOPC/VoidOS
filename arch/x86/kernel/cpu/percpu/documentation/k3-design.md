# K3-A: contesto per CPU sul BSP tramite GS

29 ottobre 2026. Scelta A approvata dall’utente; implementazione K3-A verificata sul BSP; prove e limiti in [k3-validation.md](k3-validation.md).
GS/per-CPU è già la direzione D2 approvata; qui si sceglie il passaggio
concreto dal contesto di bootstrap al contesto runtime, prima degli AP.

## Perimetro proposto

Modulo arch/x86/kernel/cpu/percpu/, test rispecchiati in
test/arch/x86/kernel/cpu/percpu/. Include i soli adattatori al provider
arch/x86/lib/synchronization/context.c, punto di bootstrap dopo GDT,
header/build e test necessari. Non cambia allocatore, heap, scheduler,
TSS/IST, routing IRQ o driver. Non invia INIT/SIPI e non avvia AP.
C nella cartella funzionale, eventuale Rust in rust/funzione/, backend
privilegiato in assembly/funzione/. Test/harness e script centralizzati.

## Riscontri nel progetto

- kernel/bootstrap/memory.c inizializza il contesto dei lock prima del
  pool. GDT e TSS arrivano dopo kernel_startup_early, in kernel/main.c.
- descriptors/assembly/activation/segments.asm carica GS durante l’init GDT;
  non si deve affidare la base per CPU a un valore installato prima di
  questa sequenza, ignorando gli effetti del caricamento del segmento.
- Il provider attuale contiene un unico bootstrap_context statico e
  rifiuta gli AP prima di leggere lo stato C tramite ks_boot_cpu.
- I guard contengono il puntatore al contesto e sono legati alla CPU;
  il passaggio è consentito solo con Depth=0, IRQ locali disabilitati,
  nessun guard o borrowed context in uso da altri chiamanti.

## Ricerca e fonti primarie

- Intel SDM 253668-093US (settembre 2026), Vol. 3A §3.4.4, Segment Loading Instructions in IA-32e Mode:
  FS/GS e basi, effetto dei caricamenti di segmento e WRMSR a CPL0;
  la base deve essere canonica, altrimenti WRMSR causa #GP.
  https://cdrdv2.intel.com/v1/dl/getContent/671190
- Intel SDM 335592-093US (settembre 2026), Vol. 4, Table 2-2, IA32_GS_BASE = C000_0101H, R/W,
  prerequisito CPUID.80000001H:EDX[29] (long mode). Il controllo di
  disponibilità MSR usa CPUID.1:EDX[5], preceduto dai massimi leaf.
  https://cdrdv2.intel.com/v1/dl/getContent/671098
- Linux v6.12, arch/x86/kernel/cpu/common.c,
  switch_gdt_and_percpu_base: distingue area early e runtime e aggiorna
  GSBASE dopo la GDT; cpu_init gestisce separatamente i registri di
  segmento e MSR. Riferimento di sequenza/lifetime, non codice copiato.
  Licenza GPL-2.0-only, nessun codice trasferito.
  https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/cpu/common.c
- Documentazione Linux x86_64/fsgs: GS rimane funzionale in 64 bit;
  FSGSBASE è distinto dall’accesso privilegiato via MSR e richiede
  capacità/abilitazione proprie. Non abilitiamo CR4.FSGSBASE in questo blocco.
  https://docs.kernel.org/arch/x86/x86_64/fsgs.html

Gli offset effettivi delle nostre strutture sono contratti ABI software
con static_assert, non offset Intel inventati. Prima del backend Assembly
si annotano fonte/sezione, indirizzo MSR, larghezza e scopo su ogni accesso.
IA32_KERNEL_GS_BASE (C000_0102H) e SWAPGS non sono necessari a questa fase
senza user mode: non se ne dichiara implementata la transizione ring 3.

## A scelta utente: passaggio graduale dopo GDT

Prima della GDT resta il provider di bootstrap già verificato da K2.
Dopo GDT verificata e ritiro dello stack iniziale si prepara un’area per CPU persistente nel BSS
alto del kernel, senza allocazioni né nuovi mapping. Solo il BSP diventa
attivo in questo blocco. Capacità del contenitore limitata a 64 slot;
nessuna CPU eccedente viene accettata o troncata silenziosamente. Gli slot
non predisposti non sono online e non sono conteggiati come AP avviati.

Il record contiene Self, riferimento al contesto ritirato, indice logico,
stato e il proprio contesto lock. Il contatore NextToken viene preservato,
senza aggirare l’esaurimento dei token durante la migrazione.
Indice logico zero significa BSP del nostro registro, non APIC ID zero:
non inventiamo un’identità hardware prima della discovery. Stack, TSS/IST,
task e clock per CPU verranno aggiunti con i rispettivi contratti, senza
placeholder presentati come funzionalità operative.

Sequenza concettuale, non eseguibile:

    EARLY_VALID -> PREPARED -> GS_VERIFIED -> ACTIVE
                                |
                         errore -> fail-stop

1. Verificare BSP, prerequisiti MSR/long mode, puntatori canonici e
   appartenenza all’immagine kernel, GDT completata e contesto quiescente.
2. Preparare il nuovo owner e il contesto, ancora non pubblicati.
3. Scrivere IA32_GS_BASE con l’indirizzo dell’owner, poi leggerlo e
   verificare gli offset Self e Context contro l’owner registrato.
4. Ritirare il vecchio contesto e pubblicare il nuovo provider; rifiutare
   una seconda inizializzazione, il riuso del contesto ritirato e la
   pubblicazione di dati parziali. Il vecchio contesto non viene copiato
   mentre contiene guard; i puntatori presi in prestito non sopravvivono
   al punto quiescente di migrazione.
5. Ripristinare IF del chiamante solo dopo il passaggio verificato.

Un fallimento prima della scrittura hardware lascia utilizzabile lo stato
precedente. Dopo una scrittura non verificata, il kernel si arresta con
ownership conservata: niente fallback silenzioso a un contesto diverso.

Il reader valida la base MSR contro owner conosciuti prima di dereferenziare
GS o il puntatore letto da GS. Basi zero, fuori registro, owner corrotti,
stati non attivi e CPU non predisposte sono rifiutati; non si rende sicuro
un puntatore arbitrario. Nessuna richiesta di dati CPU da NMI/fault in
questa fase; i loro percorsi esistenti non usano il nuovo accessor.
Il provider continua a escludere gli AP finché il loro ingresso non è
progettato. Non ci sono SWAPGS, migrazione di task o cambi di GS nello
switch cooperativo del BSP. I successivi reload GDT devono coordinarsi
con il provider e ripristinare/verificare GS; non sono operazioni libere.

Nel checkpoint K3-A il clock runtime di contesa era un blocco successivo, prima
dell’uso concorrente degli allocatori da AP. Il collegamento BSP è ora
descritto dal [contratto K3-B](k3b-design.md). Nel checkpoint K3-A rimane
il contratto K2. Non si dichiara sicuro avviare AP che usino questi servizi
prima di aver predisposto contesto e clock di attesa propri.

## B alternativa: GS fin dal primissimo ingresso

Portare GDT e area per CPU prima del pool, rifacendo l’ordine del bootstrap
e coordinando ogni caricamento di segmento successivo. Riduce la presenza
di due fasi, ma coinvolge più entry/descrittori e cambia prerequisiti già
verificati. Richiede un perimetro più ampio prima dell’implementazione.

## Accettazione per K3-A

- Host: transizioni, capacità e indici, owner non pubblicati, alias,
  bootstrap doppio, migrazione a Depth diverso da zero, context ritirato,
  base GS errata/zero, Self errato, AP non predisposto; IF preservato.
- Mock delle operazioni MSR distinto dai test hardware; niente WRMSR sul
  sistema Windows host. Static analysis e sanitizer del C pertinente.
- Build freestanding con warning come errori; nessun file oltre 400 righe
  e cinque file sorgente diretti per cartella funzionale.
- QEMU: almeno dieci boot consecutivi positivi dell’ELF finale, GSBASE
  osservata dal monitor uguale all’owner, marker di promozione una volta,
  contesto early ritirato, pool/heap validi, test guard e IF riusciti.
- Regressione del profilo ordinario e dei fault pertinenti; gli AP restano
  fermi anche quando QEMU è configurato con quattro CPU.
- Nessuna affermazione di K3 completo: seguiranno clock/dati completi per
  CPU, risorse AP, trampoline e sequenza di avvio, prove di quattro CPU
  realmente pronte e test d’integrazione richiesti dalla roadmap.

## Lifetime del clock durante la migrazione

Il descrittore Wait resta preso in prestito: deve sopravvivere al contesto early.
La promozione rifiuta descrittori sovrapposti al nuovo owner e Clock.Context
che punti dentro il vecchio contesto o il nuovo owner. Il puntatore opaco
non espone la dimensione dell’oggetto: questi controlli non certificano
la durata di un oggetto esterno, che resta responsabilità del chiamante.
