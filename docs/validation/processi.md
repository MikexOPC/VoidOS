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

Evidenze storiche locali sotto `kernel/build/` nelle esecuzioni K5
`qemu-run-0e7bd9138e074ee7821e8f62d4d050f9` e
`qemu-run-d262834e23094509a8a67d1077e54fd6`, K6-C
`qemu-run-9d1bfe167ae040909840cb5e82b38387` e
`qemu-run-a35837f296d74616a9efdd5161499307`, chiusura e review K6-D
`qemu-run-a3d8f15de78048afb74ed40c0ad5a330` e
`qemu-run-0d2f725d065341538ca4f89391a395df`, IPC
`qemu-run-a1c0fc7566e54d6db8b7466d84c28d5d`.
I conteggi delle singole campagne precedenti restano nelle versioni
dei report a `efecb8b`.

L'IPC è limitata al diagnostico e alterna i contesti sul BSP.
Restano aperti processi concorrenti generali, syscall interrompibili,
avvio da file e prove su PC fisico.
