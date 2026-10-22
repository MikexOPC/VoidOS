# Mapping VirtIO GPU v0

Decisione utente: una GPU, massimo16 pagine UC, lease pinned, guardie e
rollback; sono autorizzati i necessari adattatori all'inventario paging,
framebuffer, dispatcher/watchdog e test. Non e un registro MMIO generale.

Il contratto v5 resta invariato. C conserva ownership e transazioni;
Rust calcola l'unione ordinata delle pagine delle quattro capability.
Le pagine fisiche condivise sono mappate una volta sola. Il piano contiene
16 indirizzi e quattro offset virtuali; nessuna allocazione o puntatore
attraversa l'FFI Rust, che scambia record repr(C) per valore.
La dimensione e verificata in C e Rust e con un test ABI reale MSVC/Rust.

Una registrazione GPU singola, privata al paging, conserva il riferimento
all'owner e un callback nativo di verifica CPU/root/PAT. E modificata solo
nel profilo BSP/IF=0: non e sincronizzazione SMP. L'inventario copia piano
e lease con ID, riconosce le tre sorgenti tabelle e vieta alias delle pagine
MMIO anche fuori dal ramo GPU. Il callback e richiamato nelle verifiche
correnti, non sostituito da un'autorizzazione memorizzata fra scansioni.

Il ramo proposto e PML4[226], con prima pagina e pagina dopo Count come
guardie, tre pagine di tabelle RAM da lease pinned. Nessuna pagina grande,
user o eseguibile. Le capability condivise a livello di pagina non possono
avere guardie individuali: i futuri accessor devono verificare offset e width.

Sequenza non eseguibile: rivalidare PCI live → ricostruire piano → verificare
RAM, risorse vive e alias → allocare/pinnare tabelle → costruire detached →
pubblicare ramo → invalidare traduzioni → acquisire inventario completo.
Rollback: revocare ramo → invalidare → ricatturare inventario precedente →
togliere pin e rilasciare. Se revoca o invalidazione falliscono, trattenere
owner e lease. Non restituire risorse ancora raggiungibili dalle PTE.

Il profilo resta BSP/IF=0 durante le mutazioni, senza AP o PCID; nessun
protocollo di shootdown SMP viene dichiarato. L'inventario deve riconoscere
e validare GPU, framebuffer e LAPIC prima di poter conservare il mapping
mentre console e task sono attive. Non e accettabile saltare la scansione.
Il budget del renderer include le tre tabelle GPU oltre alle proprie e agli
stack; la scansione completa successiva continua a verificare i lease pinned.
L'attivazione avviene prima dell'ingresso nel ciclo interattivo. L'unbind
richiede task ferme e precede quello del framebuffer; non e hot-unplug.

Fonti primarie consultate:

- [VirtIO1.2, 4.1.4 e 4.1.4.5](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html):
  MMIO capability da BAR, offset/lunghezza; ISR read-clear, da non leggere
  nel collaudo del mapping. Nessun reset, negoziazione o DMA in questo blocco.
  §4.1.4.9: PCI_CFG type5 espone pci_cfg_data al byte16, che inoltra accessi
  al BAR. Loader, controllo PCI nativo e QTest leggono soltanto header e
  metadati, lasciando azzerati payload sconosciuti e finestra di accesso.
- [Intel SDM3A 090US, febbraio2026](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):
  5.5.2 PTE4KiB P0/RW1/U-S2/XD63; 14.12.2/.3 PAT UC0/WB6,
  selettore PAT7/PCD4/PWT3; 5.10.4.1 invalidazione tramite stesso CR3
  quando PCIDE e disabilitato. Nessuna programmazione PAT o MTRR.
- [Linux device I/O](https://docs.kernel.org/driver-api/device-io.html):
  separazione fra mapping e accessor, necessita di bounds e ordering;
  riferimento di progetto, senza copiare l'implementazione.
- [QEMU11.1.0 PCI host](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/pci/pci_host.c):
  selezione config bit31, bus23:16, devfn15:8; fonte per rivalidazione
  segmento0 via CF8/CFC, senza scrivere dati PCI o BAR.

Criteri: piano/Rust in debug e release con overflow checks, C/Rust sotto
ASan, errori e output non pubblicato; test di binding/rollback con contatori
reali, lease stantii, alias, corruzioni e revoca fallita; dieci boot finali
con lettura indipendente PTE e guardie, console attiva e budget ripristinato.
Finche questi gate non sono completati il mapping non e dichiarato attivo.
