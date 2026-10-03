# Checkpoint: allocazione fisica con allineamento esplicito

## Modifica e revisione

Il kernel puo richiedere blocchi contigui con base allineata a una
potenza di due di pagine. L'API precedente rimane equivalente a un
allineamento di una pagina. Il nuovo validatore Rust no_std riceve solo
scalari e controlla moltiplicazioni, somme, geometria e limite CPU prima
di restituire una base. Zero significa nessun candidato, mai una pagina
assegnata. Non ci sono puntatori, heap o blocchi unsafe nel validatore.

C conserva ownership, pin, ID monotoni e budget. Uno split conserva
prefisso e suffisso senza consumare pagine di padding. Tutti i controlli
precedono le mutazioni; un rifiuto lascia owner e output invariati.
La fusione avviene prima dell'inserimento, per consentire rilasci
adiacenti anche senza un record libero. Un candidato che richiede uno
split senza metadati disponibili viene saltato; un rilascio isolato
senza spazio viene rifiutato. Gli indici di spostamento sono limitati
alla capacita verificata di 256 extent.

I metadati di produzione restano 6448 byte. Non viene aggiunta memoria
proporzionale alla RAM; le bitmap e fotografie sono solo fixture host.
La build resta senza CRT, import, TLS o rilocazioni. Moduli C principali:
allocatore 220 righe, nuovo test 219, kernel/main.c 217. Header e
validatori piccoli restano compatti come concordato con l'utente.

## Evidenze sullo stato corrente

- Test host completi: `kernel/build/physical-alignment-host.log`.
- 8192 operazioni miste confrontate con una bitmap indipendente:
  posizione first fit, contiguita, disgiunzione, budget e ogni extent.
- 278528 confronti Rust con enumerazione delle pagine, oltre a overflow,
  allineamenti invalidi e limiti CPU da 32 a 52 bit. 21 test Rust totali.
- Rifiuti confrontati byte per byte; split, pin, stale handle e riuso.
- Fixture sintetiche con 256 extent: nessuno split oltre capacita,
  candidato successivo, fusioni a sinistra/destra/entrambi, rilascio
  isolato rifiutato senza modifiche. La selezione UEFI resta invariata.
- Build MSVC C11 `/W4 /WX`, MASM `/W3 /WX`, Rust `-D warnings` superate.
- Conversione ELF: immagine valida e sette mutazioni invalide verificate.
- QEMU: 18/18 scenari, nessun triple fault. Ogni scenario verifica
  allocazioni di 4 pagine con allineamento 64 KiB e 8 pagine con
  allineamento 2 MiB, disgiunzione e ritorno del budget prima del successo.
- Log QEMU: `kernel/build/firmware-96c91482ac51481c9409788d60d57a0a/`.

L'ELF normale costruito, installato e copiato nel caso ready ha SHA-256:
`EDF44B2CEFAEC482C2445B8ED2A4C75AF98129A3AC13FB77F868AD63B2F5D4B1`.
Dimensione su disco 53248 byte, segmenti residenti arrotondati 57344 byte;
entrambi crescono di una pagina rispetto al checkpoint dello stack.
EFI invariato: `07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3`.

## Limiti e passo successivo

L'allineamento fisico non attiva huge page, non crea mapping virtuali
e non rende gli indirizzi fisici dereferenziabili. La politica gestisce
solo ConventionalMemory ordinaria WB; pagine loader, tabelle paging,
contratto e fotografia UEFI rimangono riservati. Il profilo resta UP,
senza concorrenza o reclamazione firmware, verificato in QEMU e host.
Non e una prova su hardware reale o una garanzia formale di assenza bug.

Il prossimo blocco di memoria e un mapper con risorse e durata di vita
esplicite oltre la singola finestra temporanea. Prima di reclamare
risorse loader servira un inventario preciso delle pagine ancora vive.
Interrupt, timer e scheduler restano passi distinti successivi.

Riferimenti: [documentazione allocatore](../../kernel/memory/physical-allocator.md),
[UEFI 2.11, pagine e mappa](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html),
[Rust core u64, operazioni checked](https://doc.rust-lang.org/stable/core/primitive.u64.html).
