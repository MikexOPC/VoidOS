# Prove DMA con `edu`: design e validazione (blocco 3)

23 ottobre 2026. Decisioni approvate: E1-A revoca con voce azzerata e invalidazione
via QI, E2-A scritture di configurazione solo sul device posseduto, E3-A codice `edu`
solo in `test/`, E4 completamento in polling.

## Cosa dimostra

Il blocco 2 attivava TE senza traffico DMA. Qui un requester reale (QEMU `edu`, 00:05.0,
SID 0x28) fa DMA con la traduzione attiva:

| Fase | Prova | Esito atteso |
|---|---|---|
| P1 | RAM(A)→edu, edu→RAM(B) | B riceve i 64 byte di A, nessun fault |
| P2 | edu→RAM(D), D in sola lettura | record SID 0x28, FR 5, scrittura, pagina D; D invariato |
| P3 | edu↔pagina di guardia senza foglia (0x1000000) | FR 5 in scrittura, FR 6 in lettura |
| P4 | revoca di B, poi edu→RAM(B) | FR 5 su B; B invariato |
| P5 | RAM(A)→edu, edu→RAM(C) | C riceve A: la revoca non tocca il resto del dominio |

FR 5/6: Intel VT-d 5.0 §7.1.3 e QEMU v11.1.0 `VTDFaultReason` (`VTD_FR_WRITE`,
`VTD_FR_READ`); una foglia assente ha R=W=0 e produce gli stessi codici.

## Modifiche ai moduli

- `bus/pci/ecam`: whitelist di lettura estesa a Command (04h) e BAR0 (10h);
  `x86_ecam_command` cambia solo i bit 1 (Memory Space) e 2 (Bus Master) con una
  scrittura a 16 bit, quindi la metà Status (RW1C) non viene mai riscritta. Fonti:
  PCI Local Bus 3.0 §6.2.2, Linux v6.18 `pci_regs.h`. Il lettore ECAM accetta
  accessi solo con il budget del pool invariato: il probe configura il device
  prima delle allocazioni VT-d e lo ripristina dopo il rilascio.
- `domains`: campo `Revoked` e `x86_vtd_domain_revoke`, che riscrive le foglie
  dalla funzione `expected` e riverifica; in caso di errore il dominio resta in
  stato 1 e va trattenuto.
- `activation`: `x86_vtd_activation_expect` accetta esattamente un nuovo record del
  requester posseduto con motivo, tipo e pagina attesi; ABI 456 → 464 byte.
- `routing`: la policy accettava solo la classe display (03h); ora accetta ogni
  endpoint di tipo 0 tranne i bridge (classe 06h). Serve anche per VirtIO.
- Invalidazione della revoca: batch globale esistente (context + IOTLB + wait), non
  page-selective. È corretto ma più ampio del necessario; la variante PSI (CAP.PSI,
  MAMV) resta un lavoro futuro.

## Peculiarità di QEMU osservata

Con una voce già nella IOTLB, QEMU v11.1.0 (`vtd_do_iommu_translate`, percorso hit)
restituisce il permesso in cache senza walk e senza record di fault. Una scrittura
su A (letto in P1) non lasciava quindi traccia: P2 usa D, mai letto dal device.
P4 è invece proprio la prova contraria: B è in cache da P1 e il fault compare solo
dopo l'invalidazione.

## Evidenze

| Prova | Risultato |
|---|---|
| `test/.../dma/scripts/test.ps1` | 2.336 controlli `edu` (ASan, /W4 /WX), cppcheck senza diagnostiche bloccanti |
| ECAM host | 540.696 controlli, compresa la scrittura di Command |
| Dominio host | 976.233 controlli, compresa la revoca |
| Attivazione host | 50.283 controlli, compresi i fault attesi |
| Routing host | 70.537 controlli, compresi i casi di classe |
| QEMU `--dma`, 10 boot consecutivi | 10/10 PASS: `kernel/build/ecam-firmware-4b7b130ea4514fd78c092d8c18634a2a` |
| Regressione `--activation`, 10 boot | 10/10 PASS: `kernel/build/ecam-firmware-d2455576721e4f74830351535327aae4` |
| Verificatore indipendente | 7 mutazioni respinte (fase mancante, motivo errato, flush di revoca mancante, record QEMU mancante, scrittura fuori whitelist, budget, fault non riconosciuto) |

## Limiti

- Solo QEMU TCG con una unità emulata; nessun hardware fisico.
- `edu` ignora gli errori DMA: il blocco è provato dai record di fault e dal
  contenuto della RAM, non dal completamento.
- Fault in polling (nessun interrupt), invalidazione globale, attivazione temporanea.
- Coverage N/D; Valgrind non disponibile; ASan solo host.

Riproduzione: `kernel/scripts/build.ps1 -TestIommuDma -OutputDirectory <dir>`;
`python -B arch/x86/kernel/drivers/bus/pci/ecam/scripts/firmware.py --dma --image <dir>/kernel.elf --boots 10`.
