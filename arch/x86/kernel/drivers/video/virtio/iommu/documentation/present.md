# V2c: console presentata dalla GPU VirtIO, ritorno al GOP

23 ottobre 2026. Decisioni approvate: Z1-A target di disegno generalizzato, Z2-A un
present per flush, Z3-A geometria diversa → resta il GOP, Z4-A primo errore → GOP senza
nuovi tentativi, Z5-A avvio della GPU nello start di V2b.

## Flusso

1. **Avvio** (dentro `x86_vgpu_session_start`, dopo TE=1, prima del watchdog): transport,
   negoziazione con ACCESS_PLATFORM, code nell'arena con IOVA, GET_DISPLAY_INFO, risorsa sulla
   superficie (CREATE/ATTACH/TRANSFER), SET_SCANOUT e FLUSH. Poi il target di disegno della
   console passa alla superficie (`x86_fb_target_redirect`).
2. **Sessione**: le celle e il puntatore si disegnano nella superficie; ogni flush che ha
   disegnato qualcosa invia TRANSFER_TO_HOST_2D + RESOURCE_FLUSH del rettangolo sporco.
3. **Errore** (comando fallito o fault della GPU dal polling V2b): target di nuovo sul GOP,
   reset del device, ridisegno di tutte le celle; la GPU non viene ritentata.
4. **Exit**: SET_SCANOUT 0, DETACH, UNREF, reset, chiusura del transport, poi lo stop di V2b.

## Modifiche fuori dal modulo

- **Pixel framebuffer**: `X86_FB_TARGET` e il registro del target. Con un target registrato le
  sessioni pixel scrivono alla sua base con la stessa geometria del GOP (larghezza, altezza,
  stride, formato); l'owner GOP mantiene tutte le sue verifiche. `x86_fb_pixel_view` accetta il
  solo intervallo del target registrato.
- **Cursore** (`drawing/cursor.c`): lo sfondo salvato sotto il puntatore è legato al target da
  cui è stato letto; al cambio di target viene scartato. Senza questo, dopo il fallback il
  primo movimento ripristinava sul GOP pixel letti dalla superficie e lasciava tracce.
- **Console** (`framebuffer/console/runtime.c`): rettangolo sporco delle celle disegnate più i
  riquadri vecchio e nuovo del puntatore. Il livello del cursore compone l'immagine 18x18 e
  ripristina il riquadro precedente oltre le celle del gruppo: senza i due riquadri il present
  ne trasferiva solo una parte.

## Errori trovati durante il blocco

- `x86_vgpu_transport_end` ricattura l'inventario paging: chiamata nel fallback (task console,
  stack vivi mappati) falsava la base del disegno live e il GOP non era più disegnabile. Nel
  fallback ora c'è solo il reset; il transport si chiude all'uscita.
- La suite host delle code VirtIO (`lifecycle_test.c`) non linkava più dopo V2a (la probe usa
  l'arena e i mock simulavano la page window). Il commit V2a non l'aveva rieseguita; ora i mock
  modellano l'arena (115.000 controlli).

## Evidenze

| Prova | Risultato |
|---|---|
| Host pixel | gruppo "redirected target": rifiuti di registrazione, scrittura alla base del target e non al GOP, target invalidato o troppo piccolo senza scritture, ritorno al GOP |
| Host VirtIO code, risorse, init, console, framebuffer, kernel | verdi |
| Runner interattivo `--iommu` | screenshot e pixel del cursore dallo scanout VirtIO e dalla superficie: circa 990 present a boot, nessun fallback |
| Runner `--present-fallback` (`-TestVgpuPresentFault`, present n. 700 fallito durante il movimento) | ritorno al GOP, ridisegno completo, cursore e pixel GOP corretti, exit pulito |
| Runner `--iommu-absent` | GPU VirtIO senza IOMMU: nessuna traduzione e nessun present |

Le campagne da 10 boot sono riportate nel commit.

## Limiti

- **Geometria**: OVMF imposta il GOP a 1024x768, mentre la GPU QEMU riporta 1280x800 di
  default. Con geometrie diverse Z3-A lascia il GOP; il runner configura `xres=1024,yres=768`.
  Riadattare la griglia della console alla GPU (Z3-B) è lavoro futuro.
- Il fallback è provato con un errore iniettato (flag di build di test), non con un errore reale
  del device; la sua causa reale più probabile è un timeout o un fault IOMMU.
- Il ridisegno completo sul GOP dopo il fallback è lento in QEMU TCG (decine di secondi): il
  runner concede 90 s al segmento in cui avviene.
- Polling sincrono: ogni flush attende due comandi fenced. Latenza da misurare (V2d).
- Nessun host test del modulo `iommu/present/` con una coda simulata: la sequenza è coperta dai
  test delle code/risorse/scanout e dalle prove QEMU positive e negative.
