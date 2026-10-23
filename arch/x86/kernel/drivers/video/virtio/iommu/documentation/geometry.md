# Geometria della GPU per la console (prima versione, politica A)

24 ottobre 2026. Decisione utente: politica A, al ritorno sul GOP la vista si ritaglia sulla
griglia GOP (113x42) tenendo visibile la riga del prompt.

## Meccanismo

- **Target con geometria propria** (`framebuffer/pixels/session.h`): `X86_FB_TARGET` porta
  Width/Height/Stride (formato 1). La registrazione rifiuta Width o Height nulli,
  Stride < Width, Reserved diverso da zero e Stride*Height*4 > Bytes.
  `x86_fb_surface_geometry` restituisce la geometria attiva (GOP o target). La usano
  `pixels_begin`, `transaction`, celle, limiti del puntatore e backup del cursore. Con un
  target i limiti si verificano sui suoi byte, non sulle pagine dell'apertura GOP.
- **Superficie** (`iommu/session.h`): viene legata prima di GET_DISPLAY_INFO, perché il dominio
  GPU la mappa. Per questo è dimensionata per il maggiore tra GOP e 1920x1080
  (7,9 MiB, sotto il limite di 16 MiB del backing a voce singola).
- **Present** (`iommu/present.c`): risorsa e target prendono la risoluzione dello scanout da
  GET_DISPLAY_INFO (VirtIO 1.2 5.7.6.8), con stride uguale alla larghezza. Un display che non
  entra nella superficie resta sul GOP (`KERNEL_VGPU_PRESENT_GEOMETRY`). Il vecchio vincolo
  "GOP con stride = larghezza" è stato tolto.
- **Griglia** (`console/runtime.c`): all'apertura segue il target attivo, limitata a 240x67
  (`KERNEL_CONSOLE_GRID`). Dopo un cambio di target più piccolo intervengono
  `kernel_console_service_clip` (transcript e vista insieme) e `kernel_pointer_clamp`.
  `kernel_console_clip` tiene le colonne a sinistra e le righe che finiscono con quella del
  cursore, poi segna tutto dirty.

## Prove

| Campagna | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test host framebuffer, console (griglia, puntatore, service), VirtIO | PASS | suite `scripts/test.ps1` |
| V2c 1280x800, griglia 142x44 | 10/10 | `pointer-interactive-check-ae9adeaa4dd84b96947671580ad045e3` |
| Fallback iniettato, clip 142x44 → 113x42 | 2/2 | `...-69ba59bedbb74085905e6027fb21bcad` |
| Console GOP senza IOMMU | 3/3 | `...-29b55de6d05049c9bf602f3ee935e72a` |
| IOMMU assente (virtio-vga senza traduzione) | 2/2 | `...-be7c9edf10754dbbaaf7a964f6df7add` |

Il runner non forza più `xres/yres`. Screenshot e verifica fisica usano la geometria della GPU
mentre si presenta e quella GOP dopo il fallback.

## Limiti dichiarati

- Una campagna precedente (`...-b9ad18f6a1b54b1cb9c66897c5bb692c`, boot 6) ha avuto un
  timeout reale di una present (`QUEUE_FAULT 3`), con l'host carico da build in parallelo.
  Il fallback è partito, ma il service non veniva ritagliato e la sessione si è fermata. Il
  difetto è stato corretto con `kernel_console_service_clip`. Il timeout sotto carico resta una
  voce aperta (FW-011): la scadenza della coda va rivista.
- Le righe sopra il prompt perse nel clip non si recuperano. Il cambio di risoluzione a
  sessione avviata non è supportato.
