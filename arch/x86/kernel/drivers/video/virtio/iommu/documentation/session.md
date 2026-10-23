# V2b: traduzione VT-d persistente della GPU nella sessione interattiva

23 ottobre 2026. Decisioni approvate: Y1-A (operazioni VT-d solo fuori dagli interrupt,
IF=0 attorno ai prestiti della window), Y2-A (RMRR → nessuna traduzione persistente),
Y3-A (polling dei fault a ogni flush della console), Y4-A (stessa regola di inventario per
tutti i moduli). Chiude W2: persistente nella sessione, temporanea solo nei diagnostici.

## Cosa fa

Con l'opzione di build `-VgpuIommu` (solo con `-ConsoleShell`), nel passo B12:

1. **Avvio** (`video_private`, prima del watchdog, IF=0): route GPU (ECAM, topologia, DMAR),
   arena DMA (4 pagine), superficie con la geometria del framebuffer, dominio privato della
   GPU, QI, TE=1. Senza DMAR, o con una RMRR, la sessione resta come prima (non tradotta).
   Un errore prima della pubblicazione rilascia tutto e torna allo stato precedente.
2. **Sessione**: a ogni flush della console una scansione dei fault, solo registri (nessun
   prestito della window). Un fault della GPU la segna compromessa (X5-A).
3. **Arresto** (dopo la chiusura del watchdog): TE=0, invalidazione, rilascio di dominio, QI,
   mapping, superficie e arena, budget verificato. Se lo smontaggio è incerto: RETAINED.

In V2b la GPU non fa ancora DMA: la console disegna sul GOP. V2c userà questa traduzione.

## Modifiche fuori dal modulo

- **Inventario paging**: il framebuffer pubblicato registra la sua authority
  (`x86_paging_video_register`); le varianti LAPIC delegano alla variante video quando una è
  registrata. *Deviazione dal testo di Y4-A*: invece di passare una funzione di verifica a
  registri, domini ed ECAM, la regola è unica e centrale; lo scopo approvato (stessa
  verifica ovunque) non cambia. I corpi LAPIC sono separati in funzioni `noinline`: due
  inventari nello stesso frame superavano una pagina di stack (`__chkstk`).
- **Catene di tag**: `x86_paging_video_tables_complete` non conosceva i tag dell'arena
  (40-42); ora sì, con un caso host (gruppo 10 della suite paging).
- **DMAR**: il campo riservato diventa `Rmrr` (record di tipo 1, VT-d 5.0 §8.4); l'ABI resta
  992 byte. Il preflight dei registri controlla `Rmrr ≤ OtherRecords`.
- **Contabilità**: il watchdog fissa il budget al suo avvio e il disegno live della console
  conosce solo tabelle GPU e stack. Per questo l'avvio è prima del watchdog e
  `x86_vgpu_session_pages()` entra nel conto del disegno live.
- **Collegamento GPU+VT-d** spostato da `test/` a `arch/.../virtio/iommu/domain.c`: V1 e V2b
  usano lo stesso codice.
- `dma/registry.c` → `dma/dma_registry.c`: due `registry.obj` nello stesso output si
  sovrascrivevano in alcune suite host.

## Evidenze

| Prova | Risultato |
|---|---|
| Suite paging | 10 gruppi × 100, compreso video + arena e la delega LAPIC → video |
| DMAR host | conteggio RMRR |
| Host framebuffer, registri, attivazione, VirtIO, arena, kernel | verdi |
| Runner interattivo `--iommu` | scenario completo (cursore, Esc, help, exit) con TE=1 dall'avvio all'arresto, GPU 00:01.0 tradotta, nessun record di fault della GPU, scansioni > 0, budget uguale |
| Runner interattivo `--iommu-absent` | GPU VirtIO senza IOMMU: sessione non tradotta, nessuna allocazione |

Le campagne da 10 boot sono riportate nel commit.

## Limiti

- **RMRR**: il rifiuto è verificato solo nel codice e nel decoder host; QEMU non permette di
  aggiungere una RMRR alla DMAR che genera, e una seconda DMAR viene rifiutata come duplicato.
- Su hardware reale ogni device senza dominio perde la DMA con TE=1 (deny by default).
- Avvio lento in QEMU TCG: la scansione del grafo PCI richiede decine di secondi.
- Fault in polling, invalidazione globale; nessun interrupt di fault.
