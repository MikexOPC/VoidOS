# K2-B — proteggere l'allocatore fisico

Contratto approvato il 27 ottobre 2026. I riscontri iniziali sotto descrivono
il codice prima della sincronizzazione. Facade e query sono poi implementate
nel blocco [pool](k2-pool-validation.md); il gate complessivo K2 è verificato
il 29 ottobre: [chiusura](../../../sync/documentation/k2-integration.md).

## Riscontri iniziali prima della sincronizzazione

`kernel_physical_allocate_aligned`, release, pin e is_pinned richiamano
`kernel_physical_validate`. Mettere un lock a ciascun ingresso senza
separare il validatore interno causerebbe riacquisizione dello stesso lock.
Il validatore attraversa Free/Live/Pins e non può leggere mentre un altro
core li modifica. Nel resto del kernel esistono anche letture dirette di
budget e generazioni: bloccare i soli writer non le protegge.

Le operazioni mutanti del pool non chiamano heap, mapping o device: usano
metadati e funzioni numeriche. Il lock del pool può quindi essere una
foglia nell'ordine dei lock; il numero del rango va registrato prima del
codice. Il registro holdings è uno stato distinto, non automaticamente
protetto dal lock di una singola istanza del pool.

## Scelta approvata: contesto del chiamante

**A, scelta dall'utente: accessor architetturale del contesto corrente.** Le API
pubbliche di allocazione mantengono le firme attuali; i wrapper chiedono
il contesto della CPU al backend. In questa fase un binding esplicito del
BSP, verificato prima dell'uso, rifiuta accessi da AP non predisposti.
K3 sostituirà il binding con GS/per-CPU, dietro la stessa interfaccia.
Non è una variabile globale del lock stack condivisa silenziosamente da
tutti i core. Serve un piccolo adattatore di bootstrap prima del pool.

**B: passare il contesto a ogni API della memoria.** L'ownership è esplicita
nelle firme, ma occorre propagare il parametro nei chiamanti, incluso il
percorso bootstrap, paging, heap e driver. Cambiamento più esteso e da
verificare con le stesse garanzie; K3 dovrà comunque ottenere il contesto
per CPU all'ingresso di ogni percorso.

## Implementazione concordata

1. Binding del contesto, senza allocazioni; prima del clock ammette il
   percorso non conteso già discusso in K2-A. Nessun AP viene attivato.
2. Lock per owner, init privata prima della pubblicazione. Wrapper pubblici
   acquisiscono una sola volta; validazione e mutazioni interne lavorano
   con il guard già detenuto. Nessun codice hardware sotto il lock del pool.
3. Letture sincronizzate: snapshot/query per i dati necessari, con copia
   sotto lock e validatore che conosce l'identità dell'owner originale.
   Una query valida seguita da letture dirette non diventa una transazione.
4. Adattare i lettori individuati, preservando pin/lifetime delle risorse:
   uno snapshot coerente non autorizza da solo l'uso di una pagina che
   potrebbe essere rilasciata dopo la copia. Non abilitare SMP lasciando
   lettori plain concorrenti a contatori atomici o a metadati mutabili.
5. Confronti di stato nei test distinguono i contatori del lock dai dati
   dell'allocatore: un'allocazione rifiutata conserva i dati, ma un ingresso
   nel lock può avanzare Next/Serving. Non indebolire rollback/ownership.
6. Test multithread del pool, errori/saturazione/pinning/coalescenza,
   ASan/accounting, test host degli adattatori e dieci boot finali. Poi heap
   come modulo successivo; K2 completo richiede anche quel passaggio.

Perimetro proposto: `kernel/mm/physical/` con il solo binding architetturale,
hook bootstrap, lettori/test e build strettamente necessari. I dettagli
hardware del binding richiedono fonti CPU prima del codice. Nessun nuovo
driver, scheduler, SMP o syscall in questo blocco.

## Lettori del pool: decisione prima dei lock

28 ottobre 2026. La facade e il core sono separati e verificati;
la scelta A seguente è stata approvata dall’utente e implementata.
Le prove del blocco sono in [k2-pool-validation.md](k2-pool-validation.md).

Riscontri: authorization/validation.c, iommu/iommu_registry.c e
gpu/validation.c attraversano Free e Live; regions/mapping.c controlla
lo spazio disponibile per il rilascio prima della revoca. Il primo gruppo
richiede query di range coerenti; il secondo richiede distinguere una
previsione dal rilascio effettivo, che deve ripetere il controllo sotto
lock. Un risultato letto prima della revoca non prenota metadati.

**A, scelta utente: riepiloghi piccoli e query dedicate.** Lock incorporato
per owner, rango foglia 300 (il core non chiama mapping, heap o device).
Init privata prima della pubblicazione; wrapper acquisisce una volta il
contesto CPU con IRQ locali già disabilitati, poi lock; il core richiama
solo la validazione interna. Release prima del ripristino del vecchio IF.
Le query coprono stato/budget/generazione, overlap del range con Free/Live
e disponibilità di metadati per il rilascio. Nessun callback del chiamante
viene eseguito sotto lock, nessun puntatore interno restituito.

Il riepilogo copia solo scalari coerenti, non l'intero owner. Le query
validano numeri, capacità destinazione e alias prima di scrivere; lettura
e controllo avvengono sotto lo stesso lock. Il dato può diventare obsoleto
dopo il ritorno: nessuna query sostituisce pin, ownership o lifetime. I
controlli autoritativi restano nelle operazioni che modificano lo stato.
Il numero 300 ordina solo questo lock; i ranghi di altri moduli saranno
definiti dal loro grafo prima di adottarli, senza assumere heap già protetto.

**B: snapshot integrale dei metadati.** Copia di Free/Live/Pins e contatori
in memoria del chiamante sotto lock, poi scansioni fuori dal lock. Riduce
le API specifiche ma costa circa 6,5 KiB per snapshot e richiede controlli
di lifetime e identità dell'owner originale. Nessuna allocazione dall'heap
mentre si detiene il lock fisico; non imporre snapshot così grandi sugli
stack annidati delle verifiche paging senza un budget dimostrato.

Accettazione: mutazioni e lettori pubblici con guard singolo, errori senza
corruzione dei metadati, IF preservato, callback assenti, contesa host con
contesti per thread e atomiche reali, no overbooking o alias fra lease,
budget ripristinato; poi suite degli adattatori, ASan e dieci boot finali.
I confronti di rollback escluderanno soltanto il lock, non i metadati.
Nessuna dichiarazione SMP finché i lettori diretti non sono migrati.

## Interfacce implementate e limiti

`status` restituisce 56 byte di scalari; range, lease e capacità di rilascio
sono query specifiche. `budget_read/matches/at_least` e `generation_read` leggono i contatori
coerentemente in O(1), verificando TotalPages e il suo limite: non sono
una scansione di integrità. Le altre query validano l’intero pool.
Tutte acquisiscono il medesimo lock foglia 300 e preservano IF.

`release_fits` è una previsione senza prenotazione. Dopo la revoca hardware,
`release_after_unmap` verifica nuovamente lease, pin e capacità sotto un
solo lock e completa unpin + release. Se la capacità manca conserva pin e
lease; il mapper trattiene il proprietario e segnala errore. Non si può
rilasciare una pagina ancora mappata tramite questa API: la revoca è una
precondizione del chiamante, non dimostrata dal solo metadato del pool.

Il chiamante possiede i buffer e ne garantisce mapping, durata e accesso
esclusivo. I controlli numerici e di alias non rendono accettabili puntatori
arbitrari. Init privata e storage mutabile precedono la pubblicazione;
nessuna inizializzazione concorrente o reinizializzazione di owner vivo.
Le query non restituiscono puntatori interni e non sostituiscono il pinning.

Il contatore Generation resta a 32 bit, senza garanzia di monotonicità
illimitata o protezione ABA; non viene usato come capacità di ownership.
Il registro holdings, i mapper e i confronti globali di budget mantengono
il contratto BSP: questo blocco non autorizza lifecycle di device concorrenti.
Il core heap resta da sincronizzare. GS/per-CPU e AP sono lavoro K3.
