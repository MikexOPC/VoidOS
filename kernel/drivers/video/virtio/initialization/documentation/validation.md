# Reset e negoziazione GPU: verifica — 19 ottobre2026

[Contratto scelto e fonti hardware](design.md). Una GPU QEMU moderna,
profilo diagnostico seriale isolato. L'immagine ordinaria non resetta
la GPU: conserva framebuffer firmware, tastiera, mouse e console.

## Implementazione

Common MMIO ricavato dalle capability PCI e dal piano del mapping posseduto.
Verifica completa inventario prima/dopo, owner live e CPU verificati ad ogni
accesso. Rust no_std valida larghezze/registri e seleziona solo VERSION_1;
C gestisce reset, stato crescente e lettura di conferma FEATURES_OK.
Nessun DRIVER_OK, coda, indirizzo DMA, notifica, ISR read-clear o MSI-X.

Accessi scalari volatile su UC forte e barriere del compilatore. La
proprieta di ordinamento CPU deriva da UC verificato, non da volatile.
Il trasporto e un prestito sincrono immutabile sul BSP con IF=0; niente
concorrenza/SMP. L'owner del mapping resta persistente per trattenere le
risorse in caso di errore. Ogni reset richiede status zero osservato e
clock progredito: deadline357955 tick PM e massimo1000000 campioni.
Non e una latenza hardware universale o garanzia wall-clock dell'host.

Cleanup diagnostico: eventuale FAILED preserva uno stato letto di nuovo,
senza usare una copia stantia; reset, conferma zero, inventario, unbind e
controllo budget. Un reset non confermato lascia il mapping posseduto.
Un rifiuto dopo cleanup resta terminale nel profilo diagnostico.

## Host e build

- Protocollo C/Rust:89300 verifiche,100 cicli; ogni posizione di errore IO,
  feature mancanti/rifiutate, NEEDS_RESET, reset ritardato, clock fermo,
  deadline e wrap24/32bit. Passano MSVC /W4 /WX e ASan C.
- Backend nativo con CPU/PCI/window mock:61900 verifiche; larghezze e
  registri vietati senza accesso, ownership, indirizzo modificato,
  output sovrapposto, clock e callback di inventario.
- Due test Rust sia debug sia release con overflow checks e Clippy.
  FFI scalare reale MSVC/Rust; nessun puntatore attraversa Rust.
- cppcheck senza error/warning/performance/portability bloccanti. Note
  style e header toolchain mancanti restano distinte dai warning compilatore.
- Log host: `kernel/build/virtio-init-host-final.log`. Runner5.1 isolato:
  `kernel/build/virtio-init-ps51.log`.
- Build: `kernel/build/virtio-init-build-final.log` e
  `kernel/build/virtio-init-ordinary-build.log`. Il MAP ordinario deve
  escludere probe, trasporto e protocollo di inizializzazione.

Il MAP ordinario esclude effettivamente probe, trasporto e protocollo.
Disassemblato l'oggetto del trasporto: accessi MMIO emessi come mov/movzx
scalari byte/dword, senza SIMD sul dispositivo. Evidenza:
`kernel/build/virtio-init-native-disasm.txt`; gli usi SIMD su RAM nello
stesso oggetto riguardano inizializzazione/copia di record locali.

## QEMU finale

QEMU11.1.0 build v11.1.0-12130-ge470268ff4, q35/TCG, una CPU,128MiB,
OVMF e ESP isolata. Due canali locali QMP/QTest con timeout; deadline
avvio80s, nessun processo estraneo terminato. Copie ESP verificate con SHA256.

- 10/10 boot diagnostici positivi consecutivi sulla stessa immagine:
  `kernel/build/virtio-initialization-firmware-f46a7bb7164e44afbc35c6e71ece3374/results.json`.
- 10/10 boot diagnostici con VGA standard, GPU assente e budget invariato:
  `kernel/build/virtio-initialization-firmware-c12457a57e0a455d9120efe01543b6be/results.json`.
- Un test negativo finale con iommu_platform=on: QEMU richiede
  ACCESS_PLATFORM, non supportato da questa selezione minima. Rifiuto
  atteso FEATURES_OK, FAILED, reset zero e rilascio completo verificati:
  `kernel/build/virtio-initialization-firmware-3d5bbfd04b2742c2b28988dceb4da021/results.json`.
  Il panic terminale successivo e intenzionale; non e un boot positivo.
- 10/10 boot ordinari consecutivi con console, tastiera, movimento mouse,
  mapping GPU persistente e budget temporaneo restituito:
  `kernel/build/virtio-mapping-firmware-743339f040c542dca4949e021a741029/results.json`.

Totale finale30 boot positivi,10 consecutivi per profilo, piu un rifiuto
intenzionale verificato separatamente. Nessun fault inatteso nelle tre
campagne positive. Queste prove non certificano una sessione grafica fluida.
Struttura64 sorgenti, entro400 righe e cinque sorgenti diretti per cartella;
include/link e LF passano. Main283 righe, builder384. Manifest
`kernel/build/virtio-pci-structure.json`; diff dei percorsi coinvolti pulito.
Test Rust centralizzati in test/kernel/.../initialization/rust/policy/.
Lo spostamento del solo #[cfg(test)] lascia l'oggetto Rust identico:
SHA256 `A51506814BEF0E6390E5CAE9A89741EA96EB099982FF3D849E0C4C9C167EB04E`
sia nel kernel collaudato sia nella ricompilazione host finale.

La traccia indipendente virtio_set_status conferma richieste0,1,3,11,0
(duplicati zero del reset esclusi), senza DRIVER_OK nella sequenza guest.
Il guest verifica la lettura11; la traccia della sola scrittura11 non
dimostrerebbe l'accettazione. Nel test negativo la scrittura11 e registrata,
ma il readback resta3 e la traccia prosegue131,0. QTest legge lo status
finale0 dal common ricavato indipendentemente dai BAR, e PML4[226] assente
da RAM ordinaria. Budget prima/dopo identico,20458 pagine in questo profilo.

SHA256 kernel diagnostico:
`8E6E8B5BB4E918637F1E253BE2F1713030B76DFC4F477936525526B4110B5035`.
SHA256 kernel ordinario:
`F2FCDA942113ECF1E68CABF8212683A98A7B065ED508218B444B8F8D7CDA2E89`.
SHA256 loader, invariato dal blocco mapping:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.

ASan strumenta C host, non Rust, MMIO o RAM fisica. Questi moduli non
allocano heap; il mapper preesistente possiede tre lease di tabelle.
Valgrind non eseguito/disponibile; coverage linee/branch N/D.

## Debug documentato

Primo boot fallito prima di qualsiasi accesso GPU: il probe usava la
cattura framebuffer, ma il diagnostico non inizializza il video. Corretto
con capture_lapic, che verifica comunque l'autorita GPU registrata.
L'immagine non viene proseguita dopo il panic. Evidenza precedente:
`kernel/build/virtio-initialization-firmware-0ee46f5a68c8445686962402a522869e/`.
Il primo harness aspettava la deadline per quel panic; ora riconosce
direttamente il marker panic e termina soltanto la VM posseduta.

La prima compilazione ha rilevato codice irraggiungibile dopo uno stop
anticipato nel main: separati i rami con il preprocessore. Corretti
anche firma ABI di un mock e formato __LINE__ nei messaggi dei test.
Non sono stati disabilitati warning, overflow checks o controlli statici.

## Limiti

Il reset puo eliminare le superfici GPU; non si riprende il GOP come se
fosse stato ripristinato. La console ordinaria resta una regressione distinta.
Ancora assenti: code/DMA, scanout VirtIO, cursore hardware, gestione GPU
di IRQ, hot-unplug, hardware fisico e IOMMU. Nessuna nuova promessa di
fluidita, FPS, isolamento da un dispositivo malevolo o accelerazione3D.
