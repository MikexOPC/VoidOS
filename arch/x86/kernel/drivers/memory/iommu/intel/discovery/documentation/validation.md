# Discovery Intel VT-d: validazione

20 ottobre2026. [Design e fonti](design.md). Decisione utente: Intel VT-d
su QEMU, IOMMU prima della console VirtIO ordinaria. Questo checkpoint
conclude soltanto la discovery DMAR; nessuna protezione DMA attiva.

Checkpoint successivo: [mapping posseduto e lettura registri](../../registers/documentation/validation.md).
Le assenze descritte qui sotto si riferiscono all'immagine di discovery.

## Implementazione

Parser C e policy Rust pura senza unsafe/allocazioni/MMIO. Checksum,
revision, HAW, riservati, ordine e bounds dei record; DRHD con dimensione
registri dichiarata, allineamento e disgiunzione; scope bounded e path PCI
conservati senza convertirli arbitrariamente in BDF. Output immutato su
errore/assenza. Limiti64KiB,8 unita,32 scope,8 coppie per path.

Il finder usa directory e finestra ACPI esistenti, rifiutando una seconda
DMAR. Snapshot64KiB statico nel solo probe diagnostico; nessuna allocazione
di pagine per il parser. Finestra idle, root e budget verificati prima/dopo;
il chiamante verifica anche l'inventario paging. Altri record DMAR saltati
e contati, senza interpretare RMRR/ATSR/ANDD. La loro presenza e il routing
multihop restano gate futuri prima di autorizzare DMA.

## Test host

Runner Windows PowerShell5.1: `scripts/test.ps1` del modulo.
Log `kernel/build/iommu-discovery-host.log`, artefatti e XML in
`kernel/build/iommu-discovery-host/`.

| Prova | Esito |
| --- | --- |
| C/ASan | 8100 verifiche,100 cicli; record/scope corrotti, checksum, lunghezze, alias, limiti8/9 unita e32/33 scope, sovrapposizioni, duplicati e fallimenti lettura |
| Rust | 3 test debug e3 release: header, span registri e scope |
| Toolchain | MSVC C11 /W4 /WX, Rust warning come errori, Clippy |
| cppcheck2.22.0 | 0 error/warning/performance/portability;4 style e10 information nell'XML |
| Isolamento build | TestIommuDiscovery con Install rifiutato prima della build |

Nei test C lettore e validazione directory sono mock; parser/finder sono
produzione. Nel kernel QEMU directory e lettore sono reali. Nessun accesso
ai registri VT-d. ASan senza segnalazioni anche nel decoder sui dump QEMU.
Nessuna allocazione dinamica nel modulo; Valgrind non eseguito. Accounting
delle pagine verificato durante le copie ACPI, senza dichiarare misurati
leak di un motore DMA. Coverage linee/branch N/D;8100 verifiche ripetute
non equivalgono a8100 casi indipendenti.

## QEMU finale

QEMU11.1.0 build `v11.1.0-12130-ge470268ff4`, Q35/TCG, CPU max, BSP,
128MiB, OVMF/ESP private, nessuna rete/passthrough. Diagnostico con VGA
standard e `intel-iommu,intremap=off,caching-mode=on,device-iotlb=off`.
Una VM per volta; nessuna attivazione delle code GPU nel diagnostico.

| Profilo | Esito | Cartella sotto kernel/build/ |
| --- | --- | --- |
| DMAR reale, confronto guest/host | 10/10 PASS | iommu-discovery-firmware-b1c9b09bdf0945f18a233214ccab7e13 |
| IOMMU/DMAR assente | 10/10 PASS | iommu-discovery-firmware-47cfc7d05359451a895d1e08d56f2763 |
| Console ordinaria e input | 10/10 PASS | virtio-mapping-firmware-b3dc63a849ad41029871ca36df485b8c |
| DMAR duplicata intenzionale | 1/1 rifiuto atteso | iommu-discovery-firmware-446684366a85439b9e2349318ce48b3e |

Totale30 boot positivi, un negativo previsto; nessun crash inatteso nelle
campagne finali. Il negativo termina con DMAR_DISCOVERY_INVALID dopo la
verifica budget: quel panic intenzionale non e un boot positivo.
La regressione ordinaria verifica guardie MMIO GPU, input mouse/tastiera,
Esc e nuovo input, exit e budget temporaneo; il mapping GPU resta pinned.

Il kernel scopre DMAR112 byte, HAW48, una DRHD segmento0 a0xFED90000,
span0x1000, flags0, sei scope, zero altri record. Il test ferma la VM dopo
KERNEL_READY, legge DMAR via QTest, controlla checksum e invoca il decoder
C/ASan sul dump confrontando inventario guest/host. Un parsing Python
confronta base/span/segment/flags dei DRHD con i log. Conservati dmar.bin,
host.txt, seriale e argomenti. Assenza verificata con marker dedicato.

## Immagini

| Immagine sotto kernel/build/ | SHA256 |
| --- | --- |
| iommu-discovery-image/kernel.elf | 7838755F62722307885EC7A5A6014D06E99FC7D38F658F91234F137ED873B38A |
| iommu-discovery-ordinary/kernel.elf | 8BF3A0DE46B604905CCE604B0849DD5FBE14ADDD46A4AA35CE8635A06C2F3EDF |
| iommu-discovery-host/host.exe | C2F5063EF5CEFA2CDB831A4C080CC6875E987890057EA60C1726CB12E4BF262A |

Loader invariato:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.
SHA ricontrollato dopo copia nell'ESP privata; nessuna installazione della
diagnostica nella ESP ordinaria. MAP ordinario senza x86_dmar_decode,
x86_dmar_find e x86_dmar_kernel_probe; snapshot/probe esclusi.

## Review e riproduzione

Controllo strutturale:17 sorgenti/adattatori, max400 righe e cinque file
diretti, include/link/LF verificati. Build394 righe, main290. Manifest con
hash `kernel/build/iommu-discovery-structure.json`. Diff senza whitespace
error. Nessuna modifica al vecchio OS o alla licenza.

Corretti prima dei risultati finali: lint Rust sulla divisibilita,
inizializzazione C non riconosciuta da MSVC con warning come errori,
runtime ASan mancante nel PATH host e separatore colon dell'opzione QEMU
acpitable incompatibile col drive Windows. Tentativi falliti conservati,
non contati come PASS. Le immagini finali sono successive ai fix eseguibili.

Build: `powershell.exe -NoProfile -File kernel/scripts/build.ps1 -TestIommuDiscovery -OutputDirectory D:/OS/kernel/build/iommu-discovery-image`.
Collaudo: Python3 con `-B arch/x86/kernel/drivers/memory/iommu/intel/discovery/scripts/firmware.py --boots 10`.
Usare `--absent` oppure `--duplicate --boots 1` per le altre campagne.
Eseguire prima il runner host per creare il decoder del confronto.

Non implementati: mapping MMIO VT-d, lettura CAP/ECAP, root/context/domain,
tabelle DMA, invalidazioni, attivazione, fault DMA e revoca. Nessun test
su Intel fisico, AMD-Vi o SMP. Passo successivo: mapping posseduto e verifica
delle capacita. Console VirtIO persistente sospesa fino ai gate IOMMU.
