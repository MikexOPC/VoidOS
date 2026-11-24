# Stato di VoidOS — 23 novembre 2026

## Immagine di produzione

`ordinary`, compilata con Meson e Ninja, avvia il loader UEFI, il kernel,
init e la shell. Le verifiche attuali sono su QEMU/OVMF q35 TCG.

- BSP e AP hanno dispatcher permanenti, IRQ comuni e preemption. La sessione
  PS/2 possiede e rilascia task, stack, IRQ e binding. Il BSP resta attivo
  dopo `exit` e senza PS/2. Task e affinità usano tabelle dinamiche, con
  capacità x86 fino a 64 task e 222 vettori sottoscrivibili per CPU.
- Processi Ring 3 isolati, canali IPC e syscall protette. Il VFS carica ELF
  dal disco FAT32. `proc` verifica IPC BSP/AP, VFS, fault isolato e cleanup.
  L'immagine utente arriva a 6 MiB, con stack da 64 KiB e guard page.
- VirtIO-blk e AHCI forniscono storage a blocchi e cache. FAT legge directory
  e LFN; scrittura e unlink operano sui nomi 8.3 nella root del mount.
- NX, WP, SMEP, SMAP, UMIP, guard page, canary, entropia hardware e CSPRNG
  hanno verifiche nell'immagine ordinaria. KPTI esclude heap kernel, vista
  ricorsiva e slot device dalla vista utente.
- La shell offre `help`, `print`, `status`, `bridge status`, `proc`, `fl`,
  `rp`, `write`, `rm`, `sync`, `ap`, `gui`, `exit`.
- Il nuovo desktop contiene sfondo lavanda e dock. `gui` apre il compositor
  Slint; il Terminale è un processo reale avviato dal dock, con input IPC e
  superficie pubblicata in sola lettura. Chiudere il desktop ripristina la
  shell e verifica il recupero delle pagine. Il kernel raccoglie anche i
  discendenti del compositor alla sua raccolta.

## Lavoro corrente

**Desktop con app reali — parziale.** La prima task collega il Terminale.
Restano System Monitor con dati reali, prova con due app e stress conclusivo.
Le vecchie percentuali dimostrative di System Monitor saranno rimosse nella
seconda task. Il boot esegue i self-check e lascia la shell disponibile.
L'ingresso interattivo è `gui`.

## Limiti noti

- Hardware fisico, USB xHCI, x2APIC e rete devono ancora essere verificati o
  implementati. La GUI usa input PS/2 e rendering software; heap Slint 4 MiB.
- GUI e creazione dei suoi processi operano sul BSP. Gli AP mantengono i
  percorsi IPC supportati. La seriale `kernel_text` resta priva di lock SMP.
- L'uptime parte dalla prima richiesta display del kernel e accumula i wrap
  ACPI PM. Ogni turno utente ha un budget massimo compatibile con mezzo
  periodo del contatore PM a 24 bit.
- Heap kernel 32 MiB, 256 lease fisiche e 512 extent. Il primo init resta
  incorporato; i programmi successivi vengono caricati dal VFS.
- La vista utente conserva le mapping supervisor minime per le transizioni.
  Le mitigazioni speculative dipendono dalle capability esposte dalla CPU.
- Dubbio accettato il 15 novembre 2026, ancora aperto nella documentazione
  precedente. In una sessione su 21 con tre QEMU paralleli un AP non ha
  confermato lo shootdown TLB entro un secondo (`K4TLBACK`). La causa resta
  ignota; nove ripetizioni diagnostiche non lo hanno riprodotto.

## Prove e prossimo passo

La [validazione GUI](validation/gui.md) identifica binari e campagne del
lavoro corrente. Le prove precedenti sono nelle validazioni di
[consolidamento](validation/consolidamento.md), [processi](validation/processi.md),
[storage](validation/storage.md), [esecuzione da file](validation/exec.md) e
[sicurezza](validation/sicurezza.md). Il prossimo passo è chiudere il desktop
con le app reali prima dell'obiettivo hardware e periferiche.
