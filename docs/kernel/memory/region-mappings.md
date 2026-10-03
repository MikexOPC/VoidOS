# Mapping simultanei del kernel

`arch/x86/mm/regions/` estende il bootstrap con quattro slot indipendenti.
Ogni slot offre fino a 32 pagine contigue, 128 KiB, RW/NX supervisor,
con una guardia inferiore e una superiore; la coda non usata resta
assente. I mapping possono rimanere vivi contemporaneamente e vengono
rilasciati separatamente. Non e ancora un mapper virtuale generale.

## Geometria e proprieta

L'arena inizia con la guardia `0x700000080000`, oltre stack principale,
IST e finestra temporanea. Ogni slot occupa 34 pagine virtuali; i byte
utilizzabili iniziano una pagina dopo la guardia. Gli slot usano PTE
128..263 dello stesso PT ereditato. Non si allocano nuove tabelle,
non si cambia CR3 e non si adottano PTE preesistenti. Gli assert C
vincolano il profilo condiviso con Rust, che valida basi, handle e frame.

Il mapper occupa 192 byte e vive nella BSS; l'handle occupa 32 byte.
Owner e allocatore fisico sono non spostabili e devono restare vivi.
I buffer di metadati e gli handle devono essere accessibili, distinti
e fuori dall'arena che il mapper gestisce. I controlli numerici non
rendono accessibile un puntatore arbitrario fornito da codice non fidato.

Ogni mapping usa una lease fisica distinta, allineata come richiesto
in pagine potenza di due, e un pin esclusivo. L'handle conserva owner,
ID della lease, base virtuale e pagine. Copie obsolete vengono respinte
anche quando lo stesso slot virtuale viene riusato: l'ID fisico cambia.
Questa e ownership fra chiamanti fidati, non un sistema di capability
per processi o autenticazione crittografica degli handle.

## Sequenza e rilascio

Initialize verifica owner vergine, allocatore, CR3, ricorsione, genitori
e tutte le 136 PTE vuote. Acquire controlla output vuoto e slot libero,
assegna e blocca la lease, pubblica le PTE, invalida ogni pagina locale,
azzera e verifica i byte, poi pubblica l'handle. Solo A/D sono tollerati
nelle PTE presenti; guardie e code devono essere esattamente zero.

Release verifica tutte le lease e tutte le PTE dell'arena, l'handle
esatto e la capacita di restituire i frame. Cancella il batch, esegue
INVLPG per ogni pagina, poi rimuove il pin e restituisce la lease.
Il rilascio azzera l'handle fornito; altre copie diventano obsolete.
Detach azzera il mapper soltanto quando non esistono piu mapping vivi.
Gli ID dell'allocatore fisico continuano a crescere dopo reinitialize.

Bind registra un borrower esclusivo, identificato dai suoi metadati;
release respinge anche copie valide dell'handle mentre il binding e vivo.
Unbind richiede la stessa identita. Il [pool persistente](persistent-objects.md)
usa questo vincolo per impedire l'unmap del buffer dei suoi oggetti.
Si tratta di un protocollo fra componenti fidati, non autenticazione:
la conoscenza dell'identita non e un permesso per codice non fidato.

Il puntatore restituito da data e un prestito valido fino al rilascio.
Il chiamante deve terminare tutti gli accessi e scartarlo prima di
release: C non impedisce l'uso successivo di una copia del puntatore.
Le guardie proteggono confini degli slot, non ciascun oggetto interno.

Un backend di azzeramento assente provoca unmap, invalidazione e
restituzione della lease; l'output resta vuoto, il budget torna integro,
ma l'ID gia consumato non viene riavvolto. Se dopo la scrittura emergono
invarianti inattese, il pin viene conservato invece di liberare pagine
con mapping non verificati. Il percorso reale arresta il bootstrap
su errore; non tenta recupero automatico da corruzione o fault terminali.

## Uso attuale e limiti

Il kernel mantiene due blocchi da 128 e 64 KiB: 48 pagine, 192 KiB.
Prima prova scrittura agli estremi di ogni pagina, rilascio e riuso
azzerato del primo, byte del vicino intatti e saturazione dei quattro
slot con due blocchi sacrificabili da una pagina. Questi ultimi vengono
rilasciati; il budget finale deve essere esattamente iniziale meno 48.
KERNEL_REGIONS_OK arriva dopo tutte le verifiche, prima di GDT e stack.

Il profilo e UP, CPL0 e IF=0, con NXE/WP abilitati, pagine da 4 KiB e
PCID/PGE/LA57 disabilitati come predisposto dal loader. Non ci sono
concorrenza, shootdown SMP, huge page, conversioni dei permessi,
mapping MMIO, heap dinamico o reclamazione delle risorse loader.
Per estendere il ramo PT servira ownership esplicita delle nuove tabelle.

## Prove

La suite host verifica quattro mapping simultanei, pin e ordinamento
unmap/INVLPG/unpin/free, saturazione, riuso, alias, handle modificati,
copie obsolete, detach, rollback e 128 cicli. Confronta 8640 alterazioni
non A/D sulle PTE e controlla i flag invalidi dei genitori.
Rust verifica geometria e limiti fisici con soli scalari, senza heap
o puntatori in produzione. Il monitor QEMU legge tutte le 136 PTE;
le immagini dedicate provano unmap, entrambe le guardie e NX reali.

Riferimento: [Intel SDM 3A, 5.10.4, invalidazione delle traduzioni](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf).
