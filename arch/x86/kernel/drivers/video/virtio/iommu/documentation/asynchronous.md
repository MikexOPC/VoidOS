# Present asincrona con scadenza e recupero (R1-A, R2, R3-A)

24 ottobre 2026. Decisioni approvate: R1-A completamento asincrono tra un flush e l'altro, R2
100 ms per il log `SLOW` e 1 s per il fallback, R3-A recupero dopo un timeout (al massimo 3 per
sessione, a 5 s di distanza), R4 prove.

## Perché

Un boot della campagna geometria (`...-b9ad18f6a1b54b1cb9c66897c5bb692c`, boot 6) ha superato la
scadenza fissa di 100 ms della coda VirtIO con l'host carico. Il fallback era definitivo. In TCG
il timer PM segue il tempo dell'host, quindi un host occupato produce scadenze "spurie". Allungare
la wait bloccante non andava bene: lo scheduler è cooperativo, il worker PS/2 non svuota il suo
buffer da 32 byte durante l'attesa e un overflow ferma l'intera sessione.

## Meccanismo

- **Coda** (`kernel/drivers/video/virtio/queues/exchange.c`): `kernel_vgpu_queue_submit` invia un
  comando e ritorna (State 2). `kernel_vgpu_queue_poll(limit)` osserva una volta sola:
  1 completato, 0 in volo, -1 fault. Il completamento si controlla prima della scadenza. La forma
  bloccante resta per bring-up e chiusura.
- **Scanout** (`resources/scanout.c`): `kernel_vgpu_scanout_post` fase 0 (TRANSFER_TO_HOST_2D,
  Bound incerto) e fase 1 (RESOURCE_FLUSH dello stesso rettangolo); `kernel_vgpu_scanout_settle`
  conferma.
- **Present** (`iommu/present/lifecycle.c` e `update.c`):
  - un solo aggiornamento in volo; il danno che arriva nel frattempo si unisce al successivo;
  - ogni flush della console avanza lo stato senza mai aspettare (`x86_vgpu_present_poll`);
  - `x86_vgpu_present_busy` tiene attivo il worker finché c'è qualcosa in volo o in attesa.
- **Limiti:** oltre 100 ms un solo log `KERNEL_VGPU_PRESENT_SLOW`. A 1 s timeout, con
  `KERNEL_VGPU_PRESENT_TIMEOUT`, poi fallback sul GOP.
- **Recupero:**
  - si applica solo dopo un timeout e riusa trasporto e coda nativa già aperti, perché riaprirli
    riacquisirebbe l'inventario, vietato nei task di sessione;
  - ripete negoziazione, coda, display info, risorsa, scanout e target;
  - la superficie viene azzerata e presentata intera; la griglia resta quella ritagliata (113x42)
    su sfondo nero;
  - errori di stato, di risposta o iniettati restano definitivi.
- **Contatori di chiusura:** `KERNEL_VGPU_PRESENT_SLOWS`, `_TIMEOUTS`, `_RECOVERIES`.

Difetto trovato durante le prove: per azzerare la superficie avevo usato
`x86_vgpu_surface_pixels`, che riacquisisce l'inventario. Dopo il recupero ogni disegno veniva
rifiutato. Ora uso il solo controllo dell'autorità (`x86_vgpu_surface_check`).

## Prove

| Campagna | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test host coda: submit/poll, in volo, scadenza, completamento prima della scadenza, stato perso, risposta corrotta, argomenti | PASS (647.591 controlli) | suite `virtio-queues` |
| Test host scanout: post/settle, stati rifiutati, submit fallito | PASS | suite `virtio-resources` |
| V2c con host carico (7 processi di stress su 8 core) | 10/10 | `pointer-interactive-check-75d6d633c3614cb2a1a1759d7e9a2f58` |
| V2c con host scarico | 10/10 | `...-efb16ddedfcc47d3b952c37ab45fc95c` |
| Stallo iniettato (0,2 s slow, 1,2 s timeout, recupero) | 5/5 | `...-32c7759c697c47e9ac489d9715e1e5c6` |
| Fallback iniettato | 2/2 | `...-e8f58bff8e4d4fa49ab7bff03f794ee5` |
| Console GOP | 3/3 | `...-768c2bb10b7841db848a89201a8e88cd` |
| IOMMU assente | 2/2 | `...-2656c0e82bf44386a090f5d78e8af857` |

Sotto carico:
- nessun timeout, nessun fallback e nessuna perdita PS/2 (`MOUSE_LOSSES` 0);
- present massima tra circa 42 e circa 158 ms per boot, con un solo `SLOW`;
- con la vecchia scadenza di 100 ms quel boot sarebbe finito in fallback permanente.

## Limiti dichiarati

- La logica di present (unione del danno, limiti, recupero) è provata in QEMU con l'immagine di
  stallo, non da test host: `iommu/present/` non ha ancora un harness host (FW-011).
- `FLUSH_WAIT_PM` misura ora dall'invio all'osservazione, che avviene al flush successivo: non è
  più confrontabile con i valori V2d.
- 100 ms e 1 s sono ipotesi, non benchmark. Le misure V2d e del profilo non sono state ripetute.
- La griglia non torna a 142x44 dopo un recupero.
