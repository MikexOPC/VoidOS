# Validazione scanout e flush VirtIO

20 ottobre 2026. Scelta utente: diagnostico visivo prima, console dopo.
[Contratto e ricerca](presentation-design.md). Questo checkpoint estende
il precedente [blocco risorse2D](validation.md), senza riscriverne le prove.
Solo QEMU fidato, senza passthrough; nessuna estensione dell'eccezione IOMMU.

## Risultato del blocco

SET_SCANOUT e RESOURCE_FLUSH sono implementati nel diagnostico separato.
Il parser conserva indice e dimensioni del primo display abilitato; il
driver non assume scanout0. Una superficie WB pinned gia trasferita viene
associata, presentata, aggiornata parzialmente e scollegata prima del close.

La risorsa conserva identita della coda e stato Bound. Close vietato mentre
associata; un comando fallito lascia Bound incerto e vieta ulteriori update,
unbind e close. Il probe conferma reset prima di liberare le risorse.
Rust puro calcola rettangolo/offset senza overflow o runtime di panic;
C serializza i messaggi e gestisce lifetime e riscontri. Nessun nuovo
mapper, accesso MMIO o handler IRQ. Boot ABI e snapshot paging invariati.

Riferimenti: [VirtIO1.2 CS01](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html),
5.7.6.1/2/5/8; [QEMU v11.1.0 virtio-gpu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/display/virtio-gpu.c)
e [trace-events](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/display/trace-events).
Le comparazioni Linux e i compromessi sono nel design; nessun codice copiato.

## Host e strumenti

`kernel/build/virtio-scanout-host-final.log`, artefatti in
`kernel/build/virtio-scanout-host-final/`. Runner eseguito con Windows
PowerShell5.1, MSVC C11 /W4 /WX, ASan; Rust warning come errori e Clippy.

| Prova | Esito |
| --- | --- |
| Risorse C | 8200 verifiche, 600 cicli, invariati i casi del precedente blocco |
| Presentazione C | 19900 verifiche, 1100 cicli, errore simulato a ciascuno dei10 comandi |
| Mapper C | 6300 verifiche, 100 cicli allocatore reale, CPU/MMU simulate |
| Rust geometria | 4 test debug e 4 release: pagine, stati, dimensioni, rettangoli/offset |
| Comparatore screenshot | 1 test con due scene valide e rifiuto di immagini troncate, geometria errata e pixel corrotti dentro/fuori dal rettangolo |
| ASan | Nessuna segnalazione nelle suite C strumentate |
| cppcheck2.22.0 | 0 error/warning/performance/portability;14 style e20 information conservati nell'XML |

I test di presentazione verificano anche scanout7, disallineamento tra
geometria rilevata e risorsa, identita Self/coda, close vietato mentre bound,
coordinate rifiutate prima di pubblicare comandi e campi padding azzerati.

Regressione code: `kernel/build/virtio-scanout-queues-host.log`:
646122 verifiche protocollo con70000 completion simulate e wrap16bit,
59600 trasporto,88600 lifecycle/reset. Include selezione dell'indice7 e
controlli alias sugli output del parser; API precedente conservata.
4 test Rust debug e4 release, Clippy e ASan passano anche in questa suite.

Nessun Valgrind; coverage linee/branch N/D. ASan riguarda i processi host;
leak di pagine fisiche controllati separatamente tramite pin/lease/budget.
I conteggi C sono verifiche ripetute, non casi indipendenti di coverage.

## QEMU finale

QEMU11.1.0 build `v11.1.0-12130-ge470268ff4`, Q35/TCG, CPU max, BSP,
128MiB, OVMF e ESP private. Nessuna rete/passthrough. Una VM per volta.
Il runner verifica SHA dopo copia, deadline, marker, protocollo, RAM,
tabelle ritirate/guardie, status device, revoca dei rami e budget.

| Campagna | Esito | Cartella sotto kernel/build/ |
| --- | --- | --- |
| Scanout e update parziale | 10/10 PASS | virtio-initialization-firmware-30d38cfcc4444b5f8cb3e331b418d8c8 |
| GPU assente | 10/10 PASS | virtio-initialization-firmware-830c1664e75d46968ed1962231007507 |
| Risorse senza scanout | 10/10 PASS | virtio-initialization-firmware-8e51f251ebe34ad1b990c75087e6e0b0 |
| Console ordinaria e input | 10/10 PASS | virtio-mapping-firmware-58f59bba7ae5446b95d3205d0ada8463 |
| Mancata conferma host | 1/1 rifiuto atteso | virtio-initialization-firmware-764f8c12dbc24eec8dffcc7201e2c1ea |
| Feature non supportata | 1/1 rifiuto atteso | virtio-initialization-firmware-3e63b6d14c754390bf6a34a3339ce58f |

Totale40 boot positivi e due negativi intenzionali: nessun crash inatteso
nelle campagne finali. La console ordinaria verifica mouse, tastiera,
Esc seguito da nuovo input, exit e cleanup delle risorse temporanee;
il mapping MMIO GPU ordinario resta pinned per la durata del kernel.

Ogni boot visivo confronta integralmente due screenshot PPM1280x800:
2048000 pixel per boot,20480000 nell'intera campagna. La seconda scena
cambia solo il rettangolo64x32 a(16,24); confronto anche fuori dall'area.
Il checker e stato provato con immagini corrotte per evitare PASS vacui.

L'host ferma la VM a ogni checkpoint, acquisisce il display tramite QMP,
verifica e conferma tramite mailbox nel tail inutilizzato della pagina
cursorq; poi riprende. La mailbox non e pubblicata come descrittore GPU.
L'attesa guest e finita tramite clock PM e limite campioni; non e parte
del runtime video ordinario. Nel negativo l'host non conferma: il guest
scade, resetta, restituisce i lease e segnala il rifiuto intenzionale.
Il panic diagnostico finale di quel negativo e previsto, non contato
come boot positivo o crash inatteso.

Trace conferma SET_SCANOUT risorsa1, flush intero, transfer/flush parziale,
SET_SCANOUT risorsa0, detach/unref e reset.267 completion verificate per
boot visivo:256 del probe code iniziale, GET e10 comandi del nuovo ciclo.
Nel timeout restano6 completion del secondo ciclo: reset al posto di
teardown alla cieca di una superficie ancora associata.

## Immagini e riproduzione

| Immagine sotto kernel/build/ | SHA256 |
| --- | --- |
| virtio-scanout-image/kernel.elf | 45CA473943997B28F6A6C16E85A8528FC17D3D487A09120A59D7E605DAD133BE |
| virtio-scanout-resource-regression/kernel.elf | A3EE120AFE1738794B0C715D5688E61C934A02968B701ACE6721390350C4CAE9 |
| virtio-scanout-ordinary/kernel.elf | CA708E16D3157E7DEF1A7C5EC49160F3ED75EDFC2542AA64BC48C053A9408B2D |

Loader invariato:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.
Nessuna installazione della variante diagnostica nella ESP ordinaria.
MAP ordinario privo di resource_run, scanout_bind, visual_probe e queue_start.

Build: `powershell.exe -NoProfile -File kernel/scripts/build.ps1 -TestVgpuScanout -OutputDirectory D:/OS/kernel/build/virtio-scanout-image`.
Collaudo: Python3 con `-B arch/x86/kernel/drivers/video/virtio/scripts/initialization.py --scanout --image kernel/build/virtio-scanout-image/kernel.elf --boots 10`.
Timeout controllato: usare `--scanout-timeout` e `--boots 1`.
Host: `powershell.exe -NoProfile -File kernel/drivers/video/virtio/resources/scripts/test.ps1`.

## Review e limiti

Controllo strutturale su105 sorgenti, massimo400 righe/cinque file diretti,
include e LF. Manifest con hash in `kernel/build/virtio-pci-structure.json`.
Lo script di build principale resta sotto400 righe. Diff senza whitespace
error. Nessuna modifica a reuse/ o licenza.

Corretti durante il ciclo: calcolo Rust che introduceva un simbolo panic
non linkabile nel profilo freestanding; aspettativa del nuovo test parser
non coerente con la geometria1024x768 del suo mock; directory temporanea
del checker spostata sotto build/ per l'ambiente sandbox.
Le campagne finali sono sull'immagine successiva alle correzioni.

Nessun takeover console, cursore VirtIO, resize, multihead o hotplug.
La geometria con indice nonzero e provata nei mock; QEMU finale espone0.
Resta il limite16MiB/backing contiguo; nessuna prova hardware fisico,
GPU ostile, isolamento DMA o SMP. Il confronto di screenshot prova il
display QEMU ai checkpoint, non fluidita, latenza o assenza di tearing
durante il movimento. La console firmware resta disponibile nel profilo
ordinario; il reset del diagnostico non promette di ripristinarne l'immagine.
