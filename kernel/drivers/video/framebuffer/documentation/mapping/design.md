# Mapping framebuffer v0 — contratto e prerequisiti

9 ottobre 2026. Scelta utente: UC, ownership kernel e rollback.
Questo documento distingue il preflight numerico implementato dal mapping
hardware ancora da implementare. Nessun pixel viene scritto dal preflight.

## Ricerca verificabile

- [Linux 6.6, Device memory mapping modes](https://www.kernel.org/doc/html/v6.6/driver-api/device-io.html#device-memory-mapping-modes):
  distingue accessi device non cacheabili e write-combining. Quest'ultimo
  migliora gli aggiornamenti video ma consente combinazione e riordino;
  l'idoneita della risorsa e le barriere sono prerequisiti distinti.
  Confronto concettuale, nessuna API o implementazione Linux copiata.
- [Intel, indice SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html):
  consultato il 9 ottobre; indica revisione093. Il download del volume3A
  dal browser non era leggibile. Non viene dichiarata la lettura di tale PDF.
- [Intel 252046-074, dicembre2023](https://cdrdv2-public.intel.com/812380/252046-sdm-change-document.pdf):
  fonte primaria effettivamente leggibile, aggiornamento documentale con
  estratti del capitolo12. §12.12.2: IA32_PAT0x277, otto campi da8 bit,
  encoding UC0, WC1, WB6, UC-7; encoding riservati da rifiutare.
  §12.12.3, tabella12-11: PTE4KiB PATbit7, PCDbit4, PWTbit3 selezionano
  l'indice PAT. §12.5.2.2, tabella12-7: tipo effettivo combinato con MTRR;
  vietato dedurlo dal solo PCD. Fonte datata, non revisione093 corrente.
  Il backend dovra completare il riscontro di permessi, invalidazione e
  prerequisiti CPU prima di introdurre nuovi accessi hardware.

## Decisione v0 e upgrade

UC forte, non UC-, usando uno slot gia configurato e validato.
Nessuna modifica globale di PAT/MTRR per far funzionare il driver.
Pagine da4KiB supervisor RW/NX, niente huge pages, niente DMA, niente ISR
di disegno. Un owner BSP; mutazioni del mapping a IF=0. IF=0 non e un
lock SMP. WC resta un upgrade con discovery/capabilities e misure.

Il backend x86 vive in arch/x86/kernel/drivers/video/framebuffer/;
il contratto numerico e gli offset restano nel modulo comune corrente.
La base fisica deriva dall'handoff GOP validato, non da un BAR presunto.
Nessun registro GPU aggiuntivo e nessun codice del vecchio OS importato.

## Geometria implementata

FB_MAPPING_GEOMETRY e un risultato numerico da40 byte, non un handle live.
C valida il descrittore completo e usa le funzioni Rust senza puntatori;
Rust usa solo aritmetica controllata e vieta unsafe. Output C pubblicato
soltanto dopo successo, input/output disgiunti e allineati. Il chiamante
deve possedere buffer kernel accessibili: il controllo numerico non e
fault recovery per puntatori utente.

Il framebuffer assente produce cinque campi zero. Un descrittore presente
invalido viene rifiutato senza modificare l'output. Physical arrotonda Base
verso il basso a4KiB; Offset conserva il residuo. Pages copre Used, non
Capacity. Il controllo delle risorse continua a usare Capacity intera.
L'ultima pagina non supera il limite fisico quando l'intervallo valido
termina esattamente al limite CPU, che e allineato a pagina.

Layout proposto: nuovo ramo virtuale con una prima pagina di guardia,
poi Pages pagine video e una guardia finale. Slot PT iniziale della
guardia0; indirizzo virtuale definitivo da verificare contro tutti i
mapping esistenti. TablePages = ceil((Pages+2)/512)+2: PT necessarie,
una PD e una PDPT; PML4 corrente riusato, nessuna RAM video allocata.
Non e il budget di un intervallo virtuale arbitrariamente disallineato.

I limiti v0 della geometria danno Used massimo17694720 byte.
Base allineata a4 byte puo avere Offset massimo4092: al massimo4321
pagine video,9 PT e11 pagine di tabelle,44KiB. Il kernel controlla il
budget fisico disponibile e usa MAXPHYADDR del pool gia validato.
Per1024x768/stride1024/Base allineata:768 pagine e4 pagine di tabelle.
Non vengono ancora riservate, pinned o pubblicate queste pagine.

Le guardie proteggono pagine complete, non i byte prima di Base o dopo
Used nell'eventuale pagina parziale: ogni store richiede comunque il
controllo dell'offset e deve restare nei quattro byte del pixel valido.

## Prerequisito concreto del backend

L'inventario attuale riconosce un solo device di una pagina, il LAPIC,
e richiede che le tabelle appartengano alla memoria loader riservata.
Non accetta gia le nuove tabelle allocate dal kernel o migliaia di
foglie video. Non aggirare questa verifica e non contare il video come RAM.

Prima di un mapping live serve un contratto nell'inventario per:

1. Tabelle kernel con lease e pin, distinte dalle tabelle loader.
2. Intervalli MMIO numerati, ownership, cache e conteggio delle foglie.
3. Rifiuto di alias fisici con video, RAM, LAPIC e altre risorse vive.
4. Pubblicazione atomica rispetto al BSP dopo preflight e preparazione.
5. Revoca completa, invalidazione delle traduzioni, unpin e rilascio
   soltanto delle tabelle possedute; mai FreePages sulla memoria video.

Pseudocodice non eseguibile del backend futuro:

```text
assente -> nessuna allocazione o scrittura
validare piano, CPU/cache, inventario, intervallo virtuale e risorse
allocare budget tabelle -> ownership esplicita
preparare ramo staccato tramite una vista temporanea RAM WB
revocare vista temporanea; pin e verificare ramo/guardie
pubblicare ramo; invalidare e verificare PTE/inventario
solo ora abilitare store di pixel limitati
errore -> revocare ramo, invalidare, unpin, liberare tabelle proprie
rollback incompleto -> fail-stop con ownership conservata
```

## Accettazione

Preflight: confini fisici32..52, base parziale, overflow, descrittore
assente/corrotto, output invariato su errore, canary e10000 piani.
Riferimento Rust indipendente con u128 e conteggio iterativo delle PT.
Kernel: dieci boot video e dieci senza display con piano ricontrollato
dal test host, hash e marker KERNEL_READY. Nessuna prova di rendering.

Backend futuro: failure injection per allocazione/pubblicazione/rollback,
accounting ripristinato, nessun alias, PTE RW/NX/UC e guardie verificate.
Dieci boot finali con pattern RGB/BGR letto dallo screenshot, anche ai
bordi; screenshot della sola geometria insufficiente. Hardware fisico,
WC, SMP e GUI restano fuori dal profilo verificato.
