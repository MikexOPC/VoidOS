# Oggetti persistenti nel kernel

`arch/x86/mm/arena/` collega il pool comune di oggetti a un mapping
kernel da 64 KiB. Il componente conserva un prestito esclusivo del
mapping: questo impedisce di rilasciare i frame mentre il pool usa
il buffer. Il pool comune mantiene i validatori Rust gia esistenti
per dimensioni, allineamenti, offset e overflow; non vengono duplicati.

## Proprietari e durata di vita

Il chiamante possiede il mapper e l'handle del mapping. L'arena copia
l'handle e registra la propria identita come borrower. Il suo owner
occupa 2152 byte e vive nella BSS, fuori dai buffer che gestisce.
Il mapper aggiunge quattro identita da 8 byte: passa da 160 a 192 byte.
Owner, mapper e allocatore fisico devono restare vivi e non spostabili.
Tutte le API richiedono oggetti accessibili posseduti dal chiamante;
la verifica numerica dei puntatori non rende accessibile memoria ignota.

Attach accetta esattamente 16 pagine vive, verifica geometria e alias,
registra il prestito e inizializza il pool sul buffer corrispondente.
Se l'inizializzazione del pool fallisce, il binding viene annullato.
Un secondo pool sullo stesso mapping viene respinto. Unmap e detach
del mapper falliscono mentre il pool resta collegato, anche senza
oggetti o quando il chiamante usa una copia dell'handle del mapping.

Prima di ogni operazione, il wrapper confronta handle del mapping,
borrower, PTE, pin e puntatore reale del buffer con il pool. Gli output
devono essere distinti da owner, mapper e metadati fisici. Allocate
verifica anche tutti i byte azzerati prima di restituire successo.
Data offre un prestito dell'oggetto valido fino al suo rilascio.

Il chiamante deve scartare ogni puntatore prima di release: C non
impedisce l'uso di una copia conservata. Gli handle alterati o obsoleti
vengono respinti. Release azzera solo i byte dell'oggetto e rende il
gap riusabile, senza consumare o restituire frame fisici.

Detach richiede zero oggetti vivi, scollega il buffer del pool e solo
poi termina il binding. Conserva l'ID successivo del pool: alla
riapertura gli handle di vecchi oggetti rimangono obsoleti. Il mapping
resta del chiamante, che puo rilasciarlo dopo detach. Alterazioni
inattese dei metadati vengono respinte senza accessi ai byte del pool;
non viene tentato recupero automatico da corruzione.

## Uso nel bootstrap

Il secondo blocco persistente gia esistente ospita due oggetti:

- Contesto di boot: 120 byte, allineamento 16, incluso il prestito
  immutabile alla fotografia UEFI, che rimane riservata al loader.
- Inventario di memoria: 24 byte, allineamento 8.

Un oggetto sacrificabile da 4097 byte allineato a pagina prova
azzeramento, scrittura, rilascio e riuso. Dopo il suo rilascio restano
due oggetti, 144 byte complessivi. Il budget fisico e invariato rispetto
ai due mapping da 192 KiB complessivi: non si allocano ulteriori frame.
KERNEL_ARENA_OK arriva dopo queste verifiche. Dopo il cambio allo stack
autonomo e la revoca dello stack loader, il kernel legge i due oggetti,
valida contratto e inventario ed emette KERNEL_PERSISTENT_STATE_OK.

## Prove e limiti

I test host coprono il binding esclusivo, rifiuto dell'unmap, owner
copiato, alias, OOM di byte e record, 256 cicli di allocazione, 64
riaperture, 256 alterazioni degli handle, buffer discordanti e
rollback con un backend volutamente non allineato. Le fixture dati
sono allineate a 4096 byte come i mapping reali. La regressione Rust
mantiene 23 test, inclusi i validatori usati dal pool.

QEMU verifica dati nella seconda regione, firma e versione del
contratto, allineamenti, disgiunzione e 144 byte di oggetti. Le 22
prove di stack, guardie, NX, NMI e double fault restano attive.
Il profilo resta UP/IF=0, 64 oggetti e 64 KiB massimi: non e un heap
generale, uno scheduler o un'implementazione dell'isolamento ibrido.
Il contesto e in memoria RW/NX del kernel, senza protezione hardware
read-only. Non sono ancora prove su hardware fisico.

Riferimenti: [pool comune](object-pool.md), [mapper](region-mappings.md),
[Rust core u64, aritmetica checked](https://doc.rust-lang.org/stable/core/primitive.u64.html).
