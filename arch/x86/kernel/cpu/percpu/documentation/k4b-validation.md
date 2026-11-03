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

Il comando IPI si occupa del flush. Il collaudo della rimappatura è
riportato sotto. Coverage non misurata.

## Rimappatura diagnostica

Il giro 98 legge il valore del vecchio frame su ogni AP. Il BSP
rimappa la stessa VA e il giro 99 invalida l'intero contesto. Il giro
100 legge il valore del nuovo frame su ogni AP. Il BSP rilascia il
vecchio frame dopo gli ACK e le letture.

Nei due profili di errore, AP1 esegue il flush e completa EOI al giro
99, poi omette l'ACK oppure tenta di confermare la generazione
successiva. Il runner verifica il motivo del panic e legge l'inventario
dell'allocatore a VM ferma. Entrambi i frame risultano ancora pinned
nei casi di errore. Nel caso positivo resta il frame sostitutivo,
mantenuto fino all'arresto del diagnostico.

| Profilo QEMU a 4 CPU | Esito | Evidenza in kernel/build |
| --- | --- | --- |
| Rimappatura e rilascio | 10/10 | ipi-service-996483a1576f4c30b8bf6ae730956347 |
| ACK mancante | 3/3 arresti attesi | ipi-service-0aea57d0a60e40bdb09800a1a3320bc1 |
| ACK di generazione errata | 3/3 arresti attesi | ipi-service-260d1ddc928748c38c9c16f580e696d8 |
| Boot ordinario | 10/10 | ap-runtime-fd3c0cd9433645ddb2206b0d61606dde |

Suite host ipi-service e layout passate con exit 0. Evidenze in
kernel/build/test-run-3af6ec2f0497450ea0ce7626a0333633 e
kernel/build/test-run-6d37d33b37e6424280cebb07bcaf0cf7.
Audit di emergenza passato per tutte e tre le immagini diagnostiche.
Resta da collegare il flush al paging ordinario e al rilascio dei
suoi frame e delle tabelle intermedie.

## Rilascio della page window

Il diagnostico rimappa e verifica i frame come sopra, poi chiama
x86_page_window_release sul frame sostitutivo. Il runner controlla
che ogni AP completi la richiesta 101 con EOI e ritorno da IRETQ,
e che l'inventario finale non contenga più nessuno dei due lease.

- 10/10 boot QEMU a 4 CPU con il rilascio della finestra.
  Evidenza: kernel/build/ipi-service-1aa85b633daf4d4eb6b28d46ac12e89c.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-08b9981f6101447f929d086ef0bc37de.
- Suite host ipi-service, kernel e layout superate. Log in
  kernel/build/test-run-9aa863b2c7fb4e85914b1ec00d5c47b6,
  kernel/build/test-run-52ac4958ff014c0f90a669f1ea5690f5 e
  kernel/build/test-run-5c616c8e82424dcbae68fe6284289db4.
- Audit del codice di emergenza passato sulle due immagini.

Gli arresti da ACK mancante o errato sopra sono prove della rimappatura
del blocco precedente. Non sono iniezioni nel rilascio finale.
