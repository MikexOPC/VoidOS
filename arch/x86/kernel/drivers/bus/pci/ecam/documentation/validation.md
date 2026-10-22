# MCFG/ECAM: prove del lettore nativo

22 ottobre2026. [Contratto approvato](design.md). Modulo
arch/x86/kernel/drivers/bus/pci/ecam/ e soli adattatori build/probe.
Nessuna modifica a loader, boot ABI v5, scheduler, timer o reuse/.
Topologia completa, risoluzione degli scope e selezione DRHD sono ancora
da implementare; non confondere il lettore con il routing completo.

## Implementazione verificata

Parser/finder MCFG, massimo32 finestre in8 segmenti; checksum, riservati,
lunghezze, overlap fisico e bus. Sono ammesse finestre disgiunte nello
stesso segmento. Il calcolo usa bus effettivo, anche se first!=0.
Rust puro valida geometria e offset, C possiede snapshot e prestito.

Sessione non spostabile, CPU/PAT/inventario ricontrollati, esclusione RAM,
lease live, framebuffer, tabelle e alias. Prestito della page window
esistente: una pagina UC supervisor RO/NX, nessuna nuova tabella, lease
o allocazione fisica. Readback PTE e revoca sono del borrower reale.
Valore pubblicato solo dopo verifica finale e budget invariato.
Revoca incerta rende la sessione inutilizzabile, close rifiutato finche
la finestra non e di nuovo idle e verificata. Nessun config write.

L'API legge solo DWORD0/8/12/24. Non e un lettore generico di tutti i4096
byte o una promessa di snapshot topologico atomico; nessun hotplug,
_CBA, ARI, PASID o quirk non-ECAM implementato.

## Prove host eseguite

Runner scripts/test.ps1, MSVC14.44/Windows SDK, /W4 /WX /ASan; Rust
edition2024 con warning come errori e overflow-checks. Evidenze sotto
kernel/build/:

| Prova | Risultato | Evidenza |
| --- | --- | --- |
| C/ASan | 540760 controlli | ecam-host.log |
| Rust debug/release | 3 test per configurazione | ecam-host.log |
| Clippy policy/FFI | PASS | ecam-host.log |
| Cppcheck2.22 esaustivo | 0 bloccanti,10 style,10 information | ecam-host/cppcheck.xml |
| QI/coerenza, regressione | 176349 controlli e CPU nativa PASS | ecam-qi-host-regression.log |
| Kernel host, regressione | suite completa PASS | ecam-kernel-host-regression.log |
| Paging host, regressione | 8 gruppi x100 PASS | ecam-paging-host-regression.log |

Centosessioni,128 letture per sessione; controlli parser fino a32 entry,
checksum errato, tutte le lunghezze rifiutate, overlap bus/fisico,
finestre con first64 e segment1, indirizzi esatti, output protetto o
immutato su errore, alias PTE e RAM, feature CPU/inventario rifiutati,
callback fallito, revoca incerta e recovery, budget cambiato e ripristino.
Il finder copre lettura header/corpo fallita, tabella cambiata, duplicato
e assenza. Rust verifica tutti i65536 BDF, intervalli validi e whitelist.

La suite usa l'allocatore fisico reale. CPU, MMU e prestito
sono mock; il caricamento DWORD esegue il backend C reale su RAM host
allineata, non un accesso ECAM fisico. Il modulo non alloca RAM, e il
budget fisico e verificato a parte. ASan pulito non dimostra assenza di
leak fisici. Valgrind non disponibile, coverage linee/branch N/D.
Le note cppcheck comprendono header di sistema assenti e funzioni di
libreria non usate dal singolo harness: non e un report senza note.

## QEMU e prove indipendenti

QEMU11.1.0 build v11.1.0-12130-ge470268ff4, Q35/TCG, una CPU,128MiB,
OVMF fornito con QEMU. Runner scripts/firmware.py; VM possedute, campagne
sequenziali, deadline e cleanup host. Hash della copia ESP confrontati
con le immagini di input. Gli smoke non entrano nei conteggi finali.

| Campagna | Boot | Evidenze sotto kernel/build/ |
| --- | --- | --- |
| GPU sul bus radice | 10 | ecam-firmware-9b9b28b1d153471b9f6a17736e2a76e1 |
| GPU dietro root port, bus1 | 10 | ecam-firmware-f71b8a25707c4f4888d5f35ef2daeefc |
| Assenza filtrata, positiva attesa | 10 | ecam-firmware-585231e6a5e74f0698e4f1c7878499fd |
| MCFG duplicata, rifiuto atteso | 1 | ecam-firmware-c042ddc1d8c640cead6cfaf4f0be5405 |
| QI/coerenza, regressione | 10 | iommu-discovery-firmware-951fc9bf2f584a4d927e227664d03763 |
| Console ordinaria, input/cleanup | 10 | virtio-mapping-firmware-ca08f1d8833d47f0aa83c21ab9b6750b |

Totale50 boot positivi sul profilo Q35 (inclusi10 con assenza filtrata),
dieci consecutivi per campagna, piu un negativo atteso. Nessun fault
inatteso nelle campagne finali Q35. Il precedente smoke i440fx fallito
resta separato e descritto sotto, non riclassificato come PASS.

Il root port e pcie-root-port id=rp1, chassis1, slot2; virtio-vga e
collocata su rp1. Il guest usa il BDF passato dal loader, non bus1
codificato nel lettore. La prova richiede bus diverso da0 per impedire
un falso positivo con GPU ancora sul bus radice.

QTest legge la MCFG fisica, verifica checksum/revisione/campi e ricostruisce
la pagina ECAM con una formula indipendente. Confronta gli8 DWORD del
guest con letture MMIO ECAM e con CF8/CFC mentre la VM e ferma; ripristina
il selettore legacy dell'host di test. Non e un nuovo backend CF8 del
kernel. Sono160 letture guest confrontate nei20 boot con ECAM presente.

QTest verifica inoltre PAT/UC e percorre il CR3 loggato fino alla page
window: tutti i16 leaf sono vuoti e le due guardie invariate dopo la
revoca. Il borrower reale verifica la PTE RO/NX/UC durante la lettura;
lo snapshot esterno certifica lo stato dopo il cleanup, non un'immagine
indipendente della PTE durante l'accesso. Budget guest prima/dopo uguale.
Il positivo filtrato non esegue letture configurazione; il negativo
duplicato deve rifiutare senza KERNEL_ECAM_VALUE o KERNEL_ECAM_OK.

La regressione QI completa1280 batch reali emulati, con confronto RAM,
traccia, stop e budget. Non abilita traduzione DMA o pubblica la root.

## Immagini e riproduzione

SHA256 sotto kernel/build/:

| Immagine | SHA256 |
| --- | --- |
| ecam-final/kernel.elf | B50F23352796EFF80682AE9331F26C099C2307D087A22B9E8CB38D0C17636E21 |
| ecam-absent/kernel.elf | 8B6B9DF10B074AFB392DC63E82E765C713E5E9E3382071F532C8A6BAB89672B9 |
| ecam-qi-regression/kernel.elf | 6C7EB6DCB86A9656C1F4A9A33A9E4E26CA747A7209B4658A1961C125568B9442 |
| ecam-ordinary/kernel.elf | 01B81D61A73E376F47F30F1A15ECC638EC403FCDD5409FD69D808181CDB27DA8 |

Loader invariato:
`C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.
Build con -TestPciEcam e directory output esplicita; -TestPciEcamAbsent
crea la sola variante con filtro. Nessuna installazione nella ESP normale.
Il MAP ordinario deve escludere reader/parser/probe ECAM e filtro.
Verificato nel MAP dell'immagine ordinaria usata dalla campagna finale.

Esecuzione host: powershell.exe -NoProfile -File
arch/x86/kernel/drivers/bus/pci/ecam/scripts/test.ps1.
QEMU: Python3 con -B e scripts/firmware.py del modulo, --image e --boots10.
--bridge usa root port; --duplicate richiede --boots1. --absent deve usare
la variante -TestPciEcamAbsent e certifica soltanto l'assenza filtrata.

Manifest: kernel/build/ecam-structure.json,22 sorgenti/adattatori,
massimo400 righe (script build), massimo5 file diretti. Include, link,
LF, whitespace e diff verificati, con test centralizzati. Nessun commit
o push richiesto per questo blocco; preesistenti modifiche conservate.
Aggregato verificato: kernel/build/ecam-final-evidence.json.

## Debug e limiti incontrati

Corretto durante il ciclo un calcolo Rust che introduceva un riferimento
al panic runtime non linkabile freestanding; il calcolo finale usa add
controllata e rifiuta overflow. Test e immagini finali sono successivi.

Il primo smoke i440fx e fallito dopo KERNEL_ECAM_ABSENT, nella fase timer:
KERNEL_WATCHDOG_FAILURE=3/CAPTURE, LAPIC_TIMER_INVALID. Non e contato come
PASS. Evidenza kernel/build/ecam-firmware-8396b1d8921a48b795be6bd922ddeab7/;
causa non isolata, nessuna modifica timer nel perimetro. La piattaforma
i440fx non e certificata da questo blocco. Non viene dichiarato che il
crash sia stato corretto dal lettore ECAM.

La proprieta QEMU MCFG=unmapped non ha rimosso la tabella dopo OVMF:
kernel/build/ecam-q35-absent-smoke.log registra il rifiuto del verifier,
non un PASS di assenza. Il test finale di assenza usa invece l'adattatore
KERNEL_TEST_PCI_ECAM_ABSENT in test/: legge davvero l'header ACPI ma filtra
la firma MCFG prima del finder. Il guest stampa il marker esplicito
KERNEL_ECAM_FILTERED_ABSENCE_TEST; questo e un test di assenza simulata
della sorgente su Q35, non hardware Q35 fisicamente privo di MCFG.

Per il negativo duplicato la VM contiene la tabella QEMU e una MCFG
aggiunta: il finder deve rifiutare prima delle letture configurazione.
Non e una prova di accesso a RAM tramite ECAM; quel rifiuto e nei test host.

## Limiti e prossimo blocco

Solo Q35/TCG e letto nativamente in QEMU. Segmenti nonzero e first!=0
sono provati nei mock/policy, non su hardware segmentato reale. Il loader
GPU continua a rifiutare segmenti nonzero. Le regioni MCFG non incontrate
nella mappa EFI sono ammesse dopo esclusione RAM/alias, come nel design;
non e una validazione ACPI _CRS. Nessuna prova su hardware fisico o SMP.

Il lettore e il primo prerequisito della scelta B. Seguono snapshot
topologico, bridge/parent e resolver multi-DRHD con precedenza include-all.
La traduzione DMA resta disattivata; attivazione/fault e ACCESS_PLATFORM
richiedono i propri contratti e prove, prima della console VirtIO ordinaria.
