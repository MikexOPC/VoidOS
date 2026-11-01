# K1, tappa 5b: revoca dell'identità del loader

Scelta utente: proseguire K1 dalla tappa 5b. Perimetro: paging/retirement;
adattatori strettamente necessari a GDT/TSS, IDT, startup, build e test.
Nessuna modifica all'ABI del boot o recupero di RAM del loader.

## Contratto

Il BSP esegue la revoca una sola volta, con IF=0, dopo avere installato
GDT/TSS/IDT, IST e stack principale del kernel. Gli AP non sono avviati.
Non è un'API di unmap utilizzabile dopo SMP.

Il profilo accetta quattro livelli, CR0.PG/WP ed EFER.LME/LMA/NXE attivi,
CR4.PAE attivo e PGE/PCIDE/LA57 disattivati. Il loader azzera già PGE prima
del proprio cambio di CR3. I validatori di paging escludono le foglie globali.
GDTR e IDTR devono descrivere gli oggetti kernel conosciuti; TR seleziona il
TSS busy del kernel, con RSP0/IST posseduti. SLDT deve restituire zero.
RIP e stack devono appartenere agli intervalli alti previsti.

Il validatore completo esistente acquisisce l'inventario. Sotto 256 è
ammesso solo lo slot 0, contenente una pagina identità RX della capsula,
nove pagine RW/NX dei descrittori/stack e i due buchi di guardia attesi.
La capsula deve provenire da EfiLoaderCode; dati e guardie da EfiLoaderData,
con capacità WB e attributi del profilo corrente. Nessuna pagina può essere
in un extent libero o in un lease live dell'allocatore kernel.

## Transazione, pseudocodice non eseguibile

1. Verificare CPU, descrittori, inventario e forma del ramo basso.
2. Salvare voci PML4 alte, budget e generazione.
3. Ricontrollare IF/CR3/CR4 e voce PML4[0]; se differiscono, rifiutare.
4. Azzerare PML4[0], ricaricare lo stesso CR3 e avanzare la generazione.
5. Verificare metà bassa vuota, voci alte invariate salvo A/D, descrittori,
   budget e inventario ridotto esattamente delle tabelle/foglie rimosse.

Un errore prima del commit non modifica il mapping. Un errore dopo il
commit ferma il bootstrap: nessun rollback cieco né rilascio di pagine.
Le tabelle diventate irraggiungibili e tutte le pagine del loader restano
riservate. Recuperarle richiederà un distinto contratto di ownership.

## Fonti hardware

[Intel SDM Vol. 3A, 253668-090US, febbraio 2026](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):

- §2.4.1–2.4.4: GDTR, LDTR, IDTR e TR; STR restituisce il selettore, non
  la base nascosta del TSS. La verifica indipendente QEMU legge anche tale base.
- §2.5: CR0.PG bit 31, WP bit 16; CR4.PAE bit 5, PGE bit 7,
  LA57 bit 12 e PCIDE bit 17.
- §5.10.4.1–5.10.4.2: invalidazione dopo modifiche alle tabelle; con questo
  profilo non globale e PCID disabilitato, MOV CR3 invalida le traduzioni
  e le cache delle strutture di paging interessate.

L'Assembly conserva il contratto Microsoft x64, usa solo registri volatili
e lo shadow space del chiamante per SGDT/SIDT.

## Accettazione e limiti

- Host: policy CPU, tutte le voci basse inattese, ownership, pre/post failure,
  rifiuto del secondo utilizzo; C reale e matematica Rust, commit simulato.
- ASan sul percorso host; nessuna allocazione dinamica nel nuovo modulo.
  Non equivale a misurare tutti i leak fisici del kernel.
- Dieci boot consecutivi con walk fisico indipendente della PML4, GDT/IDT/TR.
- Profilo negativo isolato: leggere la capsula prima della revoca, rileggerla
  dopo MOV CR3 e ottenere #PF, error=0 e CR2 uguale alla vecchia capsula.
- Regressioni eccezioni/stack, console VirtIO/VT-d e suite host.

Non introduce user mode, KPTI, SMP o TLB shootdown. Questi richiedono i
blocchi successivi; la metà bassa vuota ne è soltanto un prerequisito.
