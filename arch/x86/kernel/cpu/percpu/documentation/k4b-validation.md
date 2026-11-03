# K4-B — Validazione

## Comando IPI di flush

La mailbox Rust pubblica operazione e generazione con un solo CAS.
Il bit 63 seleziona il flush, i bit 0–62 contengono la generazione.
Il chiamante vede la generazione separata, con limite 2^63−1.
L'AP verifica di aver acquisito quella richiesta prima di leggere
l'operazione. Il layout della mailbox resta di 64 byte.

Il backend ricarica lo stesso CR3 con IRQ mascherati, root attesa e
PGE/PCIDE disattivati. L'ACK segue il reload e l'EOI. Il diagnostico
esegue 99 PING, poi un flush per AP. Il BSP ricarica CR3 nel giro finale.
Il disassemblato conferma il controllo CR4 con maschera 0x20080 e
la scrittura di CR3 prima del completamento.

- Build MSVC/NASM/Rust ordinaria e diagnostica riuscita.
- Suite ipi-service passata, inclusi 7 test Rust, ASan sui test C e cppcheck.
  Evidenza: kernel/build/test-run-a84b1a5dc5844cf69ce02cb4db6627ae.
- Layout passato. Evidenza: kernel/build/test-run-34ba894ba9a54013a1805f0e7f360c4f.
- Audit del codice di emergenza passato sull'immagine diagnostica.
- 10/10 boot diagnostici QEMU a 4 CPU. Per boot, 297 PING e 3 flush
  AP con ACK dopo EOI e verifica del contesto dopo IRETQ.
  Evidenza: kernel/build/ipi-service-e6e32cfa0e9f4048a1f2a3c33c490422.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-c36b70ed949a4be7b30abacd1da321e7.

Il comando non modifica PTE né rilascia frame. Restano la prova di
rimappatura con due frame, l'iniezione degli errori durante tale prova
e il collegamento al paging ordinario. Coverage non misurata.
