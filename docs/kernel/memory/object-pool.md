# Pool iniziale di oggetti kernel

`kernel/mm/objects/` assegna oggetti dentro un buffer virtuale gia
mappato, con un massimo di 64 oggetti vivi e 64 KiB di capacita.
Non alloca pagine, non usa servizi UEFI e non modifica il paging.
Il kernel lo collega alla finestra RW/NX gia verificata.

## Contratto e budget

L'owner deve essere inizialmente zero, accessibile, non spostabile e
distinto dal buffer. Il chiamante presta in esclusiva un buffer mappato,
allineato a 4 KiB e lungo da 4 a 64 KiB, in multipli di pagina.
Il buffer deve restare accessibile e stabile finche il pool e collegato;
non deve appartenere contemporaneamente a un altro pool.

I metadati sono esterni agli oggetti e occupano 2096 byte nel profilo
attuale; un assert fissa il massimo a 3 KiB. Nessun header viene inserito
prima dei dati dell'oggetto. Sono conservati offset, byte richiesti,
allineamento e ID; i record sono ordinati e i gap restano riutilizzabili.
Used conta i byte vivi, non la capacita del maggiore gap contiguo.

Il pool opera su un solo chiamante, senza concorrenza o rientro da
interrupt. Le verifiche complete sono quadratiche sui 64 record e non
sono ottimizzate o misurate per carichi gaming. E un modulo di bootstrap,
non un heap generale, uno slab allocator o un'interfaccia malloc/free.

## Allocazione, accesso e rilascio

`kernel_object_allocate` richiede byte non nulli e un allineamento
potenza di due da 1 a 4096. Rust verifica la richiesta e calcola il
primo offset allineato in un gap, con aritmetica controllata. I byte
dell'oggetto vengono azzerati prima di pubblicare record e handle.
OOM, frammentazione, record esauriti o input invalidi non pubblicano
allocazioni parziali e non consumano un ID.

Il chiamante conserva l'handle fuori dal buffer e dai metadati del pool.
L'handle contiene offset, dimensione, ID e identita dell'owner; non e
un puntatore. `kernel_object_data` confronta tutti questi campi con un
record vivo prima di fornire un prestito ai dati. Il prestito vale per
handle.Bytes byte e termina quando l'oggetto viene rilasciato.

`kernel_object_release` respinge copie obsolete, campi alterati e doppi
rilasci. Azzera i byte prima di restituire il gap, rimuove il record e
azzera l'handle fornito. Il chiamante deve annullare i puntatori presi
in prestito e non conservarli dopo il rilascio. Una copia ancora viva
puo rilasciare l'oggetto e rende obsolete tutte le altre copie.

Prima delle mutazioni sono verificati limiti, ordinamento, assenza di
sovrapposizioni, ID distinti e somma dei byte vivi; i record inutilizzati
devono essere zero. Rust riceve solo interi tramite FFI e non dereferenzia
puntatori; nel validatore non ci sono blocchi unsafe o allocazioni.

## Scollegamento e riapertura

`kernel_object_detach` fallisce se esistono oggetti vivi. Quando riesce
annulla Arena e Ready senza toccare il buffer o liberare le pagine.
Solo dopo questo passo il chiamante puo rimuovere il mapping e restituire
la lease fisica. L'owner mantiene Self e NextId: una nuova initialize
sullo stesso owner preserva gli ID, respingendo gli handle della vecchia
apertura. Il contatore non viene mai riavvolto; l'esaurimento fallisce
prima del wrap, anche dopo detach/initialize.

L'owner non va copiato o azzerato per ricominciare mentre possono esistere
vecchi handle. Il protocollo opera fra componenti ring 0 fidati: non
impedisce scritture tramite puntatori grezzi obsoleti o unmap eseguiti
direttamente dal chiamante durante un prestito. Mancano isolamento per
oggetto, crescita automatica, gestione di distruttori C++ e recupero
da fault hardware durante l'azzeramento.

## Verifica

I test C coprono tutti gli allineamenti ammessi, byte/record esauriti,
frammentazione, riuso azzerato, handle alterati, owner copiato, alias,
record corrotti, sovrapposizioni e ID preservati dopo riapertura.
Un modello bitmap indipendente esegue 1024 operazioni e verifica anche
contenuto degli altri oggetti vivi e guardie del buffer.
Tre test Rust coprono richieste errate, estremi dei gap e 8192 confronti
di allineamento con un modello u128.

In QEMU il kernel assegna oggetti di 128, 4097 e 96 byte. L'oggetto da
4097 byte attraversa due pagine e resta integro durante il riuso di un
vicino. Vengono provati detach prematuro, handle obsoleto, riapertura,
rilascio di tutti gli oggetti, unmap e ripristino del budget fisico.
Solo allora viene stampato KERNEL_OBJECTS_OK.

```powershell
.\kernel\scripts\build.ps1 -Install
.\test\kernel\host.ps1
.\test\kernel\image.ps1
.\test\kernel\firmware.ps1 -UseInstalled
```

Serve il bootloader costruito con `-BootKernel` e il profilo della
[finestra da 16 pagine](page-window.md). Gli artefatti rimangono in build/.

Riferimenti consultati: [Rust, aritmetica u64](https://doc.rust-lang.org/stable/core/primitive.u64.html)
e [Linux, pool generici](https://www.kernel.org/doc/html/latest/core-api/genalloc.html).
La nostra implementazione usa record e handle, non riproduce gen_pool.
