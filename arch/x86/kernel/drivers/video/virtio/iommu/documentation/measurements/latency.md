# V2d: misure di latenza della console VirtIO

23 ottobre 2026. Decisioni approvate: M1-A accumulatori nel kernel riportati all'exit, M2-A
confronto GOP contro V2c sullo stesso scenario (10 boot ciascuno), M3-A nessuna soglia, M4 solo
misure.

## Strumenti

- `kernel/diagnostics/latency.{c,h}`: numero, minimo, massimo, totale, contatore saturato e
  istogramma a 8 fasce (potenze di 4 da 64 tick). Non va mai in overflow; il report rifiuta un
  accumulatore incoerente. Host: 884 controlli.
- Misure: present intero (TRANSFER + FLUSH fenced), attesa del solo FLUSH (`VGPU_QUEUE.LastWait`,
  dal notify al completamento), area presentata, polling dei fault per flush, sezione IF=0
  dell'arresto. I flush della console erano già misurati.
- Il runner interattivo controlla la coerenza di ogni report (fasce = numero, minimo ≤ media ≤
  massimo, numero uguale ai present o ai polling). `kernel/console/video/scripts/pointer/latency_summary.py`
  produce la tabella dalle due evidenze.

## Risultati (mediana su 10 boot, tra parentesi il massimo tra i boot)

Tick del timer PM (3,579545 MHz) del clock virtuale di QEMU TCG.

| Metrica | GOP | V2c |
|---|---|---|
| Flush per boot | 1014 | 977 |
| Flush, media | 15.106 tick (4,2 ms) | 40.832 tick (11,4 ms) |
| Flush, massimo | 81.769 (211.828) | 257.408 (420.339) |
| Present, media | — | 3.551 tick (1,0 ms) |
| Present, massimo | — | 124.918 (377.482) |
| Attesa FLUSH, media | — | 471 tick (0,13 ms) |
| Polling fault, media | — | 988 tick (0,28 ms) |
| Area per present, media | — | 50.734 pixel (massimo 202.280) |
| Sezione IF=0 all'arresto | — | 317.844 tick (89 ms) |
| `DrainSeconds` host, mediana | 0,1 s | 0,4 s |

Evidenze: GOP `kernel/build/pointer-interactive-check-3c9a28203a89410c956e5f05dbb78cbe`,
V2c `kernel/build/pointer-interactive-check-efe51c2da094418ba066d06fbed1fcf3`; fallback ×1
`kernel/build/pointer-interactive-check-9df8d0ddcfc946978009d30c89cb3607` (passa con la
strumentazione attiva).

## Analisi

- Un flush V2c costa circa 2,7 volte un flush GOP: circa 25.700 tick in più in media.
- Present e polling insieme ne spiegano circa 4.500. **Circa 21.000 tick per flush non sono
  attribuiti** da queste misure. Ipotesi da verificare con una misura dedicata, non conclusioni:
  - il disegno nella superficie passa per controlli di validità del target (`x86_vgpu_surface_check`
    e le letture di stato CPU che fa) a ogni sessione pixel;
  - i rettangoli del puntatore uniti a gruppi di celle lontani allargano le transazioni.
- L'area media presentata (50.734 pixel, circa 1/15 dello schermo 1024x768) è alta per un
  gruppo di 8 celle (1.296 pixel). Il rettangolo unico di Z2-A unisce i riquadri del puntatore
  con celle lontane; due rettangoli separati ridurrebbero il trasferimento.
- La sezione IF=0 dell'arresto (circa 89 ms) è lunga: comprende disattivazione, invalidazione e
  rilascio. Accade una volta, a sessione finita, ma va considerata se un giorno un interrupt
  dovrà essere servito anche lì.
- La sezione IF=0 dell'avvio **non è misurata**: comincia prima che esista il clock PM catturato.
- `DrainSeconds` misura la velocità dell'emulazione sull'host, non una latenza reale.

## Possibili passi successivi (non fatti, M4)

1. Misurare separatamente il disegno nella superficie e i controlli del target, per attribuire
   i 21.000 tick.
2. Present con fino a 3 rettangoli (celle, puntatore vecchio, puntatore nuovo) invece del loro
   inviluppo.
3. Interrupt di completamento al posto del polling, se le attese fenced diventano il collo di
   bottiglia; oggi non lo sono (circa 0,13 ms di media).
