# Profilo del flush della console (dopo V2d)

23 ottobre 2026. Decisioni approvate: P1-A timer per fase solo nelle immagini di profilo
(`-ProfileVgpuFlush`), P2-A matrice di tre immagini × 5 boot, P3 solo misure.

## Immagini

- **A** console GOP (`-ProfileVgpuFlush`);
- **B** dominio GPU tradotto, arena e superficie come in V2c, ma console sul GOP
  (`-VgpuIommu -ProfileVgpuFlush -ProfileVgpuNoPresent`);
- **C** V2c completa (`-VgpuIommu -ProfileVgpuFlush`).

Fasi (`framebuffer/console/profile.{c,h}`, accumulatore V2d): flush intero, render, singolo
gruppo di celle, verifica dell'inventario paging nella sessione pixel, controlli del target,
contabilità del disegno live (`prepare`), present, polling dei fault. Il runner controlla che
ogni report sia coerente, che i campioni del flush siano uno per flush e che render + present +
polling non superino il flush. `kernel/console/video/scripts/pointer/profile_summary.py` produce
la tabella.

## Risultati: tick PM per flush, mediana su 5 boot

| Fase | A (GOP) | B (tradotto, GOP) | C (V2c) |
|---|---|---|---|
| Flush | 16.307 | 37.224 | 43.653 |
| Render | 15.979 | 34.560 | 37.613 |
| Gruppi di celle | 15.586 | 33.497 | 36.490 |
| **Verifica inventario** | **11.133** | **27.155** | **29.278** |
| Controlli target | 0 | 0 | 58 |
| Prepare (contabilità live) | 407 | 1.459 | 1.506 |
| Present | 0 | 7 | 4.266 |
| Polling fault | 0 | 1.863 | 588 |
| Resto del gruppo (disegno) | 4.009 | 4.882 | 5.624 |
| Resto del flush | 295 | 1.011 | 1.070 |

Verifica per sessione pixel: A 11.556, B 27.789, C 30.152 tick.

Evidenze: A `kernel/build/pointer-interactive-check-d23896c81544492ea43a30bf8c5b6565`,
B `kernel/build/pointer-interactive-check-5f30e611e0104454a545eb67cb4318d9`,
C `kernel/build/pointer-interactive-check-8bc6df44815740859ed44079593ac28c`.
Regressione: l'immagine V2c senza profilo passa (1 boot).

## Conclusione

- **L'ipotesi 1 regge.** Da A a C il flush cresce di circa 27.300 tick; la verifica
  dell'inventario ne spiega circa 18.100 (due terzi). È già quasi tutta presente in **B**, dove
  la console non disegna nella superficie: il costo viene dalla **dimensione dell'inventario**
  (tabelle e 1000 foglie della superficie, arena, mapping VT-d) che ogni sessione pixel riscandisce,
  non dal target VirtIO.
- I controlli del target (ipotesi 2) sono trascurabili: 58 tick per flush.
- Il present aggiunge circa 4.300 tick, il polling 600-1.900, `prepare` circa 1.100 (anch'esso
  scandisce lo stato del pool). Il disegno vero e proprio cresce di circa 1.600 tick.
- I timer stessi costano: le immagini di profilo sono 1.000-3.000 tick più lente delle
  corrispondenti senza profilo (V2d: GOP 15.106, V2c 40.832).

## Correzioni possibili (non fatte, P3)

1. **Verificare l'inventario una volta per flush** invece che a ogni sessione pixel: stessa
   garanzia nel modello cooperativo (nessun altro task cambia le tabelle durante un flush), costo
   diviso per i gruppi del flush.
2. **Verifica per authority**: per le regioni registrate (superficie, arena, IOMMU) confrontare
   identità delle tabelle e una firma delle foglie invece di riscandire 1000 PTE ogni volta.
3. Unire 1 e 2. Entrambe toccano il modulo paging, verificato: vanno progettate e discusse.
