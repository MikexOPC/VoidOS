# Reset e negoziazione VirtIO GPU v0

Decisione del19 ottobre2026: profilo diagnostico separato, output seriale.
Il reset non promette il ripristino del GOP. L'avvio ordinario conserva
il mapping gia verificato e non esegue questa sequenza.

## Ricerca e perimetro

[VirtIO1.2 CS01](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
§§2.1,2.4,3.1.1,4.1.4.3 e6.1: reset osservato, stato crescente,
conferma FEATURES_OK; VERSION_1 bit32. Common PCI: select device0x00/u32,
feature0x04/u32, select driver0x08/u32, feature driver0x0c/u32,
status0x14/u8. Status ACK1, DRIVER2, FEATURES_OK8, FAILED128.
DRIVER_OK4 resta spento. Nessuna coda, notifica o indirizzo DMA.

[QEMU11.1.0 virtio-gpu](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/display/virtio-gpu.c),
virtio_gpu_reset_bh: distrugge risorse e azzera le superfici scanout.
La presentazione VGA aggiuntiva non costituisce un contratto di recupero GOP.
[Linux6.12](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/virtio/virtgpu_kms.c)
crea le code prima di virtio_device_ready; non copiamo codice o cleanup.

[Intel SDM3A090US](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
§14.3: UC forte e accessi scalari tramite registri generali per evitare
accessi SIMD con effetti collaterali. Il backend usa volatile scalare e
barriere compilatore; nessuna nuova politica DMA derivata da UC.

## Progetto v0

Un owner persistente del mapping; un prestito sincrono del trasporto,
senza task o callback IRQ. BSP/IF=0, niente AP o accessi concorrenti.
C esegue protocollo e attese; Rust puro valida feature e registri consentiti.
FFI scalare no_std, nessun puntatore o allocazione in Rust.

| Guasto/minaccia nel profilo | Rischio | Mitigazione |
| --- | --- | --- |
| Offset o larghezza errati | HIGH: accesso MMIO con effetti collaterali | Common dal piano verificato, whitelist Rust, bounds e accessi scalari |
| Stato/feature rifiutati | MEDIUM: inizializzazione incompleta | Readback, arresto senza DRIVER_OK, FAILED/reset e accounting |
| Clock fermo o reset pendente | MEDIUM: attesa infinita | Deadline PM e limite campioni indipendente, risorse trattenute se incerto |
| Risorse video firmware perse | MEDIUM: schermo non utilizzabile | Profilo seriale separato, nessun recupero GOP presunto |
| Callback o buffer chiamante invalidi | HIGH | API interne fidate, prestiti stabili/disgiunti; non sono user pointer |

Dispositivo fisico malevolo e DMA ostile sono fuori da questo profilo
emulato: nessun nuovo indirizzo DMA viene fornito. L'assenza di IOMMU
non autorizza a dichiarare isolamento DMA; il relativo gate precede
l'implementazione delle code per un profilo che lo richiede.

Pseudocodice non eseguibile:

1. Verificare owner, PCI corrente, piano e inventario completo.
2. Ricavare common da offset del piano; vincolare larghezza/allineamento.
3. Verificare clock ACPI PM; reset e attesa di status zero.
4. ACK, DRIVER, leggere due parole feature, accettare solo VERSION_1.
5. Scrivere entrambe le parole driver, poi FEATURES_OK e rileggere.
6. Fermarsi prima di DRIVER_OK. Il test registra risultato, resetta,
   attende zero, riverifica inventario e restituisce i lease del mapping.

Reset: deadline locale100ms (357955 tick PM), non limite dello standard;
maschera24/32bit e delta modulare. Anche il numero di campioni e finito:
un clock fermo/inaffidabile non puo produrre un'attesa infinita. La soglia
e politica diagnostica da verificare nell'emulatore, non promessa hardware.

Errori: IO, tempo, clock fermo, feature mancante/rifiutata o NEEDS_RESET.
Non proseguire dopo un errore. Segnalare FAILED dove possibile, tentare
un reset controllato; senza conferma zero non rilasciare l'owner. Non
riattivare il GOP dopo takeover. Nessun buffer comunicato al dispositivo.

## Accettazione e upgrade

Test host positivi e negativi, ordine e larghezza di ogni accesso, clock
fermo/wrap/deadline, rifiuti senza accessi, ABI Rust debug/release e ASan C.
Dieci boot diagnostici finali, stato dispositivo zero osservato da QTest,
budget originale e ramo GPU revocato; regressioni ordinarie e VGA.
Prove e limiti nel report finale, senza dichiarare coverage o leak fisici
da ASan. Step successivo: code/DMA, che richiede il proprio contratto.
