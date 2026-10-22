# VirtIO GPU: primo blocco, discovery da snapshot PCI

## Decisione e perimetro

Direzione scelta dall'utente: driver video specifico per QEMU, mantenendo
il framebuffer firmware per bootstrap e fallback prima del takeover.
Il dispositivo v0 e `virtio-vga`, identita moderna `1AF4:1050`.
Non si implementano driver Intel/AMD/NVIDIA, accelerazione 3D o Vulkan.

Il primo blocco conteneva soltanto un parser senza accessi hardware: non era
ancora enumerazione PCI nel kernel, mapping MMIO, negoziazione VirtIO,
virtqueue, scanout o cursore gestito dal dispositivo. Ora il parser e linkato
nel loader e nel kernel per il
[contratto PCI v5](../../../../../../arch/x86/boot/uefi/pci/documentation/design.md).
`KERNEL_VGPU_PCI_READY` significa layout riconosciuto, non GPU avviata.

## Contratto e ownership

Il chiamante presta uno snapshot stabile di 256 byte e sei estensioni BAR
gia acquisite da una sorgente affidabile. Il parser non misura i BAR
scrivendovi tutti uno. Le dimensioni fornite non dimostrano ownership:
la futura integrazione deve acquisire risorse e lease attraverso il kernel.

Output allineato a otto byte, separato da entrambi gli input. Nessuna
allocazione, stato globale o durata trattenuta; nessuna scrittura agli input.
La capacita dei buffer e il loro lifetime restano obbligo del chiamante C.
Gli errori lasciano l'output invariato. C esegue traversal e pubblicazione;
Rust controlla valori scalari, senza puntatori o blocchi unsafe.
L'helper region richiede base e dimensione gia accettate dall'helper BAR;
e il probe a imporre questa sequenza. Non autorizza da solo indirizzi fisici.

Sono supportati BAR memoria a 32/64 bit, con dimensione potenza di due,
base non nulla e allineata e larghezza fisica dichiarata fra 32 e 52 bit.
I/O e meta superiore di un BAR a 64 bit non sono regioni selezionabili.
Prefetchable non decide la futura politica di cache.

La lista capability e limitata a 48 nodi, con rilevamento cicli, record
vendor sovrapposti, offset disallineati e lunghezze fuori dai 256 byte.
Le capability non vendor hanno soltanto l'header controllato: non e un
validatore completo di conformita PCI o VirtIO. Tipi VirtIO sconosciuti
sono ignorati. Si seleziona la prima capability MMIO supportata per tipo;
una capability nota malformata e rifiutata anche se duplicata.

Le quattro regioni scelte devono essere disgiunte e contenute nel BAR:
common almeno 56 byte, notify 2, ISR 1, device GPU 16. Offset common/device
allineati a 4, notify a 2; multiplier notify zero oppure potenza di due
almeno 2. NOTIFICATION_DATA e le estensioni common non sono negoziati qui.

## Sequenza, non codice eseguibile

1. Validare input, identita, classe display e header endpoint.
2. Decodificare basi BAR entro i limiti dichiarati.
3. Percorrere la lista, validare e selezionare le regioni supportate.
4. Controllare completezza e sovrapposizioni delle quattro regioni.
5. Pubblicare il risultato locale soltanto dopo tutti i controlli.

Il costo e limitato: 256 byte di marcature, sei BAR, massimo 48 nodi e
sei confronti fra regioni. Non esistono retry o attese nel parser.

## Fonti primarie e dati usati

- [VirtIO 1.2 CS01, 1 luglio 2022](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html):
  4.1.2 vendor 1AF4, device 1040+tipo (GPU 16 => 1050);
  4.1.4 capability vendor 09, next +1, len +2, type +3, BAR +4,
  offset +8, length +12; 4.1.4.3 common; 4.1.4.4 notify multiplier +16;
  4.1.4.5 ISR read-clear, quindi non letto dalla discovery;
  4.1.4.6 device config e 5.7.4 configurazione GPU da 16 byte.
- [Definizioni PCI Linux, consultate il 19 ottobre 2026](https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/pci_regs.h):
  vendor/device 00h/02h, status 06h bit 4, header 0Eh, BAR 10h..24h,
  capability pointer 34h; BAR bit 0 I/O, bit 2:1 tipo, bit 3 prefetch.
  Usate come riferimento dei campi, senza copiarne implementazioni.
- [QEMU v11.1.0, trasporto VirtIO PCI](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/virtio/virtio-pci.c).
- [QEMU v11.1.0, host q35](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/pci-host/q35.c)
  e [accessi PCI](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/pci/pci_host.c):
  nel solo test host, CF8 seleziona con enable bit 31, bus 23:16,
  slot 15:11, funzione 10:8, offset 7:2; CFC viene soltanto letto.
- [Protocollo QTest](https://www.qemu.org/docs/master/devel/testing/qtest.html):
  inl/outl operano sulla VM posseduta, senza passthrough hardware host.

## Prove e limiti

`../scripts/test.ps1` verifica struttura e percorsi, warning MSVC come errori,
ABI reale C/Rust sotto AddressSanitizer, Rust debug/release con overflow
checks, Clippy e cppcheck. I test includono 65.536 mutazioni a un byte,
alias output/input, limiti e invariabilita dell'output nei fallimenti.
Le verifiche sono assert espliciti attivi anche con NDEBUG.

`../scripts/firmware.py` crea ESP e variabili OVMF isolate, avvia la VM
con virtio-vga, attende KERNEL_POINTER_READY, ferma le vCPU, acquisisce
BAR/estensioni via QMP e due snapshot identici via QTest. Ripristina CF8;
non scrive command, BAR, status o MMIO. Il parser gira sullo host sotto ASan.
Riprende poi il guest, verifica Esc/exit, KERNEL_READY e budget fisico.
Deadline socket e processi finite; termina soltanto il proprio processo.

Queste prove dimostrano parsing di una configurazione reale dell'emulatore
e compatibilita del percorso firmware, non funzionamento di un driver guest.
Valgrind non eseguito, copertura linee/branch N/D, hardware fisico non provato.
Il parser non alloca; l'accounting del kernel riguarda il percorso esistente.

## Prossimo blocco e takeover

L'acquisizione tramite loader e la validazione guest v5 precedono il
[mapping posseduto](../../mapping/documentation/design.md); seguono reset,
feature negotiation, code control/cursor, risorse 2D e aggiornamenti cursore.
Ogni passaggio richiede prove proprie: QMP non puo fornire risorse al kernel
come se fosse un bus driver. Il cursore resta software fino al completamento.

Prima del takeover, un dispositivo assente/non supportato conserva il GOP.
Dopo DMA o cambio scanout, fallback e rilascio richiedono prova di stop/reset
e ripristino del modo video; se manca, conservare risorse e diagnostica seriale.
Il solo reset VirtIO non dimostra che il framebuffer firmware sia tornato valido.
