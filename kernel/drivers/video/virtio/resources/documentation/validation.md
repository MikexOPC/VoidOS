# Validazione risorse VirtIO 2D a risoluzione display

20 ottobre 2026. Perimetro e scelte nel [design](design.md).
Checkpoint del blocco risorse; lo scanout e verificato nel successivo
[report di presentazione](presentation-validation.md).
Solo diagnostico QEMU fidato; nessun passthrough o hardware fisico.
La console ordinaria continua sul framebuffer firmware.

## Implementazione verificata

Una risorsa B8G8R8A8_UNORM alla geometria GET_DISPLAY_INFO, fino a 16 MiB.
RAM fisica contigua pinned e mapping WB dedicato, supervisor RW/NX, con
guardie e ownership delle tabelle. Registro di una sola superficie nel ramo
PML4[227]; snapshot paging esteso a 3720 byte, entro il blocco da 4 KiB.
Rust puro verifica geometria, entry e transizioni; C possiede i lease,
costruisce le tabelle e serializza i comandi.

Sequenza fenced: CREATE_2D, ATTACH_BACKING, TRANSFER_TO_HOST_2D,
DETACH_BACKING, UNREF. Le risposte vengono verificate prima di confermare
la transizione. Esito incerto: niente ulteriori comandi alla cieca;
reset confermato prima di restituire RAM e code. Reset non confermato:
buffer trattenuti. Le code GET_DISPLAY_INFO conservano la precedente API.

Fonti: [VirtIO 1.2 CS01](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
sezioni 5.7.6.5, 5.7.6.7 e 5.7.6.8; [Intel SDM 3A, revisione 090US](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
sezioni 5.5.4 e 5.6. Nessuna variazione al contratto di boot v5 o al loader.

## Test host

Suite specifica eseguita anche con Windows PowerShell 5.1:
`kernel/build/virtio-resources-host-final-ps51.log`, risultati e XML in
`kernel/build/virtio-resources-host-final-ps51/`.

| Prova | Risultato |
| --- | --- |
| Protocollo C | 8200 verifiche, 600 cicli; errore a ogni comando e rifiuti senza mutazioni |
| Mapping C | 6300 verifiche, 100 cicli con allocatore fisico reale; CPU/MMU simulate |
| Rust | 3 test in debug e 3 in release, inclusi tutti i conteggi da 1 a 4096 pagine |
| Compilazione | MSVC C11 /W4 /WX, Rust con warning come errori, Clippy |
| ASan | Nessuna segnalazione nelle due suite C strumentate |
| cppcheck 2.22.0 | 0 error/warning/performance/portability; 12 style e 17 information conservati nell'XML |

Le prove includono dimensioni invalide, overflow, alias, buffer corti,
risposte/fence corrotti, root estranea, flush e capture falliti, lease non
pinned, rollback e retry. Il mapper usa il vero accounting dei lease nel
processo host; non esegue istruzioni privilegiate sul processore host.

Regressioni eseguite nello stesso blocco:

- Code: 644749 verifiche del protocollo con 70000 completion simulate,
  59600 del trasporto e 88600 del ciclo reset/rilascio;
  `kernel/build/virtio-resources-queue-host.log`.
- Mapping GPU: 793400 verifiche, 100 cicli con allocatore reale;
  `kernel/build/virtio-resources-gpu-regression.log`.
- Framebuffer: 7 gruppi lifecycle per 100 ripetizioni, 3 gruppi pixel e
  5 gruppi drawing per 100 ripetizioni;
  `kernel/build/virtio-resources-fb-regression.log`.
- Host kernel: suite GDT/TSS, stack, timer, ownership e rollback;
  `kernel/build/virtio-resources-kernel-host.log`.
- Paging finale: 8 gruppi ripetuti 100 volte, cleanup dei lease pinned;
  `kernel/build/virtio-resources-paging-final.log`.

Valgrind non eseguito. ASan non dimostra assenza di leak fisici: quella
proprieta e controllata tramite lease e budget nei test host e QEMU.
Coverage linee/branch N/D. I conteggi sono verifiche, non test indipendenti.

## QEMU: evidenze finali

QEMU 11.1.0, build `v11.1.0-12130-ge470268ff4`, Q35/TCG, CPU max, BSP,
128 MiB, OVMF, nessuna rete o periferica passthrough. VM eseguite in sequenza,
ESP e variabili firmware private; hash dell'immagine ricontrollato dopo copia.

| Profilo | Boot consecutivi | Cartella sotto kernel/build/ |
| --- | --- | --- |
| Risorsa display | 10/10 PASS | virtio-initialization-firmware-39f620f2127142fa861f37b205c48405 |
| GPU assente | 10/10 PASS | virtio-initialization-firmware-f5b94f983353423394b536abad038f34 |
| Code precedenti | 10/10 PASS | virtio-initialization-firmware-5da470c254124e949a9f3f2789c8ad01 |
| Console ordinaria, mouse/tastiera | 10/10 PASS | virtio-mapping-firmware-079c706c904e439abe1d1aed4f9b3033 |
| Comando risorsa rifiutato | 1/1 rifiuto atteso | virtio-initialization-firmware-ade12a0ea1f64e49a74e91072be15e92 |
| Feature richiesta non supportata | 1/1 rifiuto atteso | virtio-initialization-firmware-dce10d0ba66c4642b1db42d6832580d4 |

Ogni cartella conserva results.json, argomenti, seriale, trace device,
snapshot PCI e, dove applicabile, dump delle tabelle ritirate o delle code.
Totale: 40 boot positivi e due negativi attesi. Nessun crash inatteso nelle
campagne finali. I due negativi verificano il rifiuto e il cleanup, senza
contare il mancato KERNEL_READY come un successo positivo.

A 1280x800 il backing e di 4096000 byte: 1000 pagine dati e 4 tabelle.
Ogni boot positivo risorsa completa 256 richieste del precedente probe,
poi GET_DISPLAY_INFO e cinque comandi risorsa: 262 completion verificate.
Il controllo esterno legge sei pixel campione della RAM e tutte le entry
delle tabelle ritirate; verifica guardie, indirizzi e permessi. Il kernel
verifica l'inventario mentre il mapping e attivo. Il dump esterno delle
tabelle dopo cleanup non e una misura indipendente delle PTE attive.

Trace QEMU conferma i cinque comandi in ordine. Il negativo invia un
TRANSFER con ID 2 dopo CREATE/ATTACH di ID 1: risposta rifiutata, stato
incerto, nessun DETACH/UNREF alla cieca, reset e budget ripristinato.
In entrambi i casi il test esterno verifica status device zero, rami
PML4[226]/[227] rimossi e foglie/guardie della finestra revocate.

## Immagini

| Percorso sotto kernel/build/ | SHA256 |
| --- | --- |
| virtio-resources-image/kernel.elf | 90831789F20F61BFCF36AD5497677496A1012D2635AC14B9592437EECABB8431 |
| virtio-resources-ordinary/kernel.elf | C71AFD98444F9E7203ED9698C67598756B9DB95CD814CDF416F2EB5B3806F8CE |
| virtio-resources-queue-image/kernel.elf | B39BD3BBF7483C54D6FC343005C76A38D5B2911FDD65E5836F66058CD839466D |
| virtio-resources-reject-image/kernel.elf | AD5ACAF7722FEDA24FA905BC0024456997B4A02FAE8F540DCF1BDB228356B537 |

Loader `arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI`:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.
Nessuna installazione dell'immagine diagnostica nella ESP ordinaria.
Il MAP ordinario esclude protocollo/code/probe risorse; contiene il registro
surface usato dall'inventario, senza una superficie registrata.
La regressione ordinaria verifica anche input mouse/tastiera, Esc seguito
da nuovo input, uscita della shell, guardie MMIO GPU e budget delle risorse
temporanee ripristinato. Il mapping MMIO GPU ordinario resta pinned per
la durata del kernel: non viene confuso con un lease temporaneo rilasciato.

## Review e limiti

Il controllo strutturale passa su 98 sorgenti del perimetro VirtIO e
adattatori: dimensioni, cinque file diretti, include e fine riga LF.
Manifest con hash: `kernel/build/virtio-pci-structure.json`.
Diff senza whitespace error. La review ha corretto la collisione del
basename authority.c rinominando il nuovo sorgente in registry.c;
non e stato cambiato il sorgente dell'autorita MMIO GPU.

Il primo smoke era arrivato a KERNEL_READY ma il test host non trovava il
trace, per due opzioni -trace concorrenti: corretto il test e ripetute le
prove finali. Un precedente test interrotto dall'utente non e contato.

Non implementati: SET_SCANOUT, RESOURCE_FLUSH, output visivo VirtIO,
cursor queue operativa, resize o integrazione nella console ordinaria.
La risposta al trasferimento e il trace non dimostrano pixel presentati.
Nessun benchmark di fluidita o prova GPU fisica. Limite 16 MiB e backing
contiguo: una risoluzione 4K a 32 bit non e supportata da questo profilo.

IOMMU resta nel [registro futuro](../../../../../../docs/future-work.md):
pinning, NX e guardie CPU non isolano DMA ostile. Nessuna pretesa di
supporto SMP o sicurezza di dispositivi non fidati.
