# Validazione di processi, syscall e IPC

Versione completa precedente in `docs/design/k5-validation.md`, `docs/design/k6-design.md` e `docs/design/ipc.md`, al commit `efecb8b`.

I checkpoint seguenti usano QEMU/OVMF. Le campagne con un profilo
diagnostico provano i meccanismi di quel profilo. Il percorso
ordinario corrente è descritto nello [stato](../status.md).

| Checkpoint | Immagine e risultati |
| --- | --- |
| K5 | `k5-privilege`, `k5-ap`, `k5-syscall` e `k5-copy` 10/10 ciascuno. Negativi 3/3 per caso. Verificati ingresso ring 3, IRQ e ripristino del contesto, syscall, copia utente con recupero e transizione su un AP alla volta. Allora la build ordinaria teneva SCE spento. |
| K6-A | Parser ELF in host con un ELF Rust reale, confini, troncamenti, mutazioni di header e tabella e 200.000 mutazioni a seme fisso. Due livelli di ottimizzazione. Nessuna prova QEMU in questo blocco. |
| K6-B | `k6-protection` 10/10 a quattro vCPU con lettura di CR4 su tutte. Tre configurazioni senza SMEP e/o SMAP 3/3 ciascuna. Suite host 58/58. Un AP con capacità diverse dal BSP non era riproducibile con quella configurazione QEMU. |
| K6-C | `k6-process` 10/10 con ELF valido, ELF rifiutato, fault utente, copie fallite, IRQ da CPL3, NMI nelle finestre, isolamento tra esecuzioni e raccolta. Sei negativi 3/3 ciascuno, compresi root o slot incoerenti e fault kernel. |
| K6-D | Campagna storica 119/119, ottenuta in più corse e revisioni delle immagini. `k6-process`, `k6-ordinary` e `k6-protection` 10/10 ciascuna dopo le correzioni di review. Provata la scadenza con una syscall in corso, senza IRQ di processo serviti. |
| IPC v0 | `k6-process` e `k6-ordinary` 10/10 ciascuna. Il diagnostico verifica FIFO, coda piena e vuota, handle del peer rifiutato, buffer piccolo, recupero di fault in lettura e scrittura, chiusura dopo exit o fault e budget fisico ripristinato. Host IPC 12/12 per livello di ottimizzazione, runner 13/13. |
| Obiettivo 2 (processi multipli, `ordinary`) | Immagine unica `ordinary` (kernel SHA-256 `5A921AFFAF0F70B11CE8E6E5C852017AA23D4C2A0D87C554C5D875A9B7A73BB2`, loader SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`): `--ordinary-shell --smp 4` **10/10 PASS** (`FreePages: 15990` stabili, `Overruns: 0`), `--ordinary-shell --smp 8` **3/3 PASS** (`FreePages: 15778` stabili, `Overruns: 0`), `--ordinary-no-ps2 --smp 4` **1/1 PASS**. Su ogni boot shell: `init.elf` tramite `x86_process_manager`, `bridge` migrato senza `x86_preempt_pause()`, `proc` con **6 processi Ring 3 isolati contemporanei** (2 coppie server/client utente↔utente su 2 canali IPC + 1 fault CPL3 `#UD` verificato dal trace QEMU + `bridge` residente) con raccolta 5/5 e zero pagine perse, e 3× `bridge status` verdi. Suite host Meson **18/18 PASS** (`process_table_host` con ASan + `process_policy_rust` 13/13), `qemu_runner_unit` **21/21 PASS**. |
| Ring 3 su AP + 256 lease (`ordinary`) | Immagine unica `ordinary` (kernel SHA-256 `F5192230A68A4EC5A3043C236F9A732F28BAC34A6B17C47963459D33B59908FB`): `--ordinary-shell --smp 4` **10/10 PASS** (`FreePages: 15975` stabili, `Overruns: 0`), `--ordinary-shell --smp 8` **3/3 PASS** (`FreePages: 15747` stabili, `Overruns: 0`), `--ordinary-no-ps2 --smp 4` **1/1 PASS**. Ogni AP ha stack syscall/RSP0 dedicato da 4 pagine con guard page (33 pagine mappate/AP), `SYSCALL` MSR configurati in `x86_process_ap_activate()`, e PML4 utente con slot heap/AP Ring 0. In `proc`, AP1 esegue `bridge.elf` in Ring 3 (`SHELL_PROC_AP_CPU: 0x1`) con IPC cross-CPU verso `worker.elf` su BSP. `physical_allocator_host` (256 lease, ASan) PASS, AP geometry Rust test **2/2 PASS**, `qemu_runner_unit` **21/21 PASS**. |

Evidenze storiche locali sotto `kernel/build/` nelle esecuzioni K5
`qemu-run-0e7bd9138e074ee7821e8f62d4d050f9` e
`qemu-run-d262834e23094509a8a67d1077e54fd6`, K6-C
`qemu-run-9d1bfe167ae040909840cb5e82b38387` e
`qemu-run-a35837f296d74616a9efdd5161499307`, chiusura e review K6-D
`qemu-run-a3d8f15de78048afb74ed40c0ad5a330` e
`qemu-run-0d2f725d065341538ca4f89391a395df`, IPC
`qemu-run-a1c0fc7566e54d6db8b7466d84c28d5d`, Obiettivo 2
`proc-task25-final-4`, `proc-task25-final-8`, `proc-task25-nops2-4`,
Ring 3 su AP `run-ap-r3-smp4-10`, `run-ap-r3-smp8`, `run-ap-r3-nops2`.
I conteggi delle singole campagne precedenti restano nelle versioni
dei report a `efecb8b`.

Limiti noti rimasti: l'heap è limitato a 16 MiB (`KERNEL_HEAP_MAX_PAGES = 4096`);
`kernel_text` su COM1 non ha lock SMP (i processi utente su AP non usano
`SYS_WRITE`); gli ELF sono ancora incorporati (avvio da disco in Obiettivo 3–4);
hardware fisico non verificato.
