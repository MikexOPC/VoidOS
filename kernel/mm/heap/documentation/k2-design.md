# K2-C: sincronizzazione del nucleo heap

28 ottobre 2026. Scelta A approvata dall’utente, implementata e verificata:
[prove del blocco](k2-validation.md) e
[gate finale K2 del 29 ottobre](../../../sync/documentation/k2-integration.md).
Perimetro: kernel/mm/heap e test/kernel/mm/heap; successivamente solo gli
adattatori build, binding e test necessari, se concordati. Niente SMP guest,
crescita del mapping, cache per CPU o nuove allocazioni negli handler IRQ.

## Riscontri locali

heap.c contiene slab per classi e sequenze di pagine, con metadati fissi.
alloc richiama pages, che richiama il validatore pubblico: i wrapper con
lock non possono mantenere questa riacquisizione. Stats e le bitmap sono
mutabili, quindi anche valid e stats devono usare il guard.

Il fill attuale può attraversare 4096 pagine (16 MiB) per alloc/free.
Un lock irqsave che copra tutto serializza correttamente, ma estende a quei
byte la finestra con IRQ disabilitati. Non abbiamo una misura di tale
latenza e non ne dichiariamo un costo universale.

Ulteriori verifiche necessarie: overflow bytes+8 prima della scelta classe;
limiti RunLength/SlabOf/SlabClass prima di indici e shift; range heap distinto
dai metadati; init privata e nessuna reinizializzazione con owner pubblicato.
Le prove attuali reinizializzano fixture private: vanno distinte dal lifetime
operativo, non usate come autorizzazione a distruggere un heap vivo.

## Ricerca e compromessi

- Linux v6.12, mm/slub.c, commento iniziale e sezioni Lock order, cpu_slab
  lock e irq/preemption: usa più livelli e percorsi per CPU/atomici.
  Fonte primaria fissata: https://github.com/torvalds/linux/blob/v6.12/mm/slub.c
  È riferimento per ownership e lock order, non codice da copiare né un
  progetto da riprodurre nella nostra v0. Licenza GPL-2.0, nessun codice copiato.
- Zephyr, Memory Heaps, sezioni Synchronized Heap Allocator e Low Level Heap
  Allocator: distingue il servizio sincronizzato da sys_heap, che richiede
  serializzazione esterna; la ricerca limitata evita attese non limitate.
  Fonte ufficiale consultata il 28 ottobre 2026:
  https://docs.zephyrproject.org/latest/kernel/memory_management/heap.html
  Il suo limite in cicli non viene attribuito al nostro first fit.

## A scelta utente: metadati sotto lock, fill su memoria riservata

Un Gate per heap, rango 200, accessor CPU già approvato e irqsave; il pool
fisico ha rango 300. Il core heap non chiama pool, mapper, device, logging,
allocazioni o callback. Prima separare facade e core interno: un guard per
fase, nessuna chiamata pubblica ricorsiva. Init esclusiva, storage e mapping
vivi e mutabili per tutta la durata di API e puntatori restituiti.

Transizioni non eseguibili:

    alloc: FREE -> INITIALIZING -> LIVE -> restituisci puntatore
    free:  LIVE -> RETIRING -> FREE

Le transizioni e la contabilità avvengono sotto Gate; azzeramento e poison
avvengono senza Gate, sulla memoria riservata al chiamante dell'operazione.
Rilasciare il guard ripristina IF precedente: se il chiamante aveva IF=0,
resta IF=0. Non è una garanzia di preemption o di IRQ attivi per ogni caller.

Le sequenze di pagine hanno Kind dedicati per gli stati intermedi; le code
di sequenza non diventano puntatori validi per free. Per gli slot slab una
bitmap Busy distinta dalla bitmap Used mantiene riservati quelli in fase
intermedia (circa 8 KiB aggiuntivi per heap). Una pagina di slab si restituisce
solo quando nessuno slot è allocato o riservato. Nessuna lettura concorrente
del canary/byte di una risorsa Busy; free concorrente sullo stesso oggetto
in fase intermedia viene rifiutato.

Objects include le prenotazioni iniziali e i rilasci in corso fino al commit;
un contatore Pending distingue queste operazioni. FreePages non include
pagine riservate. Stats conserva FreePages+RunPages+SlabPages=Pages anche
con operazioni in volo. I nomi effettivi dell'interfaccia verranno definiti
nell'implementazione dopo la scelta; una query non restituisce puntatori
interni e non prolunga il lifetime di un'allocazione.

Se una fase finale non può confermare la propria prenotazione, fail-stop
con risorsa trattenuta: non restituire memoria parzialmente azzerata e non
rendere riusabile una pagina mentre il poison sta ancora scrivendo. Nessun
rollback su page fault del mapping: mapping stabile è una precondizione;
non introdurre fault recovery o unmapping concorrente in questo blocco.

Il puntatore pubblico resta raw: il chiamante serializza uso e free e non
accede dopo free. Questa modifica non promette di distinguere un puntatore
stale da una nuova allocazione allo stesso indirizzo, né rende sicuro un
puntatore arbitrario o un caller ostile in ring 0.

Costo: stati intermedi e test più ampi; sezioni critiche senza fill di
interi blocchi. Le scansioni metadati restano finite, non O(1) per decreto.
Nessun binding clock nativo aggiunto implicitamente: BSP non conteso oggi,
clock di attesa modellato per thread host; supporto AP/clock per CPU in K3.

## B alternativa: intera operazione sotto irqsave

Un solo guard copre metadati, canary, azzeramento e poison. Minore numero
di stati e diff più piccolo; mantiene IRQ disabilitati anche per i blocchi
massimi. Richiede misure della durata e non può essere presentato come
soluzione di bassa latenza senza evidenza. Nessuna cache per CPU in entrambi.

## Accettazione dopo la scelta

- Unit test positivi, overflow, limiti indici, allineamento, alias,
  contatori, canary, double free e conservazione IF; nessuna ricorsione.
- Contesa host 2/4/8 thread con vere atomiche e contesti CPU distinti;
  claim e payload indipendenti per rilevare sovrapposizioni o riuso prematuro.
- Per A: sospendere il fill con un test deterministico esterno alla sezione
  critica; provare che altri oggetti progrediscono, il blocco Busy non viene
  riusato e un secondo free viene rifiutato, poi commit e budget ripristinato.
- ASan, accounting e analisi statica; strumentazione e mock distinti.
- Adattatori necessari e almeno dieci boot consecutivi dell'immagine finale,
  senza fault inattesi e con marker/contabilità pertinenti verificati.
- Review diff, massimo 400 righe per sorgente e cinque file funzionali;
  nessuna attestazione SMP guest o K2 completo prima dei gate applicabili.
