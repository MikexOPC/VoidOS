# Roadmap di VoidOS

La meta è un'unica immagine che avvia tutte le CPU, legge programmi da
disco, li esegue in ring 3 isolati e resta utilizzabile. Lo
[stato corrente](status.md) indica quali percorsi sono già verificati.

| Ordine | Obiettivo | Risultato richiesto |
| --- | --- | --- |
| 1 | Consolidamento | Documentazione breve, divisione di `process.c` dopo il commit del lavoro GUI, build e CI, immagine unica, scheduler preemptive su tutte le CPU e risorse senza limiti fissi minuscoli. |
| 2 | Processi | Più processi con spazi di indirizzamento separati, syscall interrompibili, creazione, terminazione, recupero delle risorse e IPC generale. |
| 3 | Storage | Driver a blocchi, cache, VFS e un filesystem prima in lettura e poi in scrittura. VirtIO-blk per QEMU, poi NVMe o AHCI. |
| 4 | Esecuzione da file | Caricamento di ELF dal filesystem, init e shell in ring 3, libreria utente minima. |
| 5 | Sicurezza | Entropia, stack canary, W^X verificato e decisioni su KPTI e mitigazioni speculative. |
| 6 | GUI | Prima fetta interattiva solo dopo la chiusura del cancello qui sotto. |
| 7 | Hardware reale e periferiche | USB con xHCI dopo la GUI, x2APIC, avvio su PC fisico e poi rete. |

Per immagine unica, scheduler, processi e storage, ed evoluzione delle
protezioni, la scelta fra alternative spetta all'utente prima del codice.
Ogni obiettivo si chiude con una verifica del percorso d'uso sull'immagine
di produzione. Un difetto di memoria, isolamento o stabilità riapre il
lavoro coinvolto.

## Cancello «base solida» prima della GUI

I punti 1–5 devono essere chiusi. Sull'immagine di produzione devono
valere tutte queste condizioni.

- SMP, shell e processi utente nella stessa immagine. Le capacità dichiarate
  sono disponibili nel percorso di produzione.
- Scheduler preemptive su tutte le CPU. Task, processi, IRQ e handle
  senza limiti fissi minuscoli.
- Più processi isolati caricati da file via VFS, con terminazione e recupero
  delle risorse verificati. Il sistema continua a funzionare quando un
  processo sbaglia o resta in esecuzione.
- NX, WP, SMEP, SMAP, UMIP, guard page su ogni stack kernel, canary e
  validazione degli input esterni attivi e provati.
- Build con un comando, CI verde, dieci boot consecutivi e almeno trenta
  minuti di stress senza errori né perdite di memoria.
- [Stato](status.md) di una pagina con i limiti noti concordati.
