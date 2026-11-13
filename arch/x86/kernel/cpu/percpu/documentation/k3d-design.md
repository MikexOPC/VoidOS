# K3-D — risorse private degli AP, prima dell’avvio

29 ottobre 2026. Scelta A approvata: arena completa con rollback; implementazione verificata sul BSP/QEMU; [prove e limiti](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu).
K3-C associa indici e identità; questo blocco prepara risorse per gli AP
abilitati dal firmware, senza INIT/SIPI, LTR su AP o nuovo core operativo.

## Riscontri e vincoli reali

- Stack BSP e IST esistenti hanno indirizzi fissi; le routine kernel dei
  descrittori verificano quei precisi indirizzi. Non sono un allocator AP.
- Il mapper regioni ha quattro slot da 32 pagine: non copre stack separati
  per fino a 63 AP. L’allocatore fisico dispone di 64 lease complessive,
  alcune già occupate da bootstrap e dispositivi. Quattro o cinque lease
  per AP non scalano a 64 CPU senza cambiare il relativo contratto.
- L’inventario paging accetta soltanto rami e owner conosciuti: un nuovo
  mapping deve entrarvi esplicitamente, non eluderne la verifica.
- Lo slot PML4 487 non è assegnato in layout.h né nei riferimenti cercati
  nelle configurazioni e nei mapper attuali. È proposto come spazio
  dedicato, da riservare centralmente e verificare contro tutti i profili.

## Fonti primarie

Intel SDM Vol.3A, 253668-078US (dicembre 2022), §8.7 e Figure 8-11/8-12:
TSS a 64 bit, RSPn, ISTn, descrittore TSS a 16 byte; §6.14.5: IST
selezionato dalla gate IDT, sette puntatori disponibili. Nella TSS corrente
RSP0 è a offset 4, IST1/2/3 a 36/44/52; campi a 64 bit canonici. I numeri
sono offset del formato hardware, non ipotesi sul padding del compilatore.
Lo stesso documento §4.5 descrive PTE a 4 KiB e relativi permessi.
https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf

Linux v6.12, arch/x86/mm/cpu_entry_area.c: aree per CPU, stack eccezioni
con guardie e mapping separati, TSS senza attraversamento di pagina.
Riferimento concettuale; GPL-2.0-only, nessuna copia di codice. Il nostro
profilo non importa automaticamente i mapping RO o l’intera gestione
Linux delle eccezioni; l’attivazione avrà il proprio contratto.
https://github.com/torvalds/linux/blob/v6.12/arch/x86/mm/cpu_entry_area.c

## A — preparazione completa prima del primo avvio (proposta)

Un’arena dedicata agli AP, posseduta dal modulo, usa una lease dati e una
lease tabelle. Stack di CPU diverse hanno pagine disgiunte, anche se la
prenotazione fisica è comune. Nessun hotplug o rilascio individuale dopo
l’avvio: è una scelta del bring-up statico approvato in K3-C.

Modulo: arch/x86/kernel/cpu/percpu/resources/ con responsabilità separate
per geometria, mapping/lifecycle e descrittori; C nella cartella funzionale,
validazione pura Rust in rust/funzione/. Test centralizzati rispecchiati,
script in scripts/, artefatti in build/. Nessuna cartella segnaposto.

### Geometria proposta, non costanti già operative

- Slot PML4 487 supervisor. I primi 2 MiB restano assenti; arena dal
  successivo confine 2 MiB. Nessun mapping nella metà bassa.
- Indice logico * 256 KiB individua un’unità virtuale; indice zero resta
  vuoto perché il BSP mantiene le proprie risorse. Fino a 64 unità, 16 MiB
  virtuali riservati, con pagine presenti solo per gli AP predisposti.
- Per AP: stack principale 64 KiB e tre IST da 16 KiB, ognuno con pagina
  assente prima e dopo. Totale stack fisici 28 pagine, 112 KiB; span con
  guardie 144 KiB dentro l’unità da 256 KiB.
- Una pagina aggiuntiva per AP, separata dagli stack a offset 192 KiB
  dell’unità, contiene GDT/TSS e relativo descrittore/puntatore. TSS a
  offset 48 del record di 176 byte, interamente nella pagina dedicata.
  Le pagine adiacenti restano assenti. Non usare un array di record da
  176 byte che potrebbe far attraversare alla TSS un confine di pagina.
- Dati 29 pagine per AP: 116 KiB. Con quattro CPU totali: 348 KiB per tre
  AP, oltre alle tabelle e ai metadati. Con 64 CPU: 7.308 KiB per 63 AP.
- Tabelle proprie: PDPT, PD e solo i PT necessari alla geometria, massimo
  otto PT; da tre a dieci pagine per il profilo con AP. Con solo BSP non
  si installa un ramo vuoto e non si prenotano lease AP.
- Pagine dati WB, RW, NX, U/S=0; niente pagine grandi, alias aggiuntivi o
  cache policy MMIO. Ramo e foglie devono corrispondere alle lease pinned.

L’area GDT/TSS resta RW/NX in preparazione: nessun LTR viene eseguito su
questi oggetti in questo blocco. Il futuro ingresso dovrà caricare la GDT,
la TSS privata e poi GS nell’ordine verificato, con trattamento esplicito
del bit busy; nessuna promessa di descrittori già attivati.

### Ownership, stati e failure

Owner statico e non spostabile, snapshot delle identità immutabile,
conteggio finito e tutti gli indici controllati da Rust. Una lease dati
contigua copre stack e pagine descrittori, una lease distinta le tabelle;
consuma due slot dell’allocatore, non uno per stack. Richiede però una
prenotazione contigua più grande: se manca, fallisce prima di avviare AP.
Non aumenta implicitamente i limiti dell’allocatore.

Sequenza concettuale, non eseguibile:

    EMPTY -> PLAN_VALID -> LEASES_PINNED -> TABLES_BUILT
          -> MAPPED_VERIFIED -> DESCRIPTORS_PREPARED

Un errore prima della pubblicazione ritira soltanto le risorse del modulo:
revoca delle foglie/ramo, invalidazione BSP, unpin/free dopo unmapping e
verifica del budget originario. Nessun AP è ancora in esecuzione, dunque
nessuna invalidazione remota viene simulata con un INVLPG locale.
Se il cleanup non è dimostrato, conservare ownership/pin e arrestarsi;
mai liberare pagine ancora raggiungibili o dichiarare rollback riuscito.

Nel futuro avvio, prima che un AP possa usare stack/TSS, le risorse devono
diventare committed e non rilasciabili. Dopo un avvio parziale, free è
vietato senza prova di arresto e assenza di riferimenti: tale protocollo
appartiene al blocco di avvio, non è implementato da questa preparazione.

La prova diagnostica prepara, verifica e rilascia prima dell’avvio AP.
Il percorso ordinario può conservare le risorse predisposte e pinned,
con stato esplicito PREPARED: questo non incrementa il numero di core pronti.

## B — prenotazioni separate e preparazione progressiva

Allocare un AP alla volta riduce la richiesta di RAM contigua e facilita
un futuro rilascio individuale. Consuma però più lease, richiede un budget
di metadati diverso per arrivare a 64 CPU e rende più complesso il rollback
se in seguito si avviano core prima di avere tutte le risorse. Non basta
un ciclo attorno al mapper BSP o ai suoi quattro slot. Questa alternativa
richiede rivedere limiti/accounting e il protocollo dell’avvio parziale.

## Perimetro da concordare

Modulo risorse AP con soli adattatori necessari a layout centrale,
inventario/autorità paging e relative interfacce usate dai verificatori,
bootstrap, primitive descrittori riutilizzabili, build e test correlati.
Le interfacce dei lock/allocatori restano stabili; niente riscrittura di
scheduler, watchdog o driver. Gli eventuali adattatori dei test/lettori
paging devono mantenere i profili esistenti e le verifiche complete.

Non include trampolino, riserva sotto 1 MiB, CR3 di transizione, IPI,
avvio AP, clock/GS degli AP, handler concorrenti o TLB shootdown generale.
Serviranno contratti separati prima che un AP esegua queste risorse.

## Accettazione

- Rust: indici 0/1/63/64, overflow, canonicalità, disgiunzione degli span,
  guardie, conteggi di pagine/PT e budget per 1/4/64 CPU.
- C ASan: ownership, input invalidi, doppia preparazione, failure iniettata
  a ogni allocazione/pin/pubblicazione, cleanup e budget ripristinato;
  distinto dal modello delle PTE e dalle prove sul paging reale.
- QEMU: dieci boot consecutivi dell’immagine finale, confronto dal monitor
  di foglie, permessi, guardie, lease, indirizzi TSS/IST e memoria azzerata.
  Con quattro CPU configurate, risorse per tre AP e un solo BSP operativo.
- Fault guard/NX in immagini dedicate, accounting dopo teardown diagnostico,
  regressioni BSP, lock/clock/identità e console VirtIO/VT-d.
- Sorgenti <=400 righe, massimo cinque diretti per cartella, warning come
  errori, analisi statica, review finale delle fonti e del diff. Coverage
  N/D se non misurata; nessuna dichiarazione di leak zero da ASan soltanto.

Questa fase è solo documentale: controlli di fonti, coerenza, link e diff;
nessun nuovo PASS runtime attribuito al progetto prima della scelta.

## Dettagli verificati durante l’implementazione

Lo snapshot interno del paging cresce da 3.944 a 4.000 byte con un record
AP di 56 byte. Non cambia il contratto loader/kernel. Tag 60–69 identificano
esclusivamente le tabelle dell’arena AP. Inventario comune, controllo delle
tabelle video e controllo finale GPU devono riconoscere questa autorità;
non classificare qualunque tag >=48 come heap.

Il rilascio usa kernel_physical_release_after_unmap per le lease pinned:
se l’allocatore rifiuta il rilascio, ownership e pin rimangono conservati.
I metadati del nuovo owner devono essere distinti da contesto, finestra,
allocatore, inventario e mapping LAPIC. Le API C interne richiedono oggetti
validi e vivi del kernel; i controlli numerici non sono una user-copy sicura.

Il wrapper nativo preserva IF; bind/unbind richiedono BSP e IF=0. Il profilo
diagnostico prepara/verifica/rilascia, controlla il budget e prepara di
nuovo; il percorso ordinario conserva le risorse PREPARED. Non esiste ancora
un ingresso AP che possa usarle né uno stato committed operativo. Prima
che arrivi il primo INIT/SIPI va introdotto il vincolo di non rilascio.
La riserva fisica del futuro trampolino sotto 1 MiB dovrà precedere le
allocazioni generiche: questa arena non garantisce da sola una pagina SIPI.

La prova interattiva VirtIO è esplicitamente configurata con quattro vCPU
per coprire i record AP con GPU attiva; il runner continua a distinguere un
solo core OS pronto. Il suo default da riga di comando resta una vCPU.
