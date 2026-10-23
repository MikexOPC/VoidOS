# Arena DMA persistente (V2a)

23 ottobre 2026. Primo blocco di V2 (decisioni X1-A … X5-A approvate): le pagine DMA di un
driver che resta attivo per tutta la sessione non possono occupare la page window, perché
ogni altro prestito della window (ACPI, ECAM, QI, ...) resterebbe bloccato.

## Contratto

- Slot PML4 229, `X86_DMA_VIRTUAL = (229 << 39) + 4096`, una pagina di guardia per lato.
- Un lease contiguo e pinned da 1 a 16 pagine, mappato supervisor RW, XD, WB (PAT 0):
  Intel SDM Vol. 3A §5.5.4/§5.6. Tre tabelle possedute (PDPT, PD, PT).
- Le pagine vengono azzerate tramite prestiti della window **prima** della pubblicazione:
  nessun dato residuo raggiunge un device.
- L'authority è registrata in `arch/x86/mm/paging/dma/` e l'inventario paging la verifica
  come la superficie: tag 40-42 per le tabelle, foglie esatte, alias rifiutati (una foglia
  fuori dallo slot che nomina la RAM dell'arena fa fallire la scansione).
  L'inventario passa da 3840 a 3896 byte; il contratto di boot v5 non cambia.
- Owner in `arch/x86/mm/dma/arena.c`: bind/unbind con rollback completo e budget
  ripristinato; `x86_dma_arena_bytes` restituisce la vista solo con l'authority corrente.

## Uso

Le code VirtIO (probe code, risorse, V1) prendono le 4 pagine dall'arena invece che dalla
page window. In V1 il parcheggio della window non serve più: il dominio VT-d si costruisce
con la window libera. `x86_page_window_park/resume` restano disponibili e testati.

## Errore trovato durante il blocco

`x86_paging_gpu_complete` ripercorre i record con una propria catena di tag (32+ IOMMU,
20+ superficie) e non conosceva i tag dell'arena: la prima cattura reale falliva. È stata
aggiornata, e la suite paging ora ha un caso di scansione con l'arena (gruppo 9), che prima
mancava e avrebbe trovato l'errore sull'host.

## Evidenze

| Prova | Risultato |
|---|---|
| `arch/x86/mm/dma/scripts/test.ps1` | 59.304 controlli: 800 bind da 1 a 16 pagine, guardie, alias, rifiuti, rollback a ogni passo, azzeramento; cppcheck pulito |
| Suite paging | 9 gruppi × 100, compreso il caso arena (registrata/non registrata, corruzione, alias, rimozione) |
| Suite host framebuffer, VirtIO, kernel | verdi con l'inventario a 3896 byte |

Le campagne QEMU sono riportate nel commit.

## Limiti

- Una sola arena alla volta (un solo slot), massimo 16 pagine.
- L'owner usa gli helper PML4 del modulo framebuffer (`x86_fb_root`, `x86_fb_flush`) come
  superficie e IOMMU; un helper di paging neutro è un possibile riordino futuro.
- Authority e registry ricalcano quelli della superficie: un registry di slot generico
  eliminerebbe la duplicazione, ma toccherebbe moduli già verificati.
