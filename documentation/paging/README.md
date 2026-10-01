# Piano delle mappature e tabelle x86-64

Il piano comprende ora anche la [pagina del contratto di ingresso](../handoff/start-page.md),
in sola lettura e NX nelle nuove tabelle inattive.

## Due blocchi implementati

`paging/plan.c` traduce il contratto del kernel e lo stack ancora posseduti
in intervalli numerici. Il percorso UEFI verifica questo piano dopo la
copertura delle risorse nella mappa firmware. Non modifica le mappature attive.

`paging/tables.c` costruisce PML4, PDPT, PD e PT con pagine da 4 KiB.
Questo modulo viene ora chiamato dall'ingresso UEFI tramite
`paging/preparation.c`. Una nuova mappa verifica le allocazioni dopo la
costruzione. QEMU verifica costruzione e rilascio nel percorso reale;
le tabelle restano inattive e non sono usate dalla MMU.
Il [controllo del contenuto](table-contents.md) confronta tutte le foglie
con il piano e rifiuta cicli, riferimenti estranei e permessi errati.
La [fotografia dei registri attivi](active-state.md) legge CR3 prima e
dopo la costruzione e verifica il modo di paging attualmente utilizzato.
Vedere [integrazione UEFI](uefi-preparation.md) per proprieta e limiti.

## Indirizzi e permessi

Il profilo attuale usa indirizzi virtuali canonici inferiori a 2^47.
I segmenti mantengono gli indirizzi ELF; lo stack usa 64 KiB da
0x0000700000001000. La pagina precedente e riservata e resta assente
dalle tabelle costruite. Un segmento che invade questa area viene rifiutato.
Questi indirizzi sono scelte del progetto, da mantenere coerenti col kernel.

Il piano rifiuta intervalli vuoti, somme fuori limite, pagine condivise,
alias fisici, entry fuori dai segmenti eseguibili e permessi W+X.
La verifica file-backed dell'entry resta responsabilita del parser ELF
e del contratto iniziale. Il piano non sostituisce quei controlli.

Le voci foglia sono supervisor, Present, RW solo per i dati scrivibili,
NX per tutte le pagine non eseguibili. Le voci intermedie sono Present/RW,
supervisor; nessuna huge page, voce globale o alias identity viene aggiunta.
Queste tabelle contengono kernel, stack e contratto: non sono ancora utilizzabili
per il cambio CR3, che richiede anche codice e risorse della transizione.

## Memoria e proprieta

Il piano non possiede memoria ed esige input vivi, immutabili e output
distinto dagli input. Il proprietario BOOT_PAGE_TABLES va inizializzato
a zero, non copiato e non modificato esternamente. Massimo 128 pagine
di tabelle: 512 KiB, allocate singolarmente come EfiLoaderData e azzerate.
Gli indirizzi restituiti da AllocatePages devono essere accessibili
nell'ambiente UEFI x64; questo modulo non e un allocatore post-boot.

Il costruttore richiede larghezza fisica esplicita fra 32 e 52 bit e
supporto NX. Li controlla prima delle allocazioni; ogni indirizzo fisico
deve rientrare nella larghezza dichiarata. I test usano 52 bit per ospitare
gli indirizzi del processo Windows: questo valore non e un rilevamento CPU.

Su errore, Root e Valid rimangono zero ma le pagine gia acquisite restano
nel proprietario. Il chiamante deve sempre eseguire boot_release_page_tables,
anche dopo costruzione fallita. Il rilascio invalida subito Root/Valid,
libera tutte le pagine possibili e conserva quelle il cui FreePages fallisce.
Un nuovo tentativo non libera nuovamente le pagine gia rilasciate.

AllocatePages deve rispettare il contratto firmware: pagine nuove e distinte.
Un indirizzo duplicato o sovrapposto alle risorse viene rifiutato prima
della scrittura; non viene liberato come seconda allocazione, per evitare
di liberare una risorsa gia posseduta. Con firmware non conforme non si
puo garantire recupero completo. Non usare il rilascio con tabelle attive.

## Verifiche e prossimo passaggio

Build con /W4 /WX; test indipendenti leggono i quattro livelli e confrontano
indirizzi e bit dei permessi. Coprono i confini di 2 MiB, 1 GiB e 512 GiB,
pagina zero e guardia assenti, alias identity assente, NX mancante,
indirizzi fisici fuori limite, allocazioni fallite e rilascio ripetuto.
Gli altri test del bootloader vengono eseguiti nella stessa build.
La build esegue anche i [test Rust indipendenti delle allocazioni C](rust-tests.md),
con un modello delle pagine vive e 512 sequenze riproducibili di errori.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

Il [rilevamento CPUID](cpu-capabilities.md), il costruttore e la verifica
delle nuove allocazioni nella mappa firmware sono integrati nel boot.
Prima dell'attivazione
servono transizione, mappa finale/ExitBootServices, gestione eccezioni,
IA32_EFER.NXE, CR0.WP e verifica del modo di paging. Il bit NX e la pagina
di guardia non proteggono ancora lo stack usato oggi dal firmware.
Il [kernel C diagnostico](../handoff/kernel-probe.md) e ora un ELF con
codice compilato; viene caricato in QEMU ma non ancora eseguito.

## Fonte ufficiale

[Intel SDM Volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf):
capitolo 4, paging a quattro livelli e formato delle voci;
sezione 5.13, execute-disable e necessita di IA32_EFER.NXE.
