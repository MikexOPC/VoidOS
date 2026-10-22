# Validazione discovery PCI — 19 ottobre 2026

Checkpoint del primo blocco, prima del contratto PCI v5. Per l'integrazione
successiva vedere il
[progetto del loader](../../../../../../arch/x86/boot/uefi/pci/documentation/design.md).

Esito: primo blocco verificato come parser host. Non e un driver guest
operativo e non corregge ancora la fluidita del puntatore.

## Risultati finali

- MSVC /W4 /WX, C/Rust ABI con AddressSanitizer: 477.762 verifiche PASS,
  incluse 65.536 mutazioni indipendenti a un byte dello snapshot.
- Rust: due test in debug e gli stessi due in release, overflow checks
  attivi; Clippy sia per validatori sia per gateway FFI, zero warning.
- cppcheck 2.22.0: zero error/warning/style/performance/portability;
  restano informazioni sugli header standard non forniti e sui checker attivi.
- Runner verificato anche in Windows PowerShell 5.1, con policy di esecuzione
  bypassata soltanto per quel processo; nessuna policy persistente modificata.
  XML cppcheck acquisito con redirect del processo, evitando NativeCommandError.
- Nove sorgenti, massimo 165 righe; massimo tre sorgenti diretti per cartella.
  LF, include/path Rust e link documentali controllati. Nessun TODO/FIXME/HACK/XXX.
- Dieci boot consecutivi finali QEMU/OVMF con virtio-vga e kernel invariato:
  KERNEL_POINTER_READY, snapshot PCI doppi identici, parsing host riuscito,
  Esc/exit, KERNEL_READY e budget fisico ripristinato in ogni boot.

QEMU installato: 11.1.0, build v11.1.0-12130-ge470268ff4, q35/TCG,
una vCPU, 128 MiB. Sei capability PCI; regioni selezionate:
common `0xc000001000`, notify `0xc000003000`, ISR `0xc000001800`,
device `0xc000002000`. La BAR memoria a 64 bit supera i 4 GiB.
Nessuna lettura ISR, nessuna attivazione DMA o virtqueue da questo codice.

## Evidenze locali

- `D:/OS/kernel/build/virtio-pci-host.log`, directory `virtio-pci-host/`.
- `D:/OS/kernel/build/virtio-pci-host-ps51.log`, directory `virtio-pci-host-ps51/`.
- `D:/OS/kernel/build/virtio-pci-structure.json`: sorgenti, righe e hash.
- Campagna finale: `D:/OS/kernel/build/virtio-pci-final-qemu-verified.log`.
- `D:/OS/kernel/build/virtio-pci-firmware-e4550cdbe1f94d7c98aeedaa800ed2ec/`:
  results.json e, per ogni boot, argomenti, snapshot binari, PCI QMP,
  parser.log, serial.log, qtest.log, monitor.log e stderr.log.

Hash identici nei dieci record finali:

| Artefatto | SHA256 |
| --- | --- |
| Kernel | 92C5095EE3BB43B0DF8994B8520AA42F0374BB55BA11E7C0C0DD47D2B3E4EAA9 |
| Loader | 3FD5B06E8891A16176FFCE1B197607EA31841B5EF9B231B33D3E4254D3FC9FAA |
| Parser host | D8FCEA378DF2960040A239B14B65BBF2972C5317D868458DA95D7F340AF805DE |

La campagna preliminare virtio-pci-firmware-a49eed43823a47f5b4eb356dfa2b7c80
passa dieci boot, ma una ricompilazione del parser avviene durante quella
campagna: non e usata come prova dell'identita finale. Due tentativi iniziali
non passano: connessione locale negata nel sandbox, poi runtime ASan non
presente nel PATH del figlio. La connessione locale autorizzata e il PATH
limitato al processo di test risolvono questi problemi del collaudo.

## Limiti e review

Il parser non alloca: ASan non segnala accessi invalidi, ma non dimostra
assenza di leak fisici del futuro driver. L'accounting guest riguarda il
percorso console gia esistente. Valgrind non eseguito, coverage linee/branch
N/D. Nessuna prova su hardware fisico, DMA, interrupt GPU, reset, scanout
VirtIO o cursore del dispositivo; nessuna misura FPS o promessa di frame atomici.

Review manuale: output pubblicato solo al successo; controlli overflow e
limiti prima degli accessi; nessun puntatore FFI in Rust; nessun accesso
hardware del modulo; nessuna modifica a reuse/ o al renderer corrente.
Il prossimo blocco richiede il backend PCI guest e il mapping posseduto,
con relativo progetto, test e dieci boot della nuova immagine.
