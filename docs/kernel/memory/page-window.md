# Finestra virtuale limitata del kernel

`arch/x86/mm/window/` collega da 1 a 16 pagine contigue dell'allocatore
fisico a una finestra virtuale fissa. Non e ancora un gestore generale dello spazio
virtuale o un heap. Il codice comune di ownership resta in
`kernel/mm/physical/`; la selezione delle regioni firmware resta in Rust.
`rust/layout/validation.rs` verifica dimensione, allineamento e fine del
blocco fisico con interi e aritmetica controllata, senza blocchi unsafe.

## Geometria e permessi

Il loader riserva lo slot PML4 128, cioe l'intervallo da
`0x0000400000000000` a `0x0000408000000000` escluso. La voce punta alla
root stessa con Present/RW, supervisor e NX. Questo rende accessibili
le tabelle attraverso una vista ricorsiva senza un direct map della RAM.
Il verificatore del loader ammette esclusivamente questo ciclo esatto;
gli altri cicli, rami condivisi e modifiche ai permessi restano errori.

La finestra dati parte da `0x0000700000020000` e ha capacita di 64 KiB.
Le guardie a `0x000070000001f000` e `0x0000700000030000` restano assenti.
Per assegnazioni piu piccole anche la coda inutilizzata resta assente:
la pagina immediatamente successiva al blocco non e accessibile.
Il piano del loader vieta segmenti in tutti questi
intervalli. La finestra riusa la PT dello stack, senza allocare altre
tabelle. Prima di seguire ogni livello il mapper verifica il genitore,
la root CR3 effettiva e i limiti fisici. Accetta i bit Accessed/Dirty
impostati dalla CPU; rifiuta huge page e permessi inattesi.

## Durata del blocco

L'owner della finestra deve essere zero, distinto dai metadati fisici e
non spostabile. Esiste una sola finestra attiva e un solo chiamante.

1. Valida il conteggio 1-16 prima di allocare, poi assegna un unico blocco
   contiguo da ConventionalMemory ammessa dalla politica Rust.
2. Blocca il rilascio del suo handle nell'allocatore fisico.
3. Pubblica tutte le PTE supervisor RW/NX e invalida ogni pagina.
4. Azzera tutti i byte assegnati e ne verifica il contenuto prima del successo.
5. Al rilascio verifica handle, pin, tutte le PTE e la coda inutilizzata;
   rimuove tutte le PTE e invalida ogni pagina.
6. Solo dopo l'invalidazione sblocca e restituisce il blocco all'allocatore.

Un handle alterato o una PTE inattesa vengono respinti prima di cambiare
la mappatura, anche se l'errore e nell'ultima foglia. Un blocco mappato
non puo essere liberato tramite il
rilascio fisico ordinario. I puntatori alla finestra diventano invalidi
dopo il rilascio: il chiamante non deve conservarli come riferimenti vivi.
Il pin e un protocollo fra componenti kernel fidati, non una barriera
contro codice ring 0 che modifica direttamente i metadati o le tabelle.

## Verifiche e limiti

I test host verificano tutte le dimensioni 1-16, 256 cicli di crescita e
riduzione, l'ordine pin/PTE/INVLPG/rilascio, una invalidazione per pagina,
il riuso azzerato e l'assenza di scritture fuori dal blocco,
copie dell'owner, handle alterati, OOM, parent con User/PS, guardie e
finestra occupata. Il backend host sostituisce solo gli accessi hardware.
I test del loader mutano anche la voce ricorsiva e tentano segmenti
nella prima/ultima pagina della finestra e nelle sue guardie. Rust
controlla ogni larghezza fisica 32-52 e confronta 8192 casi con un modello u128.

In QEMU il kernel scrive entrambi gli estremi di ogni pagina del blocco,
verifica il rilascio fisico bloccato, rimuove la mappatura, riusa e riazzera.
Quattro immagini dedicate tentano l'esecuzione dell'ultima pagina NX,
la sua lettura dopo unmap e le letture delle guardie inferiore/superiore:
devono produrre #PF, CR2 sull'indirizzo atteso e codici 17 oppure 0.
Il test verifica anche CR3, ExitBootServices riuscito e lo stack IST.
Queste immagini non possono essere installate con `-Install`.
QEMU salva anche trace `int,cpu_reset` per diagnosticare triple fault;
i reset iniziali del firmware non sono considerati fault del kernel.

```powershell
.\arch\x86\boot\uefi\scripts\build.ps1 -BootKernel
.\kernel\scripts\build.ps1 -Install
.\test\kernel\host.ps1
.\test\kernel\image.ps1
.\test\kernel\firmware.ps1 -UseInstalled
```

Loader e kernel devono essere ricostruiti insieme: il vecchio loader
non riserva tutta la finestra richiesta da questo profilo. Il formato
StartInfo v2 resta invariato; il layout virtuale e un accordo statico.
Il profilo ha un solo processore, interrupt disabilitati, niente PCID o
Global nelle nuove PTE. INVLPG e locale: SMP richiedera sincronizzazione
e invalidazioni sugli altri processori. Non vengono reclamate risorse
UEFI, create mappature eseguibili, isolate periferiche tramite IOMMU o
verificati hardware reale, PAT personalizzato e memoria cifrata.
Non si gestiscono frame non contigui, piu finestre simultanee o recupero
da fault hardware durante l'azzeramento. OOM e input rifiutati prima della
pubblicazione non lasciano PTE parziali; il codice ring 0 resta fidato.

Riferimento: [Intel SDM Volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
capitoli 4.5 e 4.10.4: traduzione a quattro livelli e invalidazione TLB.
