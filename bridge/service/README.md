# Servizio del bridge

Il servizio Rust riceve i byte sul proprio endpoint e restituisce
l'esito definito dall'API. Il collaudo esegue cinque scambi e termina
dopo che il servizio osserva la chiusura del canale. Il kernel gestisce
il canale e le risorse dei processi.

## Servizio persistente

Proposta da concordare. Un solo processo del bridge resta residente sul
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

### Sequenza e collaudo

1. Diagnostico IRQ a CPL3. Iniettare input durante una finestra utente
   verificata, osservare consegna ed EOI e verificare la ripresa utente.
   Coprire anche timer, NMI, fault e ripristino dello stato del BSP.
   Il timer del processo usa il vettore `0x44`. Il diagnostico accoda
   sul BSP un IPI LAPIC sul vettore `0x41` della tastiera con IF=0.
   Verifica il frame CPL3, una sola consegna nel registro ordinario,
   un solo EOI, la ripresa del programma e la rimozione della
   sottoscrizione. Il collaudo PS/2 deve ancora esercitare il controller
   e la consegna a CPL0 durante il drenaggio del timer.
   [Intel SDM 325462-089, vol. 3A, §§7.14 e 12.6–12.8, Fig. 12-8](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
   QEMU ha confermato la nuova assegnazione in 13 boot K6. Sono passati
   anche tre rifiuti attesi del vettore inatteso, dieci boot ordinari e
   dieci boot con la shell.
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
