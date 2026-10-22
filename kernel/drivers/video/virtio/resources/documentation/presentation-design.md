# Scanout e flush: diagnostico visivo

20 ottobre 2026. Scelta utente: diagnostico visivo prima, console dopo.
Perimetro: risorse VirtIO, soli adattatori code/build/probe/test necessari.
Nessun nuovo mapper, handler IRQ, loader o percorso ordinario. Conservata
l'eccezione DMA esclusivamente per QEMU fidato, senza passthrough.

## Ricerca primaria e compromesso

[VirtIO 1.2 CS01](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
5.7.6.1/2/5/8: associare risorsa e display, trasferire e fare flush;
FENCE bit0 richiede completamento prima della risposta. SET_SCANOUT0x103:
header24, rettangolo16, scanout_id4, resource_id4 (48 byte).
RESOURCE_FLUSH0x104: header24, rettangolo16, resource_id4, padding4
(48 byte). TRANSFER0x105 conserva56 byte: offset backing al byte40,
resource_id al byte48. Tutti i campi sono little endian.
Disabilitazione con SET_SCANOUT resource_id0, prima di detach/unref.

[Linux v6.12 virtgpu_display.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/virtio/virtgpu_display.c):
display e aggiornamenti hanno lifetime distinto dall'oggetto risorsa;
il backend DRM gestisce il runtime. Qui conserviamo un solo owner BSP
diagnostico e richieste sincrone. Nessuna copia del framework DRM.
[QEMU v11.1.0 virtio-gpu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/display/virtio-gpu.c):
il trasferimento parziale usa offset sorgente e stride della risorsa;
il test deve confrontare i pixel sul display, oltre alle risposte.

Alternativa discussa: collegare subito la console ordinaria. Richiederebbe
sessione persistente, aggiornamenti concorrenti e recovery del display;
l'utente ha scelto di separarla da questo blocco.

## Contratto

- Una risorsa, primo scanout abilitato fra16 entry GET_DISPLAY_INFO.
  Conservare anche l'indice; non ricostruirlo dalle sole dimensioni.
- Stessa RAM WB pinned, formato B8G8R8A8, limite16MiB. Nessun resize.
- Risorsa con identita Self e coda proprietaria; vietato close mentre
  lo scanout e attivo o il risultato di un comando e incerto.
- Bind/flush/update/unbind confermati solo da risposta fenced valida.
  Un errore dopo pubblicazione rende incerto lo stato: nessun comando
  di cleanup alla cieca; il probe deve confermare reset prima del rilascio.
- Rettangoli senza clipping: coordinate, dimensioni e offset/stride
  verificati da Rust puro prima di pubblicare il comando.
- Il diagnostico mostra due scene: immagine intera, poi modifica di un
  rettangolo interno. Il test host verifica screenshot completi, inclusa
  l'assenza di modifiche fuori dal rettangolo.
- Checkpoint di solo test nel tail inutilizzato della pagina cursorq,
  oltre descrittori/ring. Il device non riceve quel tail come descrittore.
  Host conferma ogni screenshot; attesa guest con clock e limite campioni,
  deadline host e terminazione della sola VM posseduta.

## Pseudocodice non eseguibile

1. Aprire risorsa: CREATE, ATTACH, TRANSFER intero fenced.
2. SET_SCANOUT su indice verificato; FLUSH intero fenced.
3. Checkpoint1: host ferma VM, screendump e confronto, conferma, riprende.
4. Modificare rettangolo RAM; TRANSFER con offset=(y*width+x)*4;
   FLUSH rettangolo. Checkpoint2 e confronto completo.
5. SET_SCANOUT resource0; DETACH; UNREF; reset confermato.
6. Rilasciare code/mapping, verificare inventario e budget originale.

## Accettazione prevista

Test host C/ASan e Rust su limiti, alias, serializzazione, indice nonzero,
offset/stride, stati incerti, errori a ogni comando e close vietato mentre
associato. Dieci boot consecutivi con due screenshot esatti; negativo
controllato con reset/cleanup. Regressioni risorse/code e console ordinaria.
Nessuna promessa di frame atomici, fluidita, cursore hardware o GPU fisica.
