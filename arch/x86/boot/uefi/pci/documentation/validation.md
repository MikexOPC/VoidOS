# Verifica acquisizione e handoff PCI v5 — 19 ottobre 2026

Checkpoint precedente al mapping GPU e alla lettura selettiva dei metadati
PCI. Le campagne sotto restano evidenze di quelle immagini; non dimostrano
l'esclusione della finestra indiretta PCI_CFG. Lo stato successivo, con
acquisizione13024 e test specifici della finestra, e nel
[report mapping](../../../../../../kernel/drivers/video/virtio/mapping/documentation/validation.md).

Esito: acquisizione UEFI e validazione dei metadati nel kernel funzionano.
Non sono ancora implementati mapping MMIO posseduto o attivazione GPU.

## Prove finali

| Controllo | Risultato |
| --- | --- |
| Parser C/Rust con ASan | 477.762 verifiche PASS |
| Acquisizione UEFI con ASan/accounting mock | 12.848 verifiche PASS |
| Rust debug/release, overflow checks e Clippy | PASS |
| Runner acquisizione Windows PowerShell 5.1 | PASS |
| Build/suite loader e Rust del loader | PASS |
| Suite host kernel, contratto v5 e output invariato nei rifiuti | PASS |
| Snapshot PCI presente preservato nei retry ExitBootServices | PASS, confronto di tutti i 328 byte |
| QEMU virtio-vga, nuova immagine v5 | 10/10 boot consecutivi PASS |
| QEMU VGA ordinaria, stessa immagine v5 | 10/10 boot consecutivi PASS |

I test di retry host coprono uno, due e tre tentativi di uscita, oltre ai
percorsi di errore preesistenti: i 328 byte PCI restano identici dopo il
refresh della mappa. Nessuna allocazione o chiamata firmware vietata nel
percorso ristretto. `-HostTestsOnly` nel builder loader permette di ripetere
la suite DLL senza sostituire l'EFI gia verificato; hash finale invariato.

cppcheck: nessun error/warning/performance/portability bloccante. Restano
informazioni sugli header di sistema non forniti e suggerimenti style
constParameterCallback nei mock: le firme rimangono quelle dell'ABI UEFI,
senza cast di function pointer per soddisfare il suggerimento.

## Riscontro reale e correzioni

Il primo smoke v5 avviava il kernel ma riportava PCI assente: OVMF restituiva
EFI_UNSUPPORTED per BAR4. Il codice rifiutava anche un BAR opzionale.
Ora registra la bitmap `VIRTIO_PCI_UNAVAILABLE_BARS=0x10` e prosegue soltanto
se tutte le quattro regioni richieste risultano valide nei BAR disponibili.
Il mock verifica separatamente BAR opzionale assente e BAR necessario assente.

Con virtio-vga il kernel stampa quattro indirizzi identici a quelli ricavati
dal parser host su due snapshot QTest uguali, con dimensioni fornite da QMP:
common `0xc000001000`, notify `0xc000003000`, ISR `0xc000001800`,
device `0xc000002000`. Il guest non usa QMP: acquisisce dati tramite UEFI.
Con VGA ordinaria il descrittore resta zero e la console prosegue sul GOP.

Ogni boot usa ESP e variabili OVMF isolate, verifica presence/absence,
versione5/size536, KERNEL_POINTER_READY, Esc, exit, KERNEL_READY e budget
fisico restituito. Nessun crash o fail-stop inatteso osservato in questi
venti boot. Il profilo VGA non esegue il parser host su una GPU assente:
l'ultima riga generica dei log precede la correzione del solo testo riepilogativo.

L'ampliamento del contratto ha anche esposto due dipendenze dei test:
un fixture locale superava la soglia dello stack probe MSVC ed e diventato
statico; la suite kernel ora collega il runtime memoria gia usato dal kernel,
necessario ai memset emessi dal compilatore. Non sono state disabilitate
le guardie dello stack ne introdotti stub che ignorano il probing.

## Evidenze locali

- `D:/OS/kernel/build/virtio-pci-host.log` e `virtio-pci-host/`.
- `D:/OS/kernel/build/virtio-uefi-host.log` e `virtio-uefi-host/`.
- `D:/OS/kernel/build/virtio-uefi-host-ps51.log` e `virtio-uefi-host-ps51/`.
- `D:/OS/kernel/build/virtio-uefi-loader.log`, `virtio-handoff-kernel.log`.
- `D:/OS/kernel/build/virtio-handoff-host.log`: suite kernel.
- `D:/OS/kernel/build/virtio-handoff-retry-host.log`: retry con PCI presente.
- `D:/OS/kernel/build/virtio-handoff-final-qemu.log`, directory
  `virtio-pci-firmware-7c32031784a645fd8f0af4d43049250d/`.
- `D:/OS/kernel/build/virtio-handoff-fallback-qemu.log`, directory
  `virtio-pci-firmware-1eec5630c1974f0990ec6ad0bd053190/`.
- `D:/OS/kernel/build/virtio-pci-structure.json`: sorgenti, righe e hash.

Le directory delle campagne contengono results.json e log seriale, monitor,
stderr, argomenti e inventario PCI per ogni boot; il profilo VirtIO include
anche i binari snapshot e parser.log. QEMU11.1.0, q35/TCG, una vCPU/128MiB.

| Artefatto verificato | SHA256 |
| --- | --- |
| Kernel ELF | EBAD3093C3BB3364E9ED0724062F9C70541C29958F116F89C7946E59E575038E |
| Loader EFI | 6CCC25390C004630CF7864BF7E2489A3FCCF68A063884CE6482CC804AB0CB97E |
| Parser host | 697EA766A4696B5649EC65AE2B9162F68094DB104AAFF88DBC23518EC91BEBF7 |

## Limiti

ASan/accounting riguardano i mock host, non il firmware reale. In QEMU il
budget del kernel e ripristinato; non e un leak detector del pool OVMF.
Valgrind non eseguito; coverage linee/branch N/D; hardware fisico non provato.
Nessun nuovo DMA, reset, interrupt GPU, scanout o cursor queue attivato.
Nessun miglioramento FPS o eliminazione del tearing dimostrato.

Il prossimo blocco e il mapping posseduto: rivalidazione PCI live prima
degli accessi, collisioni fisiche, cache policy, lease, rollback e cleanup.
La sola struttura v5 valida non autorizza accessi MMIO.
