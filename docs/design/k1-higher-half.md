# K1: kernel nella metà alta

25 ottobre 2026. Design senza codice, da approvare. Attua la decisione D1-A di
[G7](g7-smp-user.md): tutto il kernel negli slot PML4 256–511, la metà bassa (0–255) libera per i
processi utente.

## 1. Stato attuale (rilievo nel codice)

| Regione | Indirizzo o slot oggi | Dove è scritto |
|---|---|---|
| Immagine del kernel | `0x1_0000_0000` (slot 0), finestra di codice valida fino a `0x1_0010_0000` | `/base:` in `kernel/scripts/build.ps1` e `build-kernel-probe.ps1`, `convert-image.ps1`, controlli di RIP in `interrupts/return/table.c`, `stack/main.c`, `scheduler/mapping`, `exceptions/rust/frame/validation.rs`, runner Python e PowerShell (24 file in tutto) |
| Mappa memoria, contratto di boot | `0x5000_0000_0000`, `0x6000_0000_0000` | `arch/x86/configs/boot_profile.h` |
| Stack di boot, finestra di pagina, arena, LAPIC, stack dei task | `0x7000_0000_0000` (slot 224) | `boot_profile.h`, `mm/devices/lapic.h`, scheduler |
| Vista ricorsiva | slot 128 | `X86_BOOT_RECURSIVE_SLOT` |
| Framebuffer, GPU, superficie, IOMMU, DMA, heap | slot 225–230 | un `authority.h` per regione |

Inoltre, circa 38 file controllano "indirizzo nella metà bassa canonica" (`< 1 << 47`) come
invariante del kernel, in C, Rust, PowerShell e Python. Dopo K1 quell'invariante si rovescia.

## 2. Layout proposto

| Slot PML4 | Regione | Note |
|---|---|---|
| 0–255 | **utente** | Vuoti al termine del boot; verificato dall'inventario |
| 256–383 | riservati | Una futura mappa diretta della memoria fisica, se servirà (fuori da K1) |
| 479, 480 | finestre di boot: mappa memoria (479), contratto (480) | Erano a `0x5000…` e `0x6000…`; due slot invece di uno, come prima, per non cambiare la struttura delle tabelle del loader |
| 481–486 | framebuffer, GPU, superficie, IOMMU, DMA, heap | Erano 225–230; ogni regione resta in uno slot proprio |
| 488 | stack di boot, finestra di pagina, arena, LAPIC, stack dei task | Era 224 |
| 510 | vista ricorsiva | Era 128 |
| 511 | **immagine del kernel** a `0xFFFF_FFFF_8000_0000` | Gli ultimi 2 GiB: è il layout classico |

Le regioni conservano dimensioni e struttura interna: cambia solo lo slot.

## 3. Scelte

### L1: come migrare
- **A (consigliata):** a tappe, dietro un header unico `arch/x86/configs/layout.h` che definisce
  ogni slot e ogni intervallo.
  1. **Refactor senza cambiare comportamento.** Ogni indirizzo scritto a mano usa l'header, e
     i controlli "metà bassa" diventano predicati con un nome (`x86_kernel_range`,
     `x86_user_range`), anche nelle copie Rust. Prova: le stesse immagini, gli stessi marker,
     smoke set.
  2. **Regioni dei device e heap** (225–230 → 481–486).
  3. **Finestre di boot, stack e arena** (224 e `0x5000…`/`0x6000…` → 480 e 488).
  4. **Vista ricorsiva** (128 → 510).
  5. **Immagine del kernel** (→ slot 511).

  Ogni tappa è un commit con lo smoke set e si può annullare da sola. È la regola di migrazione
  del progetto: la scelta del layout vive in un solo posto.
- **B:** tutto in un passo. Meno lavoro totale, ma un fallimento non dice quale delle cinque
  parti l'ha causato.

### L2: indirizzo dell'immagine
- **A (consigliata):** `0xFFFF_FFFF_8000_0000`. Codice e dati restano nei 2 GiB più alti, quindi
  vale ancora il modello di codice "small/kernel".
- **B:** l'inizio della metà alta (`0xFFFF_8000_0000_0000`). Lascia la parte alta per altro, ma
  con MSVC non porta vantaggi.

Prerequisito da verificare per primo, con un prototipo: `link.exe` con `/base:` a 64 bit alto e
`/fixed` deve produrre solo rilocazioni compatibili (ADDR64, REL32, ADDR32NB). Una rilocazione
ADDR32 assoluta fallirebbe (LNK2017). Anche `convert-image.ps1`, che converte da PE a ELF, deve
accettare la nuova base.

### L3: indirizzi nei runner di test
- **A (consigliata):** i runner leggono gli intervalli dal log seriale (`KERNEL_LAYOUT_*`, stampati
  una volta al boot) invece di avere costanti proprie. È lo schema già usato in G2 per i controlli
  sul firmware.
- **B:** un `layout.py` e un `layout.ps1` generati dall'header durante la build.

### L4: passaggio dal loader al kernel
La pagina di transizione resta identica in mappatura fino al salto, come oggi. Il kernel la
rimuove all'ingresso e da lì la metà bassa è vuota. Da verificare nel loader prima della tappa 5.

## 4. Fuori da K1
- Spazi di indirizzi per processo e copie da e verso l'utente: K5 e K6.
- KPTI: deciso in K5, ma il layout lo rende possibile senza un altro refactor.
- Mappa diretta della memoria fisica: solo se una misura la giustifica.

## 5. Criteri di accettazione
1. Alla fine del boot nessuna voce PML4 sotto lo slot 256 è presente. Lo verifica l'inventario e
   lo dichiara un marker (`KERNEL_LOWER_HALF_EMPTY`).
2. RIP, stack, IST e tutte le regioni stanno nella metà alta, con controlli che usano i predicati
   di `layout.h`.
3. Il test di manomissione, l'audit G5 e gli inventari funzionano con i nuovi slot.
4. Smoke set completo a ogni tappa; alla fine V2c ×10 e kernel-boot (23 scenari, compresi #DF,
   NMI e guardie).
5. Nessun indirizzo del kernel scritto a mano fuori da `layout.h`: un controllo nello script di
   stile lo cerca.

## 6. Decisioni da approvare
L1-A (a tappe dietro `layout.h`), L2-A (immagine a `0xFFFF_FFFF_8000_0000`), L3-A (intervalli dal
log seriale), il layout del §2 e i criteri del §5.

## 7. Avanzamento

| Tappa | Stato | Prove |
|---|---|---|
| Prototipo del linker | `link.exe` con `/base:0xFFFFFFFF80000000 /fixed`: nessun errore di rilocazione, image base ed entry point nella metà alta | `kernel/build/k1-link-probe` |
| 1. `layout.h` senza cambi di comportamento | Fatta (b19e4d0). Codice generato uguale salvo la maschera dello slot (+16 byte in 5 funzioni) | suite host, smoke 12/12 |
| 2. Regioni dei device e heap negli slot 481–486 | Fatta. Il paging in Rust attraversa anche la metà alta (salvo lo slot ricorsivo) ed estende il segno degli indirizzi foglia | suite host 35/36 (virtio-discovery: `read.c` dell'utente), smoke 12/12 (`qemu-run-2a75b8781a424ee08ca8a8dd3c281778`), QI e inizializzazione VirtIO 1/1 |
| 3. Finestre di boot, stack e arena | Fatta. Mappa memoria 479, contratto 480, finestre (stack di boot, finestra di pagina, IST, stack principale, arena, LAPIC, stack dei task) 488. `X86_LAYOUT_RANGE` sostituisce i controlli "metà bassa" sugli oggetti del kernel; il loader verifica e costruisce anche la metà alta | suite host 35/36 (virtio-discovery: `read.c`), smoke 11/12 più kernel-boot rieseguita dopo la correzione dei runner |
| 4. Vista ricorsiva | Fatta. Slot 510; il paging in Rust applica la maschera allo slot ed estende il segno della vista (copia di `RECURSIVE` confrontata a ogni build) | suite host 35/36 (virtio-discovery: `read.c`), smoke 12/12 con kernel-boot (`qemu-run-6ce98a06b7cd4871884a2265414376ed`) |
| 5. Immagine del kernel | Da fare; da allargare la finestra di 1 MiB (immagine già a 692 KiB) | |

Deviazione da L3-A: i runner leggono `layout.h` (`scripts/lib/layout.ps1` e `layout.py`) invece del log
seriale. La fonte resta unica e serve anche prima del boot (`/base:` del linker).

Trovati durante la tappa 2 e non causati da K1:
- Il runner QI rifiutava una coda sotto 1 MiB, che è possibile da B8 in poi: corretto.
- La campagna del mapping VirtIO (`virtio/scripts/firmware.py`, build `-TestVgpuMapping`) aspetta
  `KERNEL_POINTER_READY`, e fallisce allo stesso modo anche sul commit precedente a K1: da sistemare a parte.
