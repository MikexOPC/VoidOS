# Processo iniziale e policy utente

Il modulo comune collega al boot un ELF incorporato e fornisce al
backend x86 i validatori ELF, le policy per frame, user copy e
budget e il canale IPC. Il backend possiede ingresso e ritorno
dal ring 3 e le tabelle del processo.

In `ordinary`, `kernel_process_boot` apre il contesto, esegue
`init` se SMEP e SMAP sono disponibili e controlla uscita e
raccolta. L'IPC fra due processi è provato in `k6-process`.
La shell PS/2 di `ordinary` usa un servizio residente per il bridge.

Rimangono un solo contesto attivo sul BSP, syscall a interrupt
disabilitati e immagini ELF incorporate. [Contratto](../../docs/design/processi.md)
e [prove](../../docs/validation/processi.md).
