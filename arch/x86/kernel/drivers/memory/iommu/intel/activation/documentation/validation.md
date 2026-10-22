# Attivazione VT-d e fault primari: validazione (blocco 2)

22 ottobre 2026. [Design, fonti e decisioni](design.md): D1-C' negare per default,
D2 polling, D3 `edu` per il blocco 3, D4 attivazione temporanea.

## Cosa e implementato

Nel solo profilo diagnostico `-TestIommuActivation` (immagine routing/ECAM), dopo
la selezione GPU–DRHD: mapping RW dell'unica unita, QI avviata, dominio privato
della GPU verificato e pulito dalla cache CPU, poi `RTADDR=root`, SRTP, invalidazione
globale context+IOTLB, TE=1. Con la traduzione attiva si leggono i fault primari
(polling, FECTL.IM resta 1); poi TE=0, nuova invalidazione globale, `RTADDR=0`,
fine dell'attivazione e rilascio di dominio, coda e mapping con budget ripristinato.

Gli altri requester dell'unita non ricevono context entry: con TE=1 le loro DMA
sarebbero rifiutate e registrate come "bloccate"; solo un fault della GPU e un
errore. Nessuna scrittura ai device PCI. Ogni errore dopo la prima scrittura porta
a RETAINED: nulla viene rilasciato e la coda QI rifiuta lo stop finche l'owner
dichiara TES possibile.

Codice: `activation/` (`sequence.c` 139 righe, `faults.c`, `backend/mmio.c` con
whitelist propria, policy Rust pura senza unsafe). Adattatore in `invalidation/`:
campo `Status` per i bit GSTS TES/RTPS posseduti dall'attivazione. Probe in
`test/.../activation/integration/`, attivato dal probe del routing.

## Host

| Prova | Risultato |
|---|---|
| C/ASan, MSVC /W4 /WX, modello registri + modello QI reale | 49.891 controlli: 64 cicli SRTP/TE con e senza fault, fault bloccati/posseduti, overflow PFO, record che si ripresentano (quattro passate), log limitato a 16, FRI fuori range, riservati, rifiuti di begin/enable senza scritture, whitelist, retention per RTADDR, SRTP e TE non serviti |
| Rust | 5 test debug e 5 release, Clippy e warning come errori; oggetto senza riferimenti a panic |
| cppcheck 2.22.0 | zero error/warning/performance/portability |
| Regressione QI | 176.368 controlli (prima 176.349) |
| Regressione routing | 70.537 controlli |

Evidenze in `kernel/build/iommu-activation-host`, `iommu-activation-qi-host`,
`iommu-activation-routing-host`; log in `kernel/build/activation-campaign/`.

## QEMU

QEMU 11.1.0, Q35/TCG, una vCPU, 128 MiB, OVMF di QEMU, `intel-iommu,intremap=off,
caching-mode=on,device-iotlb=off`. Ottanta boot, tutti exit 0:

| Profilo | Boot | Evidenza in kernel/build/ |
|---|---:|---|
| Attivazione, GPU sul bus radice (endpoint, SID 0x0008) | 10 | ecam-firmware-054635974b3943f58f9d176893b0194f |
| Attivazione, GPU dietro root port e switch (sub-hierarchy, SID 0x0300) | 10 | ecam-firmware-58c755b708a54165b827b3478b25dd40 |
| Regressione routing radice / annidato / assente | 30 | ecam-firmware-7a4b69db…, fb62fa93…, 4f36bb6e… |
| Regressione QI | 10 | iommu-discovery-firmware-d24cf640722541adb84b2af1fb207ee5 |
| Regressione topologia dietro bridge | 10 | ecam-firmware-f8de250011c7403a87c3891526a412b8 |
| Regressione console ordinaria (`-Ps2Mouse -ConsoleShell -InteractiveInput`) | 10 | virtio-mapping-firmware-de1b79c246ec4826ab20855fd3a08497 |

Il verifier `activation/scripts/firmware.py` confronta il report del guest con la
trace QEMU (`vtd_reg_write`, `vtd_reg_dmar_root`, `vtd_dmar_enable`, descrittori QI,
`vtd_frr_new`, `vtd_dmar_fault`, `vtd_switch_address_space`):

- GSTS 0x04000000 → 0xC4000000 (TES, RTPS, QIES) → 0x44000000; RTADDR 0 → root → 0;
  FSTS senza PPF/PFO in tutte e tre le fasi.
- Sequenza esatta delle scritture di controllo (13 scritture per boot, tutte nella
  whitelist); una sola `vtd_reg_dmar_root` sulla root del dominio in modo legacy;
  una invalidazione globale dopo SRTP e prima di TE=1, una dopo TE=0.
- La GPU passa allo spazio indirizzi tradotto con TE=1 e torna indietro con TE=0.
  QEMU sposta in traduzione 5 device nel profilo radice e 8 in quello annidato.
- Zero fault registrati in tutti i venti boot; budget fisico invariato.

Prima della campagna il verifier e stato provato contro sette mutazioni del boot
reale (scrittura fuori whitelist, invalidazione mancante, doppio TE, fault non
riportato, RTADDR diverso, GSTS errato, fault del requester): tutte rifiutate.

Il primo tentativo della regressione ordinaria e fallito per una build sbagliata
(mancava `-InteractiveInput`, richiesto da `KERNEL_POINTER_READY`); ricostruita e
ripetuta, 10/10.

## Audit e immagini

Mappa dell'immagine ordinaria: `audit-symbols.ps1` PASS (nessun oggetto di test) e
assenti `x86_vtd_activation_enable`, `x86_vtd_activation_probe`,
`x86_vtd_route_select`, `x86_qi_start`, `x86_scheduler_probe`. Le immagini routing,
QI e topologia non contengono simboli di attivazione; l'immagine di attivazione li contiene.

| Immagine in kernel/build/ | SHA256 |
|---|---|
| iommu-activation-final/kernel.elf | 05C7CCAC2502EE3D2E4998E67FB8B2FBF6422E41978C996C7EFF3B0349DC3923 |
| iommu-activation-routing/kernel.elf | 60F3DF5D24AA0BB882B70D7E100A5A6CAFBD221817BC157FE3C44C1B5CAE14BD |
| iommu-activation-qi/kernel.elf | 0F5113AB32DBB42F98BB9359A4DFA2AD173D78AA9BFE07705DE09E502F06DA2B |
| iommu-activation-topology/kernel.elf | 9D3AC60EDE8172EABA4D16C520EC91CDD173C545F3E842766D6E4C57DF27695E |
| iommu-activation-ordinary/kernel.elf | 513857779EF088710C424BC7BFF033633F5BF23902795CC8869FBB855CE76D4E |

Loader invariato: C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026.

## Limiti

- **Nessun isolamento DMA dimostrato.** Nessun device ha fatto DMA durante TE=1:
  il blocco prova pubblicazione, attivazione, invalidazione e teardown, non che un
  accesso consentito passi e uno vietato venga bloccato. Quello e il blocco 3 (`edu`).
- Una sola unita VT-d emulata; nessun hardware fisico. I fault sono provati solo
  nel modello host: QEMU non li genera senza traffico DMA.
- Fault per interrupt (FEDATA/FEADDR), write-buffer flushing (CAP.RWBF=1), modo
  scalable e piu unita restano rifiutati o non implementati.
- In QEMU VirtIO aggira l'IOMMU senza `iommu_platform=on` e ACCESS_PLATFORM: la GPU
  in traduzione qui non prova nulla sul percorso VirtIO.
- Coverage linee/branch N/D; Valgrind non disponibile; ASan solo host.

Riproduzione: `activation/scripts/test.ps1`; `kernel/scripts/build.ps1 -TestIommuActivation`;
`python -B arch/x86/kernel/drivers/bus/pci/ecam/scripts/firmware.py --activation [--nested] --image <kernel.elf> --boots 10`.
