# Risorse VirtIO2D alla risoluzione display

19 ottobre2026. Scelta utente: seconda alternativa, superficie alla risoluzione
display e mapping RAM dedicato. Implementazione diagnostica; risultati e
limiti delle prove nel report di validazione del modulo.
Il successivo [contratto scanout/flush](presentation-design.md) e stato
scelto dall'utente e verificato nel diagnostico, separatamente da questo blocco.
Prerequisito: [code diagnostiche verificate](../../queues/documentation/validation.md).
Resta valida l'eccezione IOMMU gia concordata: solo QEMU fidato, nessun
passthrough o dispositivo fisico. Non viene richiesta una nuova deroga.

## Perimetro scelto

Una risorsa alla risoluzione rilevata da GET_DISPLAY_INFO, formato B8G8R8A8_UNORM;
creazione, backing, trasferimento fenced, detach e unref. Nessuno scanout
o comando cursor, nessuna sostituzione della console ordinaria.

Il diagnostico conserva quattro pagine delle code nella finestra esistente;
i pixel hanno un mapping RAM separato con guardie, fino a16MiB, e lease
contiguo pinned. Se dimensioni o allocazione non sono supportate, fallimento
controllato senza ridurre silenziosamente la risoluzione. Il backing del
protocollo usa una entry contigua; scatter/gather e un'evoluzione futura.
Non creare alias con cache diversa; WB coerente per RAM e UC per MMIO.

La proposta64x64 era l'alternativa piu circoscritta, non e quella scelta.
A1280x800/32bit servono4096000 byte pixel, oltre alle code e quattro tabelle.
Lo snapshot paging include ora lease RAM e ID delle tabelle; dimensione3720
byte, entro il blocco oggetti da4KiB. I checkpoint da3600 byte restano storici.

## Mapping e ownership

Ramo dedicato PML4[227], base dati `(227ULL<<39)+4096`; guardia inferiore,
guardia superiore e foglie4KiB supervisor RW/NX senza global. Fino a11
tabelle da lease pinned: PDPT, PD e fino a9 PT. Autorita privata registrata
per una superficie; verifica CPU/root/PAT prima e dopo, inventario completo,
nessun alias delle pagine dati o delle tabelle. RAM WB, PAT entry0 verificata.

Rust puro produce geometria ed entry; C prepara le tabelle inattive tramite
il prestito WB della finestra. Solo dopo verifica pubblica il ramo e registra
l'autorita. Revoca del ramo e invalidazione delle traduzioni prima di unpin
e restituzione dei frame; un errore trattiene risorse e consente un retry
del mapper. Il chiamante deve prima confermare il reset del device se ha
pubblicato il backing DMA. Non e un mapper per utenti o per SMP concorrente.

[Intel SDM3A090US](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
§§5.5.4,5.6: P bit0, RW bit1, US bit2 spento, XD bit63 con NXE;
la revoca riusa il flush CR3 del backend esistente con PCID/global esclusi.

## Ricerca primaria

[VirtIO1.2 CS01](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
§§5.7.6.5–5.7.6.8: fencing, risorse e backing. Comandi CREATE_2D0x101,
UNREF0x102, TRANSFER_TO_HOST_2D0x105, ATTACH_BACKING0x106,
DETACH_BACKING0x107; successo OK_NODATA0x1100. Formato B8G8R8A8_UNORM1.
Header24 byte; create40; attach48 con una entry; transfer56; detach/unref32.
Una risorsa cursore futura deve essere64x64; qui non viene attivata.

[Linux v6.12 virtgpu_object.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/virtio/virtgpu_object.c):
riferimento per ownership dell'oggetto e lifetime delle risorse di backing;
non trasferiamo il suo framework DRM o codice nel nostro driver.
[QEMU v11.1.0 virtio-gpu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/display/virtio-gpu.c):
riferimento per creazione, trasferimento e cleanup dei mapping del device.
I riscontri QEMU non dimostrano compatibilita con altri backend.

## Contratto e responsabilita

Owner persistente privato e non spostabile; ID risorsa non nullo, non
riutilizzato durante la sessione. Una risorsa e una richiesta in volo.
Rust puro valida dimensioni, byte, pagine, rettangoli, offset e transizioni;
C possiede lease, pixel, serializzazione comandi, trasporto e cleanup.
Nessun riferimento Rust alla memoria modificabile dal dispositivo.

Adattare il trasporto control a lunghezze esplicite e limitate, preservando
l'API GET_DISPLAY_INFO e i suoi test. Richieste di questo blocco fino a56
byte, risposte24 byte; GET_DISPLAY_INFO conserva408 byte di risposta.
ID, delta used, tipo, lunghezza, fence e campi riservati verificati su copie
locali prima di modificare lo stato. Errore o timeout impediscono ulteriori
richieste sulla stessa coda fino al reset.

L'estensione include il modulo risorse e mapping RAM, gli adattatori delle
code e dell'inventario, diagnostico, build e test. Nessun cambiamento al loader
o all'algoritmo dell'allocatore fisico. Il registro paging resta bounded.

## Stati e pseudocodice non eseguibile

EMPTY -> CREATED -> ATTACHED -> TRANSFERRED -> DETACHED -> RELEASED.
Una transizione viene confermata solo dopo risposta fenced valida.
Se la pubblicazione e avvenuta ma l'esito non e noto: UNCERTAIN.

1. Usare la geometria rilevata dal collaudo iniziale delle code, concluso
   con reset e rilascio della finestra. Acquisire RAM pixel e tabelle dedicate.
2. Calcolare/verificare geometria e backing; inizializzare tutti i pixel
   prima di esporne gli indirizzi, mantenendo disgiunti ring e messaggi.
3. Negoziare nuovamente, acquisire quattro pagine delle code e verificare
   che GET_DISPLAY_INFO conservi la geometria scelta. Creare la risorsa,
   collegare una entry backing con capacita esatta width*height*4.
4. Ordinare gli store pixel, inviare il trasferimento fenced e verificare
   la risposta. Nessun readback grafico viene presunto dal solo successo.
5. Detach fenced e unref fenced. Non riusare ID durante la sessione.
6. Anche dopo successo, reset confermato prima di restituire i buffer delle
   code; poi revoca mapping RAM, rilascio dati/tabelle e verifica budget.
7. Se l'esito e incerto, non tentare comandi alla cieca: reset. Senza
   conferma del reset, trattenere tutti i buffer gia esposti e il mapping.

Il flag FENCE conferma il completamento nel contratto VirtIO conforme,
non limita un backend DMA ostile. Le protezioni CPU non sostituiscono IOMMU.

## Accettazione prevista, non risultati

- Test host Rust e C/ASan per overflow, alias, backing corto/sovrapposto,
  rettangoli invalidi, transizioni e fence errati, timeout e rollback.
- Errori simulati a ogni fase; nessun rilascio dopo reset non confermato.
- Regressioni delle code, incluso wrap16bit e GET_DISPLAY_INFO invariato.
- Dieci boot consecutivi del diagnostico finale, con riscontri device e
  accounting; tracce dei comandi e RAM backing verificate dal test host.
- Regressioni ordinarie e GPU assente separate. La risposta al trasferimento
  da sola non dimostra pixel presentati: serviranno prove scanout successive.

Decisione direttive-locali §7 ottenuta: superficie a risoluzione display e mapping
dedicato. Scanout, cursore, resize dinamico e sessione ordinaria GPU restano
passi successivi; questa fase non promette pixel presentati sul display.
