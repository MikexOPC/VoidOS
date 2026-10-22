# Validazione dello snapshot PCI

22 ottobre 2026. Blocco topologia verificato; resolver multi-DRHD e attivazione DMA restano separati.

Contratto e fonti nel [design](design.md). C acquisisce e possiede lo staging; Rust puro deriva e verifica il grafo.
Nessuna nuova istruzione Assembly, scrittura PCI, pubblicazione RTADDR o attivazione TE.

## Host e analisi statica

- 255769 controlli C con MSVC /W4 /WX e AddressSanitizer.
- Errore in ognuna delle 536 letture del riferimento; output precedente invariato.
- Cento catture tramite adattatore nativo mock; cambi, nuove presenze e rimozioni rifiutati.
- Alias, capacita, parent/depth corrotti e coda non azzerata rifiutati.
- Quattro test Rust in debug e release: catene, ordine invertito, cicli, overlap, segmenti, multifunction e limiti.
- Clippy passa. Cppcheck: zero error/warning/performance/portability; sei style e nove information.
- I suggerimenti residui riguardano const, loop senza libc, callback coerenti con ABI e una funzione ECAM non usata dal sottoinsieme host; information include header di sistema e checkers.
- Regressione ECAM: 540760 controlli; regressione QI: 176349 controlli e prova nativa CPUID/CLFLUSH/MFENCE.
- Quattro combinazioni build incompatibili rifiutate prima della compilazione; sintassi di quattro script PowerShell verificata.

Evidenze host in kernel/build/pci-topology-host-final.log, pci-topology-ecam-host.log e pci-topology-qi-host.log.
Mock di ECAM/CPU/window nel test topology: non sono prove hardware. Rust usa overflow checks anche nella build freestanding.

Durante il bring-up corrette somme Rust che introducevano dipendenze dal panic runtime freestanding: contatori bounded/saturating e depth checked, con overflow checks attivi. Le immagini finali sono successive alla correzione.

## QEMU finale

QEMU 11.1.0 build v11.1.0-12130-ge470268ff4, Q35, TCG, una vCPU e 128 MiB, OVMF fornito con QEMU.
Campagne sequenziali: dieci boot consecutivi per ciascun profilo, 60 positivi complessivi sulle immagini finali.
Le due prove iniziali bridge/nested sono smoke aggiuntivi e non sostituiscono le campagne finali.

| Profilo | Boot | Evidenza in kernel/build/ |
|---|---:|---|
| root | 10 | ecam-firmware-7b933bd279674e11a0023ff961308e92 |
| bridge | 10 | ecam-firmware-94b91cda1b494b20a1d8829f7ed4e4e4 |
| nested | 10 | ecam-firmware-b7d0352836234919a30567968480c991 |
| ecam | 10 | ecam-firmware-05c04e1a7cf7470d871e4b4e56ac42d8 |
| qi | 10 | iommu-discovery-firmware-b543b11148c141829fa44161a364b4eb |
| ordinary | 10 | virtio-mapping-firmware-329c2ac91cf04960abd6ca3f030c8059 |

Topologia: GPU su bus radice (5 funzioni,0 bridge), dietro root port (6,1), dietro root port + x3130-upstream + xio3130-downstream (8,3,profondita3).
Il verifier legge i 16936 byte dello snapshot tramite CR3/QTest, confronta tutti i BDF e parent col grafo QMP e ID/classe/header/bus bridge con CF8/CFC.
Ogni boot topologico verifica source MCFG, entries inutilizzate azzerate, budget ripristinato, aperture revocate, guardie e cache UC.
Regressioni: ECAM scalare dietro bridge, QI con trace whitelist e translation/root non attive, console ordinaria con tastiera/mouse e cleanup.

## Costi misurati e limiti

La scansione percorre tutti i bus MCFG due volte: 16418/16424/16436 letture nei tre profili. Ogni lettura mantiene i controlli ECAM completi.
HostReadySeconds misura dal collegamento QMP fino al marker finale, prima del verifier: comprende boot e carico host, non isola il tempo della scansione e non e un benchmark hardware.

| Profilo | Min / mediana / max, secondi host |
|---|---|
| root | 63.750 / 81.203 / 127.891 |
| bridge | 56.000 / 60.969 / 65.328 |
| nested | 53.360 / 58.156 / 62.875 |

Questo costo resta nel diagnostico: i simboli capture/native/graph sono presenti nella sua map e assenti dalla map ordinaria.
Workspace e output statici richiedono 16936 byte ciascuno; nessun heap, lease o pin aggiuntivo del modulo.
ASan verifica accessi host; accounting QEMU verifica il budget fisico del percorso. Valgrind non disponibile su questo host Windows; coverage linee/branch N/D.
Nessuna dichiarazione di zero leak di tutto il sistema o isolamento DMA attivo.

Segmenti separati, piu radici e profondita8 sono test host; QEMU prova segmento0 e una root complex. Non certificati hardware fisico, SMP, hotplug atomico/ABA, ARI/SR-IOV e requester alias.
Resta il limite i440fx/timer gia documentato nel [report ECAM](../../ecam/documentation/validation.md); questo blocco certifica soltanto Q35.

## Immagini, struttura e riproduzione

| Immagine in kernel/build/ | SHA256 |
|---|---|
| pci-topology-final/kernel.elf | 35C009AFEE23A72DC7825EAD87CDA706089A71CEC7BD682E3F379C9206865ACE |
| pci-topology-ecam/kernel.elf | 6AF7608D27B535F0EB41CEAA1209BFEFB650B6FF3536DC84D8DDA0ADB649246F |
| pci-topology-qi/kernel.elf | 1A33B79A04E0D0732C0004FEF18ABE0004FF314D3252261133F9462A925CD407 |
| pci-topology-ordinary/kernel.elf | 2B9B72E37265338AEE2760CE212840EC041A7B702DE669A1D6EC52A117C733E1 |

Loader invariato: C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026.
Ogni copia ESP e ogni hash registrato e ricontrollato nell aggregato kernel/build/pci-topology-final-evidence.json.
Manifest kernel/build/pci-topology-structure.json: 18 sorgenti/adattatori; massimo400 righe, massimo5 file diretti; LF, whitespace, include e link verificati.

Host: powershell.exe -NoProfile -File arch/x86/kernel/drivers/bus/pci/topology/scripts/test.ps1.
Build: kernel/scripts/build.ps1 -TestPciTopology -OutputDirectory <cartella>.
QEMU: Python con -B arch/x86/kernel/drivers/bus/pci/ecam/scripts/firmware.py --topology --image <kernel.elf> --boots 10; aggiungere --bridge oppure --nested per gli altri profili.

## Prossimo blocco

Resolver degli scope DMAR sul grafo verificato: match specifico, sub-hierarchy, include-all, ambiguita e separazione fino a8 DRHD.
Lo snapshot non e una autorizzazione DMA; RTADDR/TE, fault e accessi consentiti/vietati richiedono successivi contratti e prove.
