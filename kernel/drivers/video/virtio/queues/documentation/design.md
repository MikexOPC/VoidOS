# Code VirtIO GPU e buffer DMA: proposta v0

19 ottobre2026. Profilo diagnostico QEMU fidato scelto e implementato.
Le prove eseguite sono riportate in [validation.md](validation.md).
Il blocco precedente di reset/negoziazione resta invariato.

## Obiettivo e alternativa

Profilo diagnostico separato, seriale, BSP, senza task concorrenti:
preparare le due code e verificare una richiesta GET_DISPLAY_INFO.
Nessun takeover della console, scanout, framebuffer GPU o cursore.

Alternativa: implementare prima isolamento IOMMU e traduzione DMA.
E un modulo diverso, con discovery, domini e invalidazioni proprie.
La proposta seguente non ne fornisce le garanzie.

## Decisione e perimetro

Dopo il confronto delle alternative, l'utente ha scelto di rinviare IOMMU
e registrarla fra gli interventi futuri. Il perimetro resta quello seguente.

1. Due split queue da8 descrittori; una sola richiesta control in volo.
   La coda cursor viene inizializzata ma resta vuota.
2. Polling con clock e limite campioni; nessun handler GPU nuovo.
3. Buffer dedicati pinned, nessun indirizzo stack o heap generico al device.
4. Eccezione esplicita al gate IOMMU soltanto per QEMU fidato, senza
   passthrough e senza estendere il profilo a dispositivi fisici.
5. Adattatori necessari nel trasporto VirtIO, build, ingresso diagnostico,
   test e API memoria gia esistenti. Cambi al modello globale della memoria
   o al loader richiedono un ulteriore progetto, non sono inclusi.

SKILL.md, Skill4, impone: «BLOCCA se: threat CRITICAL senza mitigation,
IOMMU non usato per DMA». L'utente autorizza l'eccezione esclusivamente
per questo diagnostico QEMU fidato; il gate generale resta invariato.
Vedere [FW-001](../../../../../../docs/future-work.md#fw-001--isolamento-dma-tramite-iommu).
L'identita PCI non dimostra che il backend sia fidato: la fiducia e una
condizione dell'ambiente di sviluppo, non un controllo del driver.

## Fonti e contratto hardware da verificare nel codice

[VirtIO1.2 CS01](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
§§2.7,3.1.1,3.3,4.1.4.3,4.1.4.4,5.7.2 e5.7.6.8.
Common: num_queues0x12/u16 RO; select0x16/u16, size0x18/u16,
MSI-X0x1a/u16, enable0x1c/u16 RW; notify_off0x1e/u16 RO;
desc0x20, driver0x28, device0x30/u64 RW. Indirizzi e layout verificati,
notifica derivata dalla capability con overflow e bounds controllati.
DRIVER_OK bit2 solo dopo code pronte. Controlq0, cursorq1.

[Linux v6.12 virtio_ring.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/virtio/virtio_ring.c):
riferimento per pubblicazione ordinata e contatori locali, non copia.
[Linux v6.12 virtgpu_kms.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/virtio/virtgpu_kms.c):
riferimento per separare preparazione code e disponibilita dispositivo.
Il nostro polling diagnostico non riproduce il runtime Linux.

Ogni nuovo accesso richiede fonte, larghezza, offset e motivo nel sorgente.
Accessi scalari; definire le barriere CPU/DMA prima dell'implementazione.
Una barriera compilatore e volatile non costituiscono tale contratto.

## Ownership e memoria

Owner privato persistente: mapping registri, lease RAM, indirizzi fisici,
viste CPU, stato e contatori attesi. Il dispositivo non sceglie puntatori
CPU, lease o indici usati per liberare risorse.

Budget implementato: quattro pagine RAM da4KiB: due pagine ring, una richiesta,
una risposta. Il diagnostico riusa la finestra memoria esclusiva gia esistente:
acquisisce un blocco contiguo di quattro pagine pinned con guardie, senza nuove
tabelle o modifiche al modello di memoria. Puo fallire per frammentazione.
L'interfaccia delle code conserva quattro indirizzi fisici distinti e non
impone contiguita ad altri backend futuri. Layout calcolati con overflow,
allineamento e disgiunzione verificati; le pagine sono azzerate prima
della pubblicazione. Tabelle e buffer hanno accounting separato.

RAM CPU supervisor RW/NX, cache WB coerente nel solo profilo x86/QEMU
fidato. Registri restano UC. Nessun alias RAM con attributi discordanti.
La compatibilita con le API memoria e l'inventario esistenti va verificata
prima di scrivere codice; nessun nuovo mapping elude il relativo audit.
Guardie CPU e NX non sono barriere contro DMA ostile.

Rust puro: layout, contatori, stati e validazione di valori copiati dalla
risposta. C: lease, copie limitate, MMIO e ordinamento. Nessun riferimento
Rust vivo alla memoria modificabile dal dispositivo; FFI scalare o copie
con durata esplicita. Nessun puntatore del dispositivo viene dereferenziato.

## Stati e pseudocodice non eseguibile

EMPTY -> OWNED -> NEGOTIATED -> QUEUES_READY -> LIVE -> IN_FLIGHT
IN_FLIGHT -> LIVE dopo risposta verificata.
Qualunque stato posseduto -> RESETTING -> RELEASED oppure QUARANTINED.

1. Verificare owner e prerequisiti PCI, clock, feature e mapping. MEMORY
   e MASTER devono essere gia abilitati dal firmware; niente PCI data write.
2. Allocare/pinnare RAM, verificare inventario e azzerare buffer.
3. Per ogni coda verificare size massima almeno8 e stato disabilitato;
   impostare8, indirizzi e configurazione senza MSI-X, con readback.
4. Polling BSP/IF=0 dopo la quiescenza APIC del bootstrap, con vettori
   MSI-X configurazione/code a0xffff e flag avail NO_INTERRUPT. Questo
   flag e solo soppressione best-effort; non si abilita una route GPU.
   Prima del reset si legge l'ISR read-clear; il reset confermato precede
   ogni rilascio. Non riusare questo schema in task con IRQ attivi.
5. Abilitare code e DRIVER_OK, rileggendo lo stato.
6. Preparare richiesta/risposta, pubblicare descrittori e avail con
   ordinamento esplicito, quindi notificare il device.
7. Attendere progresso con deadline PM e limite campioni indipendente.
   Proposta locale100ms, da misurare: non e un timeout VirtIO universale.
8. Copiare una sola completion in RAM privata, verificare delta used,
   ID atteso, lunghezza e header prima di leggere i campi risposta.
   Verificare numero e geometria display senza attivare uno scanout.
9. Ripetere richieste finite per verificare riuso; poi reset confermato,
   revoca mapping, unpin e restituzione dei lease con accounting finale.

Il diagnostico invia256 richieste fenced, una alla volta. I due ring hanno
descrittori a0, avail a128 e used a160 nelle rispettive pagine. La richiesta
e lunga24 byte; la risposta408 byte. Rust controlla delta used, ID, lunghezza,
dimensioni e overflow; C controlla header/fence su una copia locale bounded.
Il limite dimensionale16384 e politica diagnostica, non limite VirtIO.
La queue cursor resta vuota, senza inviare comandi cursore.

[Intel SDM Vol2B 253667-060US](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf),
voce MFENCE, pagina4-22: barriere compilatore e MFENCE per pubblicazione e
consumo sul profilo WB coerente x86/QEMU; CPUID.1.EDX[26] verificato prima
dell'uso. Accessi MMIO UC scalari8/16/32bit, indirizzi64bit low/high prima
di queue_enable. MFENCE non e una protezione DMA.
[EDK2 stable202408 Pci22.h](https://raw.githubusercontent.com/tianocore/edk2/edk2-stable202408/MdePkg/Include/IndustryStandard/Pci22.h),
header e Command di PCI2.2 §6.1: offset0x04, MEMORY bit1, MASTER bit2.

La memoria condivisa resta modificabile dal backend: le copie limitano
gli accessi del driver ma non rendono sicuro un dispositivo DMA malevolo.
Risposta errata o timeout: non riusare la coda prima del reset.
Reset non confermato: trattenere tutti i buffer gia pubblicati e mapping;
errore terminale diagnostico, nessun rilascio basato sul solo timeout.
Un reset VirtIO osservato e garanzia del dispositivo conforme, non prova
di arresto di un dispositivo fisico ostile.

## Rischi e accettazione

| Rischio | Contromisura nel profilo proposto |
| --- | --- |
| Completion corrotta o duplicata | Snapshot locale, bounds, ID e transizioni Rust |
| Riuso prematuro del buffer | Una richiesta in volo, ownership fino a completion/reset |
| Attesa infinita | Clock verificato, deadline e limite campioni |
| Scritture dopo cleanup | Reset confermato prima di liberare; altrimenti quarantena |
| Interrupt inattesi | Configurazione e acknowledgement verificati, nessuna nuova IRQ GPU |
| Backend DMA ostile | Non mitigato senza IOMMU; escluso solo con scelta esplicita QEMU fidato |

Gate previsti, non risultati: unit test Rust debug/release, ABI C/ASan,
overflow, errori a ogni fase, rollback, timeout e reset bloccato; contatori
16bit anche al wrap, completion errate, code piccole e notify fuori BAR.
Verifica delle istruzioni/barriere e analisi statica con warning bloccanti.
Dieci boot diagnostici consecutivi con scambio reale e budget ripristinato;
regressioni ordinarie e GPU assente separate, immagini con hash verificati.
Test negativi distinti dai boot positivi. Hardware fisico, isolamento DMA,
coverage e prestazioni non si dichiarano dimostrati da questi test.

## Revisione documentale

Fonti primarie e contratti riesaminati. Questo documento descrive il nuovo
diagnostico; non riscrive le prove del reset precedente come se coprissero
le code. Le campagne dell'immagine finale, i limiti dei mock e i risultati
effettivi sono separati nel report di validazione.
