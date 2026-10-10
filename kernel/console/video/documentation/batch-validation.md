# Batch verificato — 10 ottobre 2026

[Contratto scelto e limiti](batch-design.md). Riusa il disegno testo bounded:
una scansione completa per gruppo di massimo8 celle adiacenti, verifica finale,
nessuna autorità memorizzata tra gruppi. Errori terminali, nessun rollback
pixel fittizio. Il percorso singolo e il suo ABI rimangono disponibili.

## Quaranta boot finali

| Profilo | Immagine | Evidenze in kernel/build/ | Boot |
| --- | --- | --- | ---: |
| Batch | console-batch-final/kernel.elf | ps2-input-firmware-150b188bbeeb4e69a1f006b1a3a1f2a5/ | 10/10 |
| Singolo | console-batch-single/kernel.elf | ps2-input-firmware-7c7496c3a7bc4a4fa7f069c215c88d43/ | 10/10 |
| Ordinario | console-batch-ordinary/kernel.elf | framebuffer-live-75392a9769c543b8ba9bb6c1119ddd65/ | 10/10 |
| Senza video | console-batch-ordinary/kernel.elf | framebuffer-live-8e7fa9c91ef640fda21165690504e548/ | 10/10 |

SHA256 batch: `968E8FAC6772A480ABC3246F7A8D2EFEB8786B1720168BF58B028CCCE88171F7`.
Singolo: `460898AF48A0A4220FA68E93DF6376D5AAC0EE2BB0FCC838FC3D7AC0830796E8`.
Ordinario: `9C09778A083BC17B6ADDEDE4898B65AA9F0B36CF15AF26CE54CB17F39C9B89D0`.
Immagini/loader verificati; nessuna installazione ESP. Nessun fault inatteso.

Batch: 28480 celle in3560 gruppi; zero perdite input osservate, tastiera italiana,
shell seriale e progressi IRQ/HPET verificati. Tutti i1024 pixel fisici e1024
visibili dell'ultimo gruppo confrontati per boot con il font Barlow rasterizzato.
Il backend verifica store/readback anche nei gruppi intermedi, senza un oracle
esterno per ciascun frame. Budget fisico20436 prima e dopo in tutti i20 boot
input; il valore assoluto è proprio della nuova immagine, non di quelle storiche.

## Costo osservato

| Metrica | Singolo | Batch8 |
| --- | ---: | ---: |
| Celle osservate | 3540 | 28480 |
| Gruppi osservati | 3540 | 3560 |
| Media profilata per cella | 2,0176 ms | 0,3016 ms |
| Media profilata per gruppo | 2,0176 ms | 2,4127 ms |
| Massimo flush/gruppo | 11,6548 ms | 15,1028 ms |
| Gruppi oltre10ms | 1 | 4 |

Rapporto osservato delle medie per cella:6,6901. Le campagne non sono un
benchmark controllato: letture clock, pause/carico host, profili e immagini
diverse incidono sul risultato. Il cambiamento strutturale è una scansione
completa per8 celle anziché8 scansioni; non è una garanzia di speedup hardware.
La fase inventario pesa ancora90,402% del tempo batch profilato. SlowCells=32
significa4 gruppi lenti di8 celle; MAX_PM riguarda il gruppo intero. Il gate
diagnostico100ms passa, l'obiettivo10ms rimane aperto. Nessuna latenza
end-to-end dell'input, garanzia realtime o protezione di uno stallo C IF0.
Aggregati: `kernel/build/console-batch-comparison.json` e `console-batch-summary.json`.

## Test e review

`kernel/build/console-batch-host/`: MSVC /W4 /WX e C/ASan,2048 workload sparse
(256 mask per8 budget), confini riga/gap, copied owner, disallineamento,
rientro, callback fallita, tick zero, mutazioni griglia/geometria, overflow
e accounting delle celle. Gateway: UTF-8 misto e buffer esatto32 byte,
count1/8, count0/9, bordi, scalar invalido, colore/null/overflow, IF ripristinato.
Snapshot mock: pointer/baseline ripristinati anche dopo failure del gruppo.
Regressioni griglia, flusher/misuratore singoli, owner/stack mock e HPET passano.
Rust:4 cursor e4 timing in debug/release, Clippy verde; policy numerica condivisa.
Python:8 active e5 firmware passano, incluse somme/celle/gruppi e pixel incompleti.

Cppcheck senza error/warning/performance/portability; restano13 style/18 information
nel subset base e33 style/31 information nell'active. Review manifest senza errori:
massimo sorgente293 righe, massimo5 file diretti. Diff finale verificato.
Il batch non alloca memoria dinamica; ASan copre C host, non Rust/ASM/RAM fisica.
Budget QEMU verificato separatamente; Valgrind non eseguito, coverage N/D.

Blocchi conclusi: misure per fase e batch scelto. Rendering ancora diagnostico,
shell seriale. Collegare l'output operativo richiede mantenere l'autorità
valida nella task shell e limitare il lavoro tra le finestre IRQ; questo
checkpoint non dichiara tale collegamento o una GUI implementati.
