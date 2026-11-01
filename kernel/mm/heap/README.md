# Heap generico del kernel (G3, sincronizzazione K2-C)

Stato aggiornato al 28 ottobre 2026. Decisioni G3 approvate: H1-A slot PML4 dedicato, H2-A slab per classi più sequenze
di pagine, H3 authority nell'inventario del paging, H4 16 MiB fissi all'avvio, H5 struttura e prove.

## Struttura

- **Nucleo portabile** (`kernel/mm/heap/heap.c`), indipendente dal paging:
  - slab da 16 a 2048 byte, fino a 256 pagine di slab, con una bitmap degli slot per pagina;
  - sopra i 2 KiB (o con allineamento a 4096) sequenze di pagine contigue, first fit su una
    bitmap delle pagine;
  - memoria restituita azzerata e riempita con 0xCC al free;
  - canary di 8 byte in fondo agli slot;
  - double free, free non validi e canary corrotti vengono rifiutati e contati in `Rejected`.
- **Mappatura x86** (`arch/x86/mm/heap/binding.c`):
  - slot 230, a partire da `0xfffff30000200000` dopo K1;
  - 4096 pagine in un unico lease pinnato e 10 tabelle (PDPT, PD, 8 PT) in un secondo lease
    pinnato: 2 lease su 64;
  - foglie RW/XD/WB; il primo e il decimo blocco da 2 MiB dello slot restano vuoti come
    guardie.
- **Authority nel paging** (`arch/x86/mm/paging/heap/heap_registry.c`):
  - tag delle tabelle 48–57;
  - tipo di foglia 9;
  - snapshot da 48 byte in fondo all'inventario, che passa da 3896 a 3944 byte; gli offset usati
    dai runner non cambiano.
- **Avvio** (`kernel/startup/devices.c`): `x86_heap_kernel_start` è la prima operazione di
  `kernel_startup_devices`, in tutti i profili e prima del framebuffer, così ogni budget
  successivo comprende già l'heap. Se fallisce, `KERNEL_HEAP_FAILED`.
- **Accesso:** `kernel_heap()` restituisce l'heap del kernel.
- **Sincronizzato per owner:** metadati con ticket lock irqsave rango 200;
  azzeramento e poison senza il lock dell’heap, su memoria riservata fino
  al commit. Stats include Pending; init privata su owner azzerato, nessuna
  reinizializzazione di owner pubblicato. Mai da NMI/fault o handler IRQ.
- [Contratto K2-C](documentation/k2-design.md) e
  [prove e limiti](documentation/k2-validation.md).

## Prove storiche G3, precedenti a K2-C

| Prova | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test host del nucleo (ASan, cppcheck): 200.000 operazioni casuali con oracolo, rifiuti, canary, poison, classi | PASS | `heap-host` |
| Immagine `-TestBootstrapMemory -TestKernelHeap`: self-test con slab, sequenze di pagine, 64 double free e un canary rifiutati | 10/10 | `kernel-heap-ec8a8b742bcd4f80b66705d242ecc4c7` |
| Suite host del paging, kernel, framebuffer, VirtIO, arena DMA | PASS | `run.ps1` |
| Smoke set, compresi kernel-boot (23/23) e kernel-heap | 11/11 | `qemu-run-5f66b36c34c34617ba0cef1f73eeddd3` |

## Costo misurato storico, 24 ottobre

Flush medio della console V2c, 10 boot per parte:
- prima dell'heap 8.330 tick PM, con 417 scansioni complete;
- con l'heap 9.680 tick PM (+16%), con 410 scansioni complete
  (`pointer-interactive-check-3e45f6824f144a238205d3941db590d6`).

Ogni scansione completa dell'inventario visita 4096 foglie in più. È il costo che G5 (verifica alla
costruzione più audit periodici) deve ridurre.

## Limiti dichiarati

- Capacità fissa: nessuna crescita a runtime, perché la contabilità della console richiede
  `FreePages` costante durante la sessione.
- Un use-after-free su una pagina di slab già restituita e riusata non si rileva sempre: il canary
  lo intercetta solo in parte.
- Le API del nucleo sono serializzate e testate con thread host; il backend
  nativo ammette ancora il solo BSP verificato. Mapping, backing pinned e
  vita degli oggetti restano responsabilità del chiamante. Nessuna prova SMP guest.
- Il chiamante non accede dopo free e serializza uso/rilascio. Il puntatore
  raw non distingue una vecchia allocazione da una nuova allo stesso indirizzo.
- Ripristinare IF non introduce preemption: sullo scheduler cooperativo una
  grande operazione resta lavoro della task chiamante, pur senza il lock heap.
- Nessun modulo usa ancora l'heap: i primi saranno le superfici delle finestre della GUI.
