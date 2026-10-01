# Preparazione della mappa persistente

`handoff/firmware/prepare_map.*` prepara un buffer dedicato dopo kernel,
stack, contratto e tabelle. Le tabelle sono ancora inattive e i servizi
firmware devono essere disponibili.

Il modulo ricostruisce il piano numerico di kernel e stack e aggiunge
la riserva fisica del contratto. Usa una copia dei metadati della pagina
per questa verifica: non modifica la pagina pronta o il piano usato per
costruire le tabelle.

Alloca il [buffer persistente](../../memory/persistent-map.md), acquisisce
una nuova fotografia e verifica la copertura di:

- tutte le pagine del buffer stesso;
- segmenti del kernel e stack;
- tutte le pagine delle tabelle;
- pagina del contratto.

Le risorse devono essere `EfiLoaderData` e prive dell'attributo runtime.
Un errore invalida la fotografia, mantenendo il proprietario del buffer
per il cleanup. Il modulo non rilascia kernel, stack, contratto o tabelle.

## Percorso attuale

L'ingresso UEFI mostra una sezione dedicata soltanto dopo le verifiche
precedenti. Segnala successo dopo la nuova verifica e stampa la capacita
del buffer. Poi ritorna ancora al firmware, rilasciando anche queste
pagine prima delle altre risorse.

La mappa persistente non e ancora mappata nelle nuove tabelle o descritta
nel contratto di ingresso. Questo passo non attiva CR3 e non chiama
ExitBootServices. Ulteriori attivita firmware, inclusa la console, possono
rendere vecchia la chiave: il futuro percorso di uscita deve aggiornare
il buffer immediatamente prima di ciascun tentativo.

## Test

Sei scenari dedicati verificano successo e tipo non ammesso per ciascuna
delle cinque categorie di risorse. Gli adattatori restano intatti e la
mappa e posseduta anche su errore.

I test dell'ingresso simulano fallimento dell'allocazione, aggiornamento
fallito e FreePages fallito per la mappa persistente. Verificano lo status
restituito e i tentativi di cleanup delle altre risorse.
QEMU controlla ordine, separatori e successo della nuova sezione nei due
scenari validi, insieme al successivo ritorno al firmware.

[UEFI: GetMemoryMap e ExitBootServices](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#efi-boot-services-getmemorymap).
