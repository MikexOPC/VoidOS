# Design di processi, syscall e IPC

Versione completa precedente in `docs/design/k5-design.md`, `docs/design/k6-design.md`, `docs/design/ipc.md` e `docs/design/project-direction.md` (mediazione delle richieste), al commit `efecb8b`.

Il percorso ordinario K6 esegue sul BSP un ELF Rust incorporato e lo
raccoglie. Il diagnostico `k6-process` estende la prova a due contesti
alternati. La gestione generale di più processi resta da progettare.

## Caricamento e proprietà delle tabelle

Il parser Rust valida ELF64 x86-64 `ET_EXEC` prima di allocare. Accetta
segmenti caricabili allineati, disgiunti, entro la memoria utente, con
`filesz <= memsz` e entry eseguibile. Respinge interprete, linking
dinamico e segmenti W+X. Il budget del parser è separato dal limite
effettivo della finestra di mapping. L'ordinaria usa
`userspace/init/init.rs` collegato come blob ELF.

Il processo possiede la PML4 e le tabelle basse. Prende in prestito
dalla root kernel gli slot 488 (stack, finestra e LAPIC) e 511
(immagine e dati necessari alla transizione). La root utente esclude
la vista ricorsiva 510, le finestre di boot 479–480 e le regioni
481–487. Gli slot presi in prestito sono verificati alla creazione e
al rilascio. Dopo il ritorno alla root kernel il dispatcher può
azzerare le pagine e restituire le lease. Una revoca incerta trattiene
le risorse. Lo slot 511 condivide l'immagine kernel. KPTI è aperto.

## Ingresso, ritorno ed errori

GDT utente e protezioni dipendono dalle capacità CPU. L'assenza di
SMEP o SMAP disabilita ring 3. Assembly salva il contesto e cambia
root prima del codice C. `SYSCALL` usa lo stack kernel protetto
da guardie e ritorna con `IRETQ` dopo la validazione del frame.
Lo stato di transizione distingue i passaggi di GS e CR3 anche per
NMI. La copia utente verifica intervallo e accesso alle pagine,
riattiva la root del processo con AC nella finestra di copia e
recupera solo i fault ammessi in quella finestra.

Il budget è di due secondi sul clock ACPI PM, con limite di riserva
di 200 IRQ. Il kernel controlla entrambi anche ai confini delle syscall. Le syscall restano a IF=0. Fault utente ed esaurimento
del budget terminano il processo, mentre fault kernel e IRQ o NMI
inattesi nei percorsi previsti producono una diagnostica fatale.

## Syscall e codici di errore

Il numero entra in RAX, gli argomenti in RDI, RSI e RDX nell'ordine
indicato. Il risultato torna in RAX quando la chiamata riprende.
I codici negativi sono rappresentati in complemento a due su 64 bit.

| Numero | Chiamata | Esito |
| --- | --- | --- |
| 0 | `exit(code)` | Termina il processo con il codice passato in RDI. |
| 1 | `write(ptr, len)` | Scrive in console e restituisce i byte scritti. Limite di 256 byte per chiamata e quota cumulativa di 4096 byte per processo. |
| 2 | `yield()` | Restituisce il controllo al dispatcher e torna con 0 alla ripresa. Richiede un contesto riprendibile. |
| 3 | `send(handle, ptr, len)` | Con un endpoint IPC, accoda il messaggio e restituisce la lunghezza. |
| 4 | `recv(handle, ptr, capacity)` | Con un endpoint IPC, riceve un messaggio e restituisce la lunghezza. |
| 5 | `wait_readable(handle)` | Con un endpoint IPC e un contesto riprendibile, restituisce 0 se un messaggio è pronto. Su FIFO vuota parcheggia il contesto e alla ripresa restituisce 0 oppure l'errore di chiusura. Handle invalido o peer già chiuso restituiscono subito errore. La chiamata lascia il messaggio in coda. |

| Errore | Significato |
| --- | --- |
| -1 | Numero sconosciuto o chiamata indisponibile nel contesto corrente. |
| -2 | Intervallo utente invalido o fault durante la copia. |
| -3 | Lunghezza fuori limite, per `write` o per il messaggio IPC. |
| -4 | Quota cumulativa di `write` superata. |
| -5 | Handle IPC invalido, scaduto o appartenente al peer. |
| -6 | FIFO piena per `send`, vuota per `recv`. `wait_readable` usa questo esito internamente per sospendere il contesto. |
| -7 | Peer chiuso. |
| -8 | Buffer di ricezione troppo piccolo. |

L'ELF di avvio di `ordinary` dispone di `exit` e `write`. Il
diagnostico `k6-process` usa i contesti riprendibili e l'IPC. Il
profilo separato della shell PS/2 usa `wait_readable` per il bridge
residente. [Dispatcher](../../arch/x86/kernel/process/process.c),
[codici IPC](../../kernel/process/rust/ipc/channel.rs),
[policy di copia e quota](../../kernel/process/rust/policy/rules.rs).

## IPC v0

Nel diagnostico due processi con root distinte condividono un canale
bidirezionale sul BSP. Il dispatcher alterna gli ELF via `yield`
conservando frame, x87/SSE e budget. Torna alla root kernel prima di
cambiare contesto. Il canale ha due FIFO da quattro messaggi con
payload da 1 a 128 byte. L'handle iniziale arriva al payload in RDI.
L'handle è legato al processo e alla generazione del canale. Lo slot
si ritira se la generazione si esaurisce.

L'invio pubblica solo dopo la copia completa. La ricezione estrae solo
dopo la copia completa e può lasciare un prefisso nel buffer utente in
caso di fault. Exit, fault o scadenza di un peer chiudono il canale,
scartano i messaggi e fanno osservare la chiusura all'altro. Le code
sono azzerate prima del riuso. L'IPC generale, con attese e trasferimento
dei diritti, richiede un design successivo. [Validazione](../validation/processi.md).

## Mediazione delle richieste

La direzione discussa per il bridge prevede un servizio di policy che
limita concessioni per identità, risorsa, durata e operazione. Il kernel
deve applicare l'autorità anche alle chiamate dirette. Una richiesta in
attesa di conferma deve poter scadere o essere annullata senza detenere
lock. Il percorso fidato di input e visualizzazione, le revoche e il
comportamento quando il servizio si arresta restano da progettare.
Il profilo PS/2 prova solo il servizio di stato residente.
