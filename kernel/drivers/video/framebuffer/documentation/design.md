# Framebuffer UEFI v0 — progetto concordato

9 ottobre 2026. Stato: scelta B approvata; selezione GOP e handoff v4
implementati e verificati nel [report corrente](handoff-validation.md).
Mapping e rendering ancora da implementare. Modulo funzionale framebuffer; console
testuale e collegamento shell sono passi successivi, dopo i suoi gate.

Scelta successiva approvata: UC con ownership kernel e rollback.
[Contratto del mapping e prerequisiti](mapping/design.md); il preflight
numerico e implementato, il backend hardware non e ancora pubblicato.

## Stato iniziale del progetto, precedente all'handoff v4

Il contratto BOOT_START_INFO versione 3 e lungo 168 byte e contiene
memoria, ingresso, stack, paging e ACPI; non contiene un framebuffer.
L'interfaccia UEFI corrente espone console testuale, non GOP.
Il kernel non puo quindi usare una base video presunta o recuperare
funzioni GOP dopo l'uscita dai Boot Services.

Il mapper delle regioni RAM corrente accetta al massimo 32 pagine per
regione (128 KiB) e possiede lease RAM. Non e un mapper di framebuffer:
non si puo riusarlo per un intervallo video grande o per memoria device
senza un contratto distinto. Il backend LAPIC possiede una policy MMIO
verificata, ma il suo mapping di una pagina non valida il mapping video.
La fase di handoff deve quindi progettare un intervallo virtuale dedicato
e il budget delle tabelle prima di introdurre le scritture del driver.

## Fonti e confronto

- [UEFI 2.10, §12.9.2](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html#efi-graphics-output-protocol):
  GOP, formati RGB/BGR a 32 bit, informazioni del modo, base e dimensione
  del framebuffer. PixelsPerScanLine determina il passo fra righe, che
  puo differire dalla risoluzione orizzontale. PixelBltOnly non offre un
  framebuffer lineare. §12.9.2.2 descrive SetMode e i suoi errori.
  Ricerca iniziale UEFI 2.11: pagina/PDF non leggibili dal browser (403);
  il riferimento effettivamente letto e 2.10, senza presumere equivalenza
  integrale con 2.11.
- [Linux Frame Buffer Library](https://cdn.kernel.org/doc/html/latest/driver-api/frame-buffer.html),
  documentazione latest consultata il 9 ottobre: separa proprieta del
  dispositivo, geometria e operazioni, e connette la console al driver.
  La pagina non identifica un commit; non e una dipendenza compilata.
  Serve a motivare la separazione delle responsabilita, non una copia
  della struttura fb_info o del sottosistema Linux.
- [EDK2 edk2-stable202508, UefiSpec.h](https://raw.githubusercontent.com/tianocore/edk2/edk2-stable202508/MdePkg/Include/Uefi/UefiSpec.h)
  e [GraphicsOutput.h](https://raw.githubusercontent.com/tianocore/edk2/edk2-stable202508/MdePkg/Include/Protocol/GraphicsOutput.h):
  riscontro primario dell'ordine dei servizi e dell'ABI del produttore.
  Prefisso x64 Boot Services160 byte;20 slot successivi; LocateProtocol
  all'offset320, prefisso esteso328 byte. GOP information36 byte,
  mode40 byte con base all'offset24, protocollo32 byte. Static assert
  compilati verificano queste derivazioni. Nessun codice EDK2 importato;
  sole dichiarazioni interoperabili espresse nel progetto.

## Alternative da scegliere

A. Conservare il modo gia attivo, accettare soltanto RGB/BGR 32 bit,
rendere il video opzionale e conservare l'output seriale se assente o
non supportato. Nessun cambio modo durante la prima implementazione.

B. Enumerare i modi e selezionarne uno supportato prima di uscire dai
Boot Services. Richiede limite di enumerazione, gestione dei buffer
QueryMode, rollback di SetMode e scelta della geometria desiderata.

Scelta utente: B, enumerare e impostare un modo supportato. La prima fase
implementa soltanto acquisizione/selezione GOP, prima del nuovo handoff:
massimo 256 modi, preferenza per la geometria piu vicina a 1024x768,
parita risolta dall'indice minore, RGB/BGR 32 bit, larghezza <=1920,
altezza <=1080, stride <=4096 e capacita <=32 MiB. Sono limiti operativi
v0, non limiti universali del dispositivo. Le altre geometrie supportate
entro questi limiti restano candidate; non si forza un indice presunto.
Le risorse di QueryMode vengono liberate prima di SetMode. FreePool
fallita conserva il puntatore nell'owner per retry e blocca l'avanzamento.
Readback incoerente o SetMode fallita richiedono tentativo di ripristino
del modo originale; il fallimento del ripristino resta esplicito.
Il caso positivo QEMU deve comunque dimostrare che il
framebuffer e presente: il fallback seriale non costituisce PASS video.

## Contratto handoff v4

Decisione delegata dall'utente il 9 ottobre: framebuffer opzionale,
fallback seriale. Il kernel puo avviarsi senza display; il servizio GUI
futuro richiedera un framebuffer valido e segnalera separatamente il
fallimento video. Assenza e corruzione non sono equivalenti: il descrittore
assente e completamente zero, quello presente ma invalido blocca il suo
uso. Nessuna conversione silenziosa di un descrittore corrotto in assenza.

ABI implementata: versione4, append del descrittore da40 byte
al prefisso v3 da168 byte, dimensione208 byte e offset168. Loader e kernel
aggiornati insieme; nessuna compatibilita v3 dichiarata senza un parser
distinto. Loader e kernel ordinario ora usano questo formato. Validatori
kernel e controlli delle risorse/alias impediscono l'adozione come RAM;
mapping/cache rimangono prerequisiti delle scritture.

Il loader copia dati numerici, mai puntatori a strutture firmware:
presenza, indirizzo fisico, capacita, larghezza, altezza, stride e formato.
L'ABI e versionata e i validatori sono aggiornati insieme. I byte
riservati sono zero. Una risorsa assente ha descrittore completamente zero.

Validazione Rust no_std, senza heap: geometria positiva, stride almeno
larghezza, formato riconosciuto, moltiplicazioni controllate, capacita
almeno stride * altezza * 4, intervallo fisico senza overflow e nei limiti
CPU. I limiti operativi massimi saranno espliciti e testati, senza
derivarli da una risoluzione QEMU presunta.

Prima di qualsiasi store, il backend x86 verifica mapping, permessi RW/NX,
policy cache e compatibilita con PAT/MTRR del profilo. Volatile non sostituisce
questa analisi. Vietati alias con cache policy discordanti e sovrapposizioni
con risorse vive. L'intervallo video non viene adottato dall'allocatore RAM
e non viene liberato come se fosse una normale allocazione.

Un solo owner BSP, mutazioni a IF=0; nessuna scrittura dalla ISR. C possiede
mapping e store limitati; Rust valida descrittore e offset. Non servono
nuove istruzioni Assembly se il backend di mapping corrente e sufficiente.
Il driver futuro vive in kernel/drivers/video/framebuffer/ e il backend
in arch/x86/kernel/drivers/video/framebuffer/.

Schema non eseguibile:

```text
GOP prima di ExitBootServices
  -> copia/validazione descrittore
  -> handoff versionato
  -> validazione kernel e mapping con policy verificata
  -> prova di disegno limitata
  -> confronto screenshot e cleanup/accounting
  -> gate framebuffer
  -> task successiva: console, font, UTF-8, shell
```

## Accettazione prevista

Host: RGB/BGR, stride maggiore della larghezza, coordinate ai bordi,
overflow, capacita corta, formati non supportati, descrittore assente,
ownership e rollback. ASan sul C host, canary e accounting, warning come
errori, Clippy, cppcheck e limiti di struttura.

QEMU: dieci boot positivi consecutivi con hash immagine/OVMF/ESP e pattern
video verificato tramite screenshot della VM posseduta, senza fault
inattesi. Marker seriale da solo insufficiente. Prove negative separate
per assenza/formato non supportato e nessuno store su risorsa invalida.
Regressione seriale ordinaria. Stress su coordinate, stride e bordi.

Hardware fisico, cambio risoluzione runtime, GPU accelerata, DMA e multi-monitor
fuori dalla v0. Nessuna percentuale di copertura o prova leak inventata.
