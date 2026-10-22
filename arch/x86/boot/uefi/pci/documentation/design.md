# Acquisizione PCI UEFI per VirtIO GPU

Decisione utente del 19 ottobre 2026: risorse raccolte dal loader e copiate
nel contratto di boot; driver, mapping e code gestiti successivamente dal
kernel. Alternativa discussa: completare prima un gestore PCI nativo.
Questo blocco non introduce enumerazione generale o riallocazione PCI.

## Contratto v5

BOOT_START_INFO passa da 208 a 536 byte: il prefisso resta identico,
KERNEL_VGPU_BOOT occupa 328 byte all'offset 208. Include presence,
segment/bus/device/function, reserved, snapshot config da 256 byte e sei
dimensioni BAR. Nessun puntatore UEFI entra nel contratto.
La pagina del contratto rimane R/NX, con le protezioni esistenti.
Loader e kernel devono essere aggiornati insieme: v4 e size208 sono respinti.

Profilo v0: esattamente una GPU moderna 1AF4:1050, segmento zero,
bus0..255/device0..31/function0..7; BAR MMIO 32/64 bit con traduzione
host/PCI nulla. GPU assente, ambigua o non supportata lascia il descrittore
interamente zero e conserva il GOP. Un errore di I/O inatteso o cleanup
incompleto impedisce di pubblicare il risultato.

## Ownership, limiti e stati

Un owner inizialmente zero e usato esclusivamente prima di ExitBootServices.
LocateHandleBuffer(ByProtocol=2) produce al massimo 256 handle accettati;
il buffer viene sempre rilasciato. Pci.Read(Uint8=0) legge una fotografia
configurazione; GetLocation restituisce BDF, GetBarAttributes descrive i BAR.
Il firmware resta nel confine di fiducia del boot: i buffer restituiti con
successo devono rispettare le allocazioni e il layout promessi dal protocollo.
L'API UEFI non restituisce la capacita allocata del buffer Resources.

Viene accettato un QWORD memory descriptor seguito da EndTag, verificando
tag, lunghezza, granularity, traduzione, base, dimensione e checksum se attivo.
I descrittori frammentati e la traduzione non nulla sono fuori profilo.
Le dimensioni sono confermate contro i BAR e il parser C/Rust precedente.
AddrRangeMax non viene usato per dedurre base o dimensione: EDK2 lo valorizza
con l'allineamento; si usano Minimum e Length con overflow controllato.

Un BAR dichiarato non disponibile da UEFI e saltato con dimensione zero.
Questo e ammesso soltanto se il parser trova comunque tutte le quattro
regioni VirtIO necessarie. Non si deduce una dimensione da QMP, da un
indirizzo hardcoded o scrivendo tutti uno nel BAR.

La fotografia di 256 byte e filtrata: header standard e metadati delle
capability, byte non letti azzerati. Non e un dump indiscriminato. In
particolare PCI_CFG type5+16..19 e una finestra di accesso al BAR: non viene
letta. Anche la rilettura e filtrata e deve essere identica prima di pubblicare.
I buffer dei descrittori vengono liberati prima del buffer handle. Se FreePool
fallisce, l'owner conserva il puntatore per retry e non pubblica metadati.
Gli output indefiniti delle chiamate firmware fallite non vengono adottati.
La cleanup del loader ferma il passaggio al kernel se il rilascio resta fallito.

Sequenza non eseguibile: ZERO → ENUMERAZIONE → CATTURA LOCALE → VERIFICA
RILETTURA → RILASCIO → PUBBLICAZIONE. Errori → RILASCIO; rilascio fallito
→ OWNER TRATTENUTO, senza avanzamento. Stage e bitmap BAR non disponibili
sono diagnostica dell'owner, non parte del contratto persistente.

Il refresh della memory map e ciascun retry di ExitBootServices preservano
e rivalidano i metadati PCI. Il kernel li copia nel contesto proprio e li
valida nuovamente; stampa gli indirizzi delle quattro regioni senza accedervi.
La scoperta non autorizza ancora MMIO o DMA: il mapping posseduto e il gate
successivo, con verifica delle collisioni fisiche e degli attributi di cache.

## Fonti primarie

- [UEFI 2.10, 14.4.1/8/16/18](https://uefi.org/specs/UEFI/2.10/14_Protocols_PCI_Bus_Support.html):
  ABI PCI_IO, GUID 4cf5b200-68b8-4ca5-9eec-b23e3f50029a,
  Pci.Read, GetLocation e GetBarAttributes. Tabelle 14.10/11:
  QWORD tag8A, len2B, type+3, granularity+6, host minimum+0E,
  translation+1E, length+26, EndTag79 al byte2E.
  Resources e allocato per il chiamante, che deve eseguire FreePool.
- [UEFI 2.10A, Boot Services](https://uefi.org/specs/UEFI/2.10_A/07_Services_Boot_Services.html):
  tabella EFI_BOOT_SERVICES e LocateHandleBuffer; nessuna chiamata Boot
  Services dopo l'uscita riuscita. Offsets x64 verificati con static_assert:
  LocateHandleBuffer312, Pci.Read48, GetLocation112, GetBarAttributes128.
- [EDK2 edk2-stable202508 PciIo.c](https://raw.githubusercontent.com/tianocore/edk2/edk2-stable202508/MdeModulePkg/Bus/Pci/PciBusDxe/PciIo.c):
  confronto del layout e semantica di PciIoGetBarAttributes; Minimum host,
  Length e Max valorizzato con Alignment. Implementazione non copiata.
- [VirtIO 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
  sezioni 4.1.2 e 4.1.4: identita e regioni del trasporto PCI.

## Criteri di accettazione

Mock host con ASan, accounting allocazioni, fallimenti e retry; parser Rust
debug/release e ABI C/Rust; suite host kernel e suite loader. Dieci boot
consecutivi dell'immagine finale con acquisizione UEFI presente e validazione
kernel v5, indirizzi uguali a uno snapshot QMP/QTest indipendente, Esc/exit
e budget fisico ripristinato. Profilo VGA ordinario deve continuare con
metadati assenti. Test e script restano separati dai sorgenti di produzione.

Nessuna prova di reset GPU, virtqueue, scanout, cursor queue o DMA appartiene
a questo blocco. Nessuna promessa di fluidita aggiuntiva; display ancora GOP.
