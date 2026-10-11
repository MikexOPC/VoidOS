# Ownership delle pagine fisiche

`kernel/mm/physical/` gestisce intervalli liberi e assegnazioni contigue.
Non alloca metadati tramite firmware, non tocca i byte delle pagine
fisiche e non modifica CR3 o tabelle. Una assegnazione e un indirizzo
fisico posseduto, non un puntatore virtuale utilizzabile.

## Mappa, politica e limiti

L'inizializzazione richiede un owner gia riservato e interamente zero,
un buffer mappa immutabile e leggibile, distinto dai metadati, e il numero
di bit fisici CPUID. Il percorso x86 legge la foglia 0x80000008 dopo averne
verificato la disponibilita, preservando RBX secondo ABI. Sono accettati
valori da 32 a 52; i profili con cifratura/key-ID e hardware reale non sono
ancora verificati da questa implementazione.

Prima viene validata tutta la mappa: dimensione, stride, intervalli e
sovrapposizioni. Rust seleziona solo ConventionalMemory con base non
nulla, capacita WB e nessun attributo oltre UC/WC/WT/WB/UCE. Gli intervalli
che superano il limite fisico vengono esclusi interamente, senza clipping.
Runtime, loader, boot services, MMIO, memoria persistente e attributi
sconosciuti restano riservati. La capacita WB non prova un mapping WB
attualmente attivo; la politica paging dovra rispettare tali requisiti.

Sono gestibili al massimo 128 descrittori candidati, 256 intervalli liberi
e 64 assegnazioni contemporanee. Una mappa oltre il limite viene rifiutata
interamente, senza perdere silenziosamente RAM. L'owner occupa 6448 byte
nel profilo corrente e vive nella BSS kernel, gia EfiLoaderData riservata;
un assert limita il costo a 8 KiB. Nessun grosso owner viene costruito sullo
stack e non serve memoria proporzionale al numero totale di pagine.

La selezione avviene in due passaggi prima del ritorno riuscito; gli
intervalli vengono ordinati e quelli adiacenti coalescono. Su un errore
durante la costruzione i metadati parziali vengono azzerati. Ready e
invarianti sono controllati prima di restituire successo.

## Allocazione e rilascio

Il chiamante fornisce un handle distinto e interamente zero.
`kernel_physical_allocate` usa un allineamento minimo di una pagina;
`kernel_physical_allocate_aligned` riceve un allineamento espresso in
pagine, positivo e potenza di due. Per esempio 16 significa 64 KiB,
512 significa 2 MiB. Questo non crea huge page o mapping virtuali.
Rust calcola il primo candidato con moltiplicazioni e somme checked,
entro il limite fisico CPU. La prima regione utilizzabile viene scelta
senza creare alias o usare buchi riservati. Prefisso e suffisso restano
liberi; se entrambi esistono serve un solo record aggiuntivo. A metadati
pieni si cerca un candidato successivo che non richieda tale record.
Il record contiene base, numero di pagine,
identificatore monotono e identita dell'owner. L'owner e non spostabile
e non deve essere copiato, azzerato o reinizializzato mentre e in uso.

Il rilascio richiede una corrispondenza esatta con un record vivo. Dopo
successo il record e l'handle fornito vengono azzerati, il conteggio libero
viene ripristinato e i vicini riuniti prima di inserire un nuovo record.
Questo permette il rilascio adiacente anche con 256 extent occupati;
un rilascio isolato senza spazio nei metadati fallisce senza modifiche.
Un doppio rilascio o una copia
obsoleta falliscono anche dopo riuso dello stesso indirizzo: l'ID cambia.
L'esaurimento degli ID fallisce prima del wrap. Se esistono copie dello
stesso handle vivo, la prima puo rilasciarlo e rende obsolete le altre;
questo e un protocollo fra chiamanti fidati, non autenticazione o isolamento.

Prima di ogni mutazione si verificano limiti, ordinamento, assenza di
sovrapposizioni fra liberi e assegnati, ID distinti e somma esatta delle
pagine. OOM, frammentazione contigua, record esauriti, output non vuoto,
alias o handle rifiutati non modificano owner o output. I buffer passati
alle API C devono essere oggetti accessibili posseduti dal chiamante.

Il codice e per bootstrap su un solo processore, con un solo chiamante,
senza concorrenza, SMP o rientro da interrupt. Le verifiche complete hanno
costo quadratico sui piccoli budget fissati; non e un allocatore ottimizzato
per carichi gaming o un sostituto dell'heap. Non rilascia ancora risorse
del loader e non offre azzeramento, mappature, guardie o protezione DMA.

## Prove

I test host controllano coalescenza, rifiuti e contabilita, 64 record
esauriti, tutte le pagine esaurite, frammentazione, intervalli oltre CPU,
attributi esclusi e 129 regioni candidate rifiutate. Eseguono 1024 sequenze
con un modello indipendente di indirizzi e conteggi. Rust verifica anche
estremi CPU, requisiti WB e ogni bit degli attributi speciali.
La suite di allineamento aggiunge 8192 operazioni miste con una bitmap
indipendente di 128 pagine, confrontando first fit, budget e intervalli
liberi dopo ogni operazione. Controlla split, pin, handle obsoleti,
overflow, alias, output non vuoti e rifiuti con fotografia byte per byte
dell'owner. Fixture sintetiche valide con 256 extent esercitano i limiti
dei metadati; non estendono la politica di acquisizione dalla mappa UEFI.
Due test Rust aggiungono 278528 confronti con un modello enumerativo
e i limiti fisici da 32 a 52 bit, senza puntatori o heap in produzione.

In QEMU il kernel assegna due blocchi, libera e riusa il primo, respinge
un handle obsoleto, rilascia entrambi e verifica il ripristino di tutte le
pagine prima di KERNEL_PHYSICAL_OK. Non scrive a quegli indirizzi fisici.
Assegna anche 4 pagine allineate a 64 KiB e 8 pagine allineate a 2 MiB,
controlla disgiunzione e budget, poi le rilascia entrambe prima di
KERNEL_PHYSICAL_ALIGNED_OK. Lo script controlla le basi stampate in
tutti i 18 scenari, inclusi 128/256 MiB, panic ed eccezioni terminali.

```powershell
.\kernel\scripts\build.ps1 -Install
.\test\kernel\host.ps1
.\test\kernel\image.ps1
.\test\kernel\firmware.ps1 -UseInstalled
```

Il mapper puo bloccare una assegnazione con un pin esclusivo. Il rilascio
fisico respinge handle bloccati; l'identita del pin si puo verificare
senza modifiche. Il mapper sblocca solo dopo unmap e invalidazione TLB.
La [finestra iniziale](page-window.md) aggiunge permessi RW/NX e azzeramento;
l'allocatore fisico da solo continua a non toccare i byte delle pagine.

Riferimenti: [UEFI 2.11, GetMemoryMap](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel CPUID, dimensione fisica](https://cdrdv2-public.intel.com/774475/252046-sdm-change-document.pdf).
L'aritmetica scalare usa [Rust core u64](https://doc.rust-lang.org/stable/core/primitive.u64.html).
