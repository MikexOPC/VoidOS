# Validazione del resolver GPU–DRHD

22 ottobre 2026. Blocco routing verificato nel profilo descritto nel [contratto implementato](resolver.md).

C valida topologia/output e pubblica la scelta; Rust puro controlla DMAR, path e precedenze. Nessuna nuova istruzione Assembly o scrittura hardware.

## Host

- 70537 controlli C/ASan, MSVC /W4 /WX: 1000 selezioni, tutti i 65536 BDF, 992 mutazioni dei metadati DMAR (972 rifiutate), output invariato, alias e aperture protette.
- Dieci test Rust in debug e release: sei del resolver e quattro del grafo riusato. Massimi8 unita/32 scope/path8, segmenti, ordine, sovrapposizioni e identita cambiata.
- Clippy passa. Cppcheck: zero error/warning/performance/portability, due style unusedFunction del sottoinsieme analizzato e undici information (header di sistema e checker).
- Regressione host topologia: 255769 controlli. Regressione QI: 176349 controlli e test nativo CPUID/CLFLUSH/MFENCE.
- Quattro combinazioni build incompatibili rifiutate prima della compilazione; sintassi di quattro script PowerShell verificata.

Evidenze in kernel/build/iommu-routing-host-final.log, iommu-routing-topology-host.log e iommu-routing-qi-host.log.
Corretto nel bring-up un path di ricerca progetto nel runner e un riferimento panic_const_add_overflow generato da iteratori enumerate: cicli bounded con accessi checked, overflow checks ancora attivi.

## QEMU finale

QEMU11.1.0 build v11.1.0-12130-ge470268ff4, Q35/TCG, una vCPU,128 MiB, OVMF fornito con QEMU.
Sessanta boot positivi: dieci consecutivi per ciascun profilo finale. Lo smoke annidato iniziale e aggiuntivo, non sostituisce le prove finali.

| Profilo | Boot | Evidenza in kernel/build/ |
|---|---:|---|
| root | 10 | ecam-firmware-61962c14b8374b1ea305d3749ff4c739 |
| nested | 10 | ecam-firmware-916e1dab8fd74689b266c235f6326b05 |
| absent | 10 | ecam-firmware-58ada3083c574fc3aea357a23d6198fa |
| topology | 10 | ecam-firmware-30a635482a424d7fb24fab9d93cce52e |
| qi | 10 | iommu-discovery-firmware-d82b0e3201df48faa54fb4851ce146a2 |
| ordinary | 10 | virtio-mapping-firmware-a36cf6cd1149486a86305240a33ec18a |

Root e nested: DMAR reale copiata dal guest e confrontata con parser indipendente dalla memoria fisica; snapshot PCI gia confrontato col grafo QMP e CF8/CFC.
Request e risultato40 byte letti tramite walk CR3. Il resolver Python ricostruisce gli insiemi di device degli scope e verifica unita, nodo, scope, motivo, segmento e requester.
Il caso root risolve endpoint/type1; quello annidato risolve sub-hierarchy/type2 per GPU dietro root port + x3130-upstream + xio3130-downstream.
Ottanta fixture guest sintetiche verificano otto candidati, fallback su unit7, precedenza di unit3, gerarchia e rigetto di scope duplicati con risultato sentinella invariato.
Non sono prove di otto IOMMU hardware: la configurazione QEMU usa una sola unita VT-d emulata. Segmenti distinti e massimi sono prove host/sintetiche.
Assenza: la VM non espone intel-iommu, il finder restituisce assenza e il verifier ricontrolla tutte le firme della XSDT/RSDT. Non viene usato un filtro ACPI simulato.
Durante tutti i trenta boot routing la trace vtd_reg_write resta vuota. Nei venti con DMAR il verifier legge TES31=0 e RTADDR=0. Nessun isolamento DMA dichiarato.
Regressioni: dieci snapshot dietro bridge senza nuovo resolver, dieci QI con stop/whitelist e dieci console ordinaria con tastiera/mouse e cleanup.

## Memoria, struttura e limiti

Nessun heap o lease aggiuntivo nel resolver. La memoria delle fixture/scratch e statica nel solo diagnostico; gli output C restano privati fino al ritorno.
Budget e revoca della finestra verificati per ogni boot pertinente; ASan verifica gli accessi host. Valgrind non disponibile su questo host Windows; coverage linee/branch N/D. Non e una prova di zero leak dell intero OS.
I simboli del resolver e delle fixture sono assenti dalle map di topologia, QI e console ordinaria. Il contratto boot resta v5.
Manifest kernel/build/iommu-routing-structure.json: 21 sorgenti/adattatori, massimo400 righe e massimo5 file diretti; LF, whitespace, include e link verificati.
La scansione topologica resta esaustiva e costosa, come nel suo checkpoint; questo blocco non la introduce nel boot ordinario.
Nessuna prova hardware fisico, SMP/hotplug atomico, namespace, ARI/SR-IOV/ATS/PASID o alias requester. Scelta logica e geometria indirizzi non autorizzano mapping MMIO o DMA.
Restano i limiti Q35/i440fx gia documentati nel [report ECAM](../../../../../bus/pci/ecam/documentation/validation.md).

## Immagini e riproduzione

| Immagine in kernel/build/ | SHA256 |
|---|---|
| iommu-routing-final/kernel.elf | 4142F404DC39519E16122A487A687C197DD1D4D5B3EE715DCDCBBEDF2CD2AD4B |
| iommu-routing-topology/kernel.elf | B32CE38480982B8700D8B3CAB6B1E056CEFA17926656BA62BD10526279781699 |
| iommu-routing-qi/kernel.elf | 28423A29FD737C5E2781C6B52ADA9E481B54CD03C7F6DCB2B39CCCAD3A3B14BE |
| iommu-routing-ordinary/kernel.elf | 5A10FACDB5C6355FFE07E9D2C8EB7C9B8A3D7EE54047A2D13E1DC76E75160804 |

Loader invariato: C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026.
Aggregato verificato: kernel/build/iommu-routing-final-evidence.json; hash ricontrollati anche nelle sessanta copie ESP.
Host: powershell.exe -NoProfile -File arch/x86/kernel/drivers/memory/iommu/intel/routing/scripts/test.ps1.
Build: kernel/scripts/build.ps1 -TestIommuRouting -OutputDirectory <cartella>.
QEMU: Python -B arch/x86/kernel/drivers/bus/pci/ecam/scripts/firmware.py --routing --image <kernel.elf> --boots 10; aggiungere --nested oppure --routing-absent.

## Prossimo blocco

Adattare ownership registri/QI e pubblicazione del dominio all unita selezionata, definendo prima la sorte degli altri requester quando TE viene attivato.
Attivazione, fault DMA, invalidazione prima del riuso RAM e prove consentito/vietato rimangono gate separati prima di ACCESS_PLATFORM e console VirtIO persistente.
