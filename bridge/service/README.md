# Servizio del bridge

Il servizio Rust riceve i byte sul proprio endpoint e restituisce
l'esito definito dall'API. Il collaudo esegue cinque scambi e termina
dopo che il servizio osserva la chiusura del canale. Il kernel gestisce
il canale e le risorse dei processi.

## Servizio persistente

Un solo processo del bridge resta residente sul
BSP e si sospende tra le richieste. La shell del kernel usa un endpoint
privato con una richiesta in volo. L'API binaria di `status` resta
quella già collaudata.

### Prerequisito x86

Oggi il runtime dei processi installa un'IDT separata e prende in uso il
timer LAPIC del BSP. I vettori esterni del diagnostico, salvo timer e
spurious, portano al percorso di arresto. Il percorso ordinario degli IRQ
presuppone invece un contesto kernel. Per mantenere operativi i driver,
il primo blocco deve gestire un IRQ dispositivo arrivato a CPL3,
passare al contesto kernel e usare il dispatcher ordinario con
conferma e notifica eseguite una volta. Il ritorno al processo passa
dalla validazione del frame utente. Timer, eccezioni e NMI conservano i
loro percorsi e le relative garanzie.

xv6 per RISC-V usa ingressi distinti per le trap da utente e da kernel
e un dispatcher dispositivi comune. Il riferimento è la separazione
tra ingresso e consegna dell'IRQ. [xv6-riscv, ramo riscv, trap.c](https://raw.githubusercontent.com/mit-pdos/xv6-riscv/riscv/kernel/trap.c)

### Richiesta e risposta

1. La shell pubblica i due byte della richiesta nel canale. Il
   dispatcher rende eseguibile il bridge e la shell attende l'esito
   usando un evento con generazione.
2. Il bridge riceve, valida e risponde in ring 3. La ricezione su
   canale vuoto lo sospende. Il dispatcher serializza pubblicazione,
   sospensione e risveglio per evitare notifiche perse.
3. Il kernel abbina l'esito alla richiesta attiva e sveglia la shell.
   Il processo conserva il proprio spazio di indirizzi tra due
   richieste. Il kernel possiede endpoint, pagine e stato della richiesta.

Il servizio ha un budget per richiesta. Se termina, va in fault o
esaurisce il budget, il kernel chiude il canale, completa l'attesa
della shell con un errore e raccoglie le pagine dopo la quiescenza.
Una risposta tardiva non può completare una richiesta successiva.
L'esito di `status` comunica la disponibilità del servizio.

Il modello richiesta-risposta con un ricevente sospeso è documentato in
seL4. VoidOS conserva il proprio formato e il proprio trasporto.
[Manuale seL4 14.0.0, §4.2 e §4.2.4](https://sel4.systems/Info/Docs/seL4-manual-14.0.0.pdf)

Nel diagnostico K6, `wait_readable` sospende il processo quando la
coda è vuota. Il dispatcher BSP riprende il frame salvato quando
arriva un messaggio o quando il peer si chiude. La lettura continua
tramite `recv`. Il test esegue cinque richieste sulla stessa istanza
del bridge e controlla anche il risveglio del client se il peer
termina per fault o per scadenza del budget.

### Sequenza e collaudo

1. Diagnostico IRQ a CPL3. Iniettare input durante una finestra utente
   verificata, osservare consegna ed EOI e verificare la ripresa utente.
   Coprire anche timer, NMI, fault e ripristino dello stato del BSP.
   Il timer del processo usa `0x44`. Il diagnostico scrive due byte nel
   buffer del controller i8042 emulato. Il controller genera IRQ1 sul
   vettore `0x41`. Il primo byte arriva a CPL3, il secondo a CPL0
   durante il drenaggio del timer. La prova verifica i byte, la ripresa
   del processo, due consegne e due EOI. Alla fine chiude il controller
   e ripristina la rotta IOAPIC.
   [QEMU i8042, comando D2](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c#L425)
   La prova è passata in 13 boot QEMU, di cui tre con quattro vCPU.
   Anche i tre casi di IRQ inatteso sono stati respinti come previsto.
2. Bridge residente. Verificare più richieste sullo stesso processo,
   nessun polling in attesa e risveglio anche quando pubblicazione e
   attesa si sovrappongono. Provare timeout, fault, chiusura e ripristino
   del budget di pagine.
3. Shell ordinaria. Eseguire `bridge status` più volte con input attivo,
   poi verificare che la shell resti utilizzabile quando il bridge
   termina. Dieci boot consecutivi per ciascun blocco che modifica
   transizioni di privilegio o IRQ.

L'alternativa è integrare subito più processi utente nel dispatcher,
con code di attesa e IPC bloccante generico. Anticipa il supporto ai
prossimi servizi, ma amplia gestione di lifetime, contesa e regressioni.
La proposta circoscritta permette di provare prima il confine IRQ e di
riusarlo quando verranno aggiunti altri processi.
