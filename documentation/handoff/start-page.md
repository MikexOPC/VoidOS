# Pagina del contratto di ingresso

Il boot alloca una pagina di 4 KiB EfiLoaderData prima delle tabelle e
la azzera completamente. `handoff/protocol/page.c` ne mantiene il
proprietario distinto da kernel, stack e pagine delle tabelle.
Il contratto versione 2 occupa i primi 112 byte, il resto della pagina resta zero.

## Mappatura e verifica

Il piano aggiunge la pagina all'indirizzo virtuale `0x0000600000000000`,
prima della voce dello stack. Il PTE e supervisor, Present, senza RW e
con NX. Non esistono alias fisici tra le voci del piano. Il limite e
19 mappature: 16 segmenti kernel, una pagina contratto, la mappa persistente e lo stack.
La pagina contratto aggiunge 4 KiB al budget massimo kernel di 128 MiB.

L'inserimento valida il piano completo; se un indirizzo collide, ripristina
il piano precedente. Il costruttore controlla anche che le nuove pagine
delle tabelle non siano alias della pagina contratto. Una mappa UEFI fresca,
acquisita dopo le allocazioni, verifica tutte le risorse come EfiLoaderData.
Il contratto viene riempito solo dopo questi controlli. `Ready` viene
pubblicato dopo il rilascio riuscito del buffer temporaneo della mappa.

Il loader scrive attraverso l'identity mapping corrente del firmware.
La protezione R/NX descrive le nuove tabelle inattive: non protegge ancora
questa pagina nello spazio corrente. Il contratto mantiene
`EnabledFeatures=0` e non viene passato al kernel.

## Proprieta e fallimenti

Il chiamante usa un proprietario inizialmente azzerato e lo mantiene vivo
fino al rilascio. Un output di allocazione riuscita ma numericamente
invalido viene conservato per il cleanup, senza dereferenziazione.
Un output che coincide con risorse gia possedute segnala firmware non
conforme: non viene adottato o liberato come secondo proprietario.
Questo controllo comprende anche i metadati del proprietario, il piano
di mappatura e tutta la tabella dei servizi indicata da HeaderSize.

Il rilascio invalida prima il contratto pronto. Se FreePages fallisce,
conserva indirizzo e numero di pagine per un nuovo tentativo. Dopo il
successo, ulteriori rilasci non chiamano FreePages. Il boot tenta comunque
il rilascio delle altre risorse e conserva il primo errore.
Non liberare questa pagina dopo l'attivazione delle nuove tabelle finche
il kernel la usa: il cleanup attuale avviene prima di ogni cambio CR3.

## Test

I test C verificano azzeramento della pagina, collisioni, rollback,
allocazioni fallite, indirizzi invalidi senza scritture e FreePages
ritentabile senza doppio rilascio. Il test delle tabelle percorre i quattro
livelli e verifica il PTE R/NX anche con 16 segmenti e 128 MiB kernel.
Il percorso d'ingresso simula errori di allocazione e rilascio del contratto.
QEMU verifica entrambi gli ELF, la nuova fase e il ritorno al firmware.

[UEFI: AllocatePages e FreePages](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#memory-allocation-services).
