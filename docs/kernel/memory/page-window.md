# Prima pagina virtuale del kernel

`arch/x86/mm/window/` collega una pagina dell'allocatore fisico a una
finestra virtuale fissa. Non e ancora un gestore generale dello spazio
virtuale o un heap. Il codice comune di ownership resta in
`kernel/mm/physical/`; la selezione delle regioni firmware resta in Rust.

## Geometria e permessi

Il loader riserva lo slot PML4 128, cioe l'intervallo da
`0x0000400000000000` a `0x0000408000000000` escluso. La voce punta alla
root stessa con Present/RW, supervisor e NX. Questo rende accessibili
le tabelle attraverso una vista ricorsiva senza un direct map della RAM.
Il verificatore del loader ammette esclusivamente questo ciclo esatto;
gli altri cicli, rami condivisi e modifiche ai permessi restano errori.

La pagina dati usa `0x0000700000020000`; le pagine immediatamente prima
e dopo restano assenti. Il piano del loader vieta segmenti in questi
intervalli. La finestra riusa la PT dello stack, senza allocare altre
tabelle. Prima di seguire ogni livello il mapper verifica il genitore,
la root CR3 effettiva e i limiti fisici. Accetta i bit Accessed/Dirty
impostati dalla CPU; rifiuta huge page e permessi inattesi.

## Durata della pagina

L'owner della finestra deve essere zero, distinto dai metadati fisici e
non spostabile. Esiste una sola finestra attiva e un solo chiamante.

1. Assegna un frame da ConventionalMemory ammessa dalla politica Rust.
2. Blocca il rilascio del suo handle nell'allocatore fisico.
3. Pubblica una PTE supervisor RW/NX e invalida localmente il TLB.
4. Azzera tutti i 4096 byte e ne verifica il contenuto prima del successo.
5. Al rilascio verifica handle, pin e PTE, rimuove la PTE e invalida il TLB.
6. Solo dopo l'invalidazione sblocca e restituisce il frame all'allocatore.

Un handle alterato o una PTE inattesa vengono respinti prima di cambiare
la mappatura. Una pagina mappata non puo essere liberata tramite il
rilascio fisico ordinario. I puntatori alla finestra diventano invalidi
dopo il rilascio: il chiamante non deve conservarli come riferimenti vivi.
Il pin e un protocollo fra componenti kernel fidati, non una barriera
contro codice ring 0 che modifica direttamente i metadati o le tabelle.

## Verifiche e limiti

I test host verificano l'ordine pin/PTE/INVLPG/rilascio, il riuso azzerato,
copie dell'owner, handle alterati, OOM, parent con User/PS, guardie e
finestra occupata. Il backend host sostituisce solo gli accessi hardware.
I test del loader mutano anche la voce ricorsiva e tentano segmenti
nella finestra e nelle sue guardie.

In QEMU il kernel scrive entrambi gli estremi della pagina, verifica il
rilascio fisico bloccato, rimuove la mappatura, riusa e riazzera il frame.
Due immagini dedicate tentano l'esecuzione della pagina NX e la lettura
dopo unmap: devono produrre #PF, CR2 sulla finestra e codici 17 e 0.
Il test verifica anche CR3, ExitBootServices riuscito e lo stack IST.
Queste immagini non possono essere installate con `-Install`.

```powershell
.\arch\x86\boot\uefi\scripts\build.ps1 -BootKernel
.\kernel\scripts\build.ps1 -Install
.\kernel\test\host.ps1
.\kernel\test\image.ps1
.\kernel\test\firmware.ps1 -UseInstalled
```

Loader e kernel devono essere ricostruiti insieme: il vecchio loader
non pubblica la vista ricorsiva richiesta da questo profilo. Il formato
StartInfo v2 resta invariato; il layout virtuale e un accordo statico.
Il profilo ha un solo processore, interrupt disabilitati, niente PCID o
Global nelle nuove PTE. INVLPG e locale: SMP richiedera sincronizzazione
e invalidazioni sugli altri processori. Non vengono reclamate risorse
UEFI, create mappature eseguibili, isolate periferiche tramite IOMMU o
verificati hardware reale, PAT personalizzato e memoria cifrata.

Riferimento: [Intel SDM Volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
capitoli 4.5 e 4.10.4: traduzione a quattro livelli e invalidazione TLB.
