# K2-A: primitive di sincronizzazione

27 ottobre 2026. Contratto del primo modulo K2. Opzione A scelta dall'utente:
attesa sorvegliata e arresto diagnostico. La decisione D4 in [g7-smp-user.md](../../../docs/design/g7-smp-user.md)
ha già scelto ticket lock, irqsave, atomiche e ordine dei lock.

## Perimetro e sequenza

Primo modulo: `kernel/sync/`, test in `test/kernel/sync/`. Include soltanto
il backend IF/PAUSE x86 in `arch/x86/lib/synchronization/` e gli adattatori
di build/probe necessari. Allocatore e heap non vengono modificati prima
che queste primitive superino i propri gate. K2-A non completa tutto K2.

## Riferimenti e differenze motivate

- [Linux v6.12, spinlock generico](https://raw.githubusercontent.com/torvalds/linux/v6.12/include/asm-generic/spinlock.h):
  riferimento FIFO e try-lock, con contatori a 16 bit nello stesso word e
  presupposti specifici sulle atomiche di larghezza diversa. Non è una
  descrizione automatica del lock x86 corrente: quel backend usa qspinlock.
  Non copiamo codice GPL. Il progetto usa contatori separati a 64 bit,
  senza accessi atomici sovrapposti e senza riciclo dei ticket.
- [Rust AtomicU64](https://doc.rust-lang.org/core/sync/atomic/type.AtomicU64.html)
  e [Ordering](https://doc.rust-lang.org/core/sync/atomic/enum.Ordering.html):
  primitive no_std, allineamento di 8 byte, acquire/release. La documentazione
  online osservata è 1.99.1; il compilatore locale è 1.98.1. Le API usate
  vanno verificate anche su quest'ultimo, senza dipendere da nightly.
- [Intel SDM Vol. 3A, 253668-090US, febbraio 2026](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):
  §2.3: RFLAGS.IF è il bit 9; §7.8.1: CLI/STI controllano gli interrupt
  mascherabili locali; NMI ed eccezioni non vengono esclusi. §§11.10.2 e
  11.10.6.1: PAUSE nei cicli di attesa, senza attribuirgli sincronizzazione.

## Proprietà, linguaggi e lifetime

- Rust implementa le atomiche e la policy numerica di ranghi/limiti.
  Le conversioni FFI da puntatore C ad AtomicU64 sono un confine unsafe
  ristretto e documentato: richiedono memoria RW viva, allineata, di taglia
  corretta e accessi concorrenti esclusivamente atomici. Non validano da
  sole il lifetime o le pagine di un puntatore arbitrario.
- C possiede lock, contesto e guard. Nessuna allocazione nella primitiva.
  Due ticket atomici (Next/Serving); Self e rango immutabili dopo init.
  L'init richiede memoria privata non pubblicata; vietati copia/reinit del
  lock vivo e rilascio della memoria con proprietario o attese pendenti.
- Un contesto esclusivo per CPU contiene al massimo otto guard attivi,
  in ordine di rango strettamente crescente. Niente stato condiviso globale
  per la pila dei lock. In K3 il chiamante lo legherà ai dati per CPU;
  fino ad allora il kernel usa il contesto BSP, i test host uno per thread.
- I guard sono non copiabili per contratto, legati al proprio indirizzo,
  al contesto, al lock, al ticket e a un token monotono. Release solo LIFO,
  con confronto della voce del contesto prima di dereferenziare il lock.
- Ticket/token non fanno wrap: esaurimento rifiutato prima di prenotare.
  Questo evita l'ABA dovuto al riciclo del contatore durante un try-lock.
- ASM salva IF e lo disabilita; ripristina solo il precedente IF, dopo il
  release. Non riscrive RFLAGS intero. Non si dorme né si cede la CPU sotto
  lock. Gli handler NMI/fault non acquisiscono questi lock ordinari.

## Ordine e attese — scelta richiesta

I ranghi effettivi di heap/pool/paging saranno fissati analizzando le loro
chiamate prima dell'integrazione: nessun ordine inventato in questo blocco.
I test delle primitive usano ranghi distinti privati.

Proposta v0: `try_lock_irqsave` non entra in coda se il lock è conteso.
L'acquisizione bloccante prenota un ticket con CAS e attende Serving con
load-acquire; il release pubblica Serving successivo con store-release.
Next viene prenotato atomicamente; non protegge il payload, che dipende
dall'acquire di Serving e dal release del proprietario precedente.

**A, consigliata: attesa sorvegliata e arresto diagnostico.** Il chiamante
fornisce clock senza lock e budget di attesa; un limite di poll finito
rileva anche un clock fermo. Nessun valore universale in millisecondi.
Prima di prenotare si può rifiutare senza effetti e ripristinare IF.
Dopo la prenotazione, timeout, clock guasto o contabilità impossibile
fermano il kernel: non è lecito restituire errore lasciando un buco nella
coda. Bootstrap senza clock: ammesso solo il percorso non conteso; una
contesa inattesa è terminale.

**B: attesa standard senza scadenza.** Più semplice, ma se il proprietario
non rilascia il lock la CPU può restare bloccata per sempre. Non soddisfa
il vincolo locale sui cicli limitati senza un override esplicito.

Pseudocodice non eseguibile del percorso A:

    salva IF e disabilita IRQ locali
    verifica contesto, rango, lifetime e guard destinazione
    prenota ticket senza wrap, rispettando il budget di tentativi
    finché Serving != ticket:
        verifica deadline / limite di poll / validità del clock
        in caso di guasto: fail-stop, senza annullare il ticket
        PAUSE
    registra guard nel contesto; esegui la sezione critica
    verifica release LIFO e ticket proprietario
    invalida guard, pubblica Serving successivo con release, ripristina IF

## Gate prima dell'adozione da parte della memoria

1. Test Rust debug/release e Clippy: order/rank, allineamento, limiti ed
   esaurimento. Nessuna copertura percentuale inventata.
2. C con warning come errori e ASan: alias, token stantii, doppio release,
   recursion/rank inversion, release fuori ordine e preservazione IF.
3. Host multithread con atomiche Rust reali: contesa, payload pubblicato,
   mutua esclusione e FIFO osservato dall'ordine dei ticket. Non è SMP guest.
4. Errori di attesa: deadline/clock fermo dopo prenotazione devono fermare
   il processo di test, mai proseguire con ticket abbandonati.
5. Dieci boot consecutivi del diagnostico x86: IF reale, annidamento,
   ripristino e marker; poi regressione del percorso ordinario. Gli AP
   non sono avviati: non dichiarare contesa hardware fra core guest.
6. Accounting: nessuna allocazione nella primitiva; ASan host non dimostra
   assenza di leak dell'intero kernel. Diff, fonti, 400 righe/cinque file.

Solo dopo questi gate si passa all'allocatore, poi all'heap. La scansione
dei lettori esterni e dei contatori di generazione fa parte dei successivi
adattamenti: un lock sui soli writer non rende automaticamente SMP-safe
tutti gli osservatori esistenti.

## K2-B: accessor approvato e binding BSP

L'utente ha scelto un accessor architetturale mantenendo stabili le API
della memoria. `kernel_lock_bootstrap_initialize` inizializza una sola
volta il contesto statico, soltanto sulla CPU BSP verificata. L'accessor
restituisce NULL prima dell'init e sugli AP: non legge lo stato C del BSP
da un AP. Questo descrive il binding early K2: dal blocco K3-A il provider
passa a GS dopo GDT e ritiro dello stack iniziale, mantenendo gli AP fermi.
[Prove K3-A](../../../arch/x86/kernel/cpu/percpu/documentation/k3-validation.md).

Intel SDM 090US Vol. 2A, istruzione CPUID: leaf 1 EDX bit 5 (MSR) e bit 9
(APIC), dopo la verifica del massimo basic leaf. Vol. 3A §§13.4.2–13.4.4,
figura 13-5: IA32_APIC_BASE all'indirizzo MSR 1Bh, bit 8 BSP. Il backend
legge soltanto tale MSR e preserva RBX secondo ABI Microsoft x64.

`kernel_lock_context_bind_wait` richiede contesto valido e Depth=0;
rifiuta clock invalido, alias, puntatore disallineato e budget nullo o
oltre metà periodo. Preserva IF anche sul rifiuto. Il descrittore clock
e il suo Context sono presi in prestito e devono restare vivi e immutabili
finché il binding è attivo; unbind solo a contesto quiescente, prima di
liberarli. La funzione non rende sicuro un puntatore arbitrario. Contesto
esclusivo della CPU, niente yield/migrazione o uso da NMI/fault; non si
può trasferire un guard a un'altra task.

Dal 28 ottobre il bootstrap ordinario inizializza il contesto prima
dell'allocatore; il probe usa quel contesto senza reinizializzarlo. Il
clock nel checkpoint K2 restava non collegato: prima del clock il contratto
ammetteva soltanto il percorso non conteso. Dal blocco K3-B il runtime BSP
usa la sorgente ACPI PM: [contratto](../../../arch/x86/kernel/cpu/percpu/documentation/k3b-design.md). Nessun AP avviato. Il successivo blocco pool
adotta ora i lock e query piccole: vedere
[contratto](../../mm/physical/documentation/k2-design.md) e
[prove](../../mm/physical/documentation/k2-pool-validation.md).
Nel successivo blocco K2-C anche l’heap adotta Gate e stati di prenotazione:
[prove heap](../../mm/heap/documentation/k2-validation.md).
Holdings, mapper e avvio AP mantengono i propri gate; non è una prova SMP.
