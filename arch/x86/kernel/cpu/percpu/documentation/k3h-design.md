# K3-H — root inattiva per l'ingresso AP

30 ottobre 2026. Scelta A approvata: root condivisa, avvio sequenziale.
Implementazione e limiti in [validazione del checkpoint](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu). Segue la
[riserva K3-G](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu); serve a completare in seguito il
[gate SMP K3-F](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu). Nessun AP viene avviato in questo blocco.

## Fonti e vincoli verificati

[Intel SDM Vol.3A, 253668-078US, dicembre 2022](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§2.5, registri di controllo a 32 bit nei modi iniziali; §4.1.2,
transizioni del paging; §4.5.2 e Tabella 4-12, root PML4 e CR3;
§4.5.4, cammino a quattro livelli; §10.8.5, preparazione IA-32e,
strutture iniziali sotto 4 GiB e codice identità quando si abilita paging.
Non assumere che il CR3 operativo sia caricabile dal modo protetto.
Il limite di 1 MiB riguarda l'ingresso SIPI, non tutte le pagine del kernel.

[Linux v6.12, realmode/init.c](https://github.com/torvalds/linux/blob/v6.12/arch/x86/realmode/init.c),
setup_real_mode: root separata con ramo identità e riferimenti alle mappe
kernel. [trampoline_64.S](https://github.com/torvalds/linux/blob/v6.12/arch/x86/realmode/rm/trampoline_64.S),
startup_32/startup_64: cambio di modalità prima dell'ingresso ordinario.
GPL-2.0, analisi concettuale senza copia. Non importiamo IDT nulla,
attese illimitate o istruzioni del riferimento come requisito locale.

Il progetto corrente mantiene vuote le voci PML4 0–255 nel CR3 operativo.
K3-G possiede una pagina codice bassa pinned, ancora non scritta e senza
mapping. L'arena K3-D/E contiene già stack e descrittori AP nella metà alta.

## Decisione approvata

A, approvata: una root di transizione condivisa, AP avviati uno per
volta. Quattro nuove pagine per PML4/PDPT/PD/PT e una pagina separata per
i futuri dati/stack iniziali. Circa 20 KiB aggiuntivi, oltre alla pagina
codice già riservata. Il primo AP deve lasciare la fase di transizione
prima che il BSP riutilizzi i dati iniziali per il successivo.

B: root e dati iniziali privati per AP. Circa 20 KiB per AP, oltre al
codice condiviso e alle risorse K3-D/E già esistenti. Riduce la condivisione
dei dati iniziali e facilita un futuro avvio parallelo, ma richiede più
lease, selezione dei contesti e accounting. Da sola non implementa un
protocollo di avvio parallelo o di recupero dopo timeout.

## Contratto v0 proposto per A

1. La pagina dati bassa viene prenotata subito dopo la pagina codice,
   prima delle allocazioni generiche. È distinta, pinned e sotto 1 MiB:
   semplifica l'accesso iniziale e la separazione tra codice e stack.
   Serve una primitiva fisica con limiti espliciti; nessun allocate-and-hope
   né allocazione oltre il limite seguita da tentativi non sorvegliati.
2. Dopo la disponibilità della finestra di pagina, quattro frame contigui
   sotto 4 GiB vengono allocati e pinned per le tabelle. Questo limite
   sull'intero gruppo è una scelta conservativa del profilo v0. Le mappe
   kernel condivise vengono usate soltanto dopo l'attivazione IA-32e.
3. Root nuova: ramo basso privato per le sole due pagine codice/dati,
   riferimenti alle mappe kernel alte già verificate, slot ricorsivo 510
   assente. Nessuna copia indiscriminata del ramo basso o del ricorsivo.
   Gli indici virtuali vengono dal layout esistente.
4. Foglia codice identità supervisor RO/eseguibile; foglia dati supervisor
   RW/NX, pagina zero assente. Nessuna pagina grande che esponga RAM vicina.
   Permessi e cache coerenti col PAT validato; NX richiede NXE prima dell'uso.
   Sono permessi di tabelle inattive: non si scrive ancora nel codice SIPI.
5. C possiede pagine, lease e durata. Rust valida geometria, limiti,
   maschere, permessi e contenuto atteso. Scritture fisiche attraverso la
   finestra posseduta, con bounds e chiusura verificata dopo ogni prestito.
6. Il BSP non carica la root di transizione. Il suo CR3 e le 256 voci basse
   rimangono invariati. Nessuna modifica alle tabelle kernel condivise;
   solo nuovi frame privati. Nessun nuovo registro hardware in questo blocco.
7. Fallimento prima del commit: ritirare soltanto le risorse del tentativo
   e verificare il budget. La pagina codice K3-G resta di proprietà del
   suo modulo, senza trasferimenti o doppi rilasci impliciti. Se mancano
   frame idonei, niente avvio AP; il profilo BSP può proseguire.
8. Prima del primo futuro tentativo di avvio, sigillare codice, dati,
   tabelle e riferimenti condivisi. Un timeout non autorizza rilascio,
   riscrittura dei dati per un altro AP o riuso delle pagine: l'AP tardivo
   potrebbe essere ancora nella fase iniziale. L'avvio sequenziale dovrà
   vietare il successivo tentativo se il precedente non è risolto.

Sequenza concettuale, non eseguibile:

    CODE_RESERVED -> EARLY_DATA_RESERVED -> TABLES_PINNED
                         |                     |
                         +--- rollback --------+
                                               v
                                        PREPARED_INACTIVE
                                               |
                                     future START_COMMITTED

La root non dispone della vista ricorsiva ordinaria: il futuro stub alto
deve caricare il CR3 operativo prima di chiamare servizi del kernel che
la usano. Il passaggio richiederà stack, GDT/IDT e gestione NMI/fault
espliciti; non è autorizzato ad assumere già pronto il panic K3-F quando
CR3 e descrittori sono ancora quelli transitori.

I riferimenti condivisi richiedono un'epoca di preparazione stabile: nel
futuro avvio nessuna mutazione del paging BSP durante la transizione.
Copiare la PML4 non clona le tabelle figlie e non fornisce isolamento.

## Perimetro e prove di accettazione

Modulo per-CPU/transition; soli adattatori a riserva bassa, allocatore
fisico con limiti, bootstrap, finestra di pagina, build e test. Prima di
implementare leggere quegli adattatori per verificare le API reali.
Nessuna revisione generale dei driver, nessun INIT/SIPI o nuovo Assembly.

Test host C/Rust: overflow, limiti 1 MiB/4 GiB, frame distinti, permessi,
alias, root modificata, duplicati e failure injection in ogni acquisizione,
pin e scrittura; budget e owner corretti dopo rollback. ASan sul C.

QEMU: dieci preparazioni consecutive con controllo fisico delle quattro
tabelle, delle due foglie e delle lease; CR3 operativo e metà bassa
immutati. Indisponibilità e rollback separati; dieci boot ordinari e
regressione console pertinente, in sequenza con lo stress host. AP fermi.
Nessuna prova di traduzione su AP vivo viene dedotta da tabelle inattive.

La stesura iniziale era solo documentale. Le prove di implementazione
sono registrate separatamente nel rapporto collegato, distinguendo
immagini intermedie e finali. Le campagne K3-G restano prove storiche.
