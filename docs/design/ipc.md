# Prima IPC tra processi utente

Stato: IPC v0 verificata nel diagnostico `k6-process`. Il client invia
messaggi, il server risponde e il kernel raccoglie entrambi. In una seconda
prova il server va in fault e il client riceve un errore di chiusura.

## Scelta della prima versione

**Proposta.** Due processi con root distinte sul BSP, un canale
bidirezionale e una FIFO per direzione. Ogni FIFO contiene al massimo
quattro messaggi da 128 byte, pari a 1 KiB complessivo per i payload. Invio
e ricezione restituiscono subito un esito. Quando serve attendere, il
programma cede il turno con `yield` e riprova dopo la ripresa. Linux v6.12
offre un riferimento per i messaggi in coda e le operazioni non bloccanti
con i socket locali `SOCK_SEQPACKET`. [Sorgente Linux v6.12](https://github.com/torvalds/linux/blob/v6.12/net/unix/af_unix.c#L2244)

**Alternativa.** Invio e ricezione sincroni in stile rendezvous. Il
mittente si sospende fino a quando il destinatario riceve. Questo riduce
l'accodamento, ma introduce subito task sospese, risveglio, timeout e
cancellazione al termine del peer. [IPC di seL4](https://docs.sel4.systems/Tutorials/ipc.html#system-calls)

La prova parte in un profilo Meson dedicato. Il BSP gestisce il canale e
l'alternanza. La v0 usa copia dei messaggi e assegnazione degli endpoint
al caricamento. Memoria condivisa, trasferimento dei diritti e servizi
permanenti arrivano in blocchi dedicati.

## Contratto del canale

Il kernel crea entrambi gli endpoint quando i due processi sono pronti.
Un handle è valido soltanto per il processo a cui appartiene, per la
generazione corrente e per l'operazione autorizzata. L'identità del
mittente proviene dal contesto corrente nel kernel. All'esaurimento delle
generazioni il relativo slot viene ritirato.

| Operazione | Esito |
| --- | --- |
| `send(handle, ptr, len)` | Accoda un messaggio da 1 a 128 byte dopo la copia completa. Coda piena, handle errato o copia fallita restituiscono un errore. |
| `recv(handle, ptr, capacity)` | Copia il primo messaggio e restituisce la lunghezza. Coda vuota, spazio insufficiente o copia fallita restituiscono un errore. |
| `yield()` | Salva il contesto e restituisce il controllo al dispatcher BSP. Alla ripresa, restituisce successo. La prova K6 usa il numero di syscall 2. |

Nel diagnostico K6 la routine di ingresso trasferisce l'handle iniziale
da `R8`, terzo argomento MSVC x64, a `RDI` per il payload ELF.
[ABI x64 Microsoft](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)

`send` e `recv` sono le syscall 3 e 4. Ricevono handle, puntatore e
lunghezza o capacità in `RDI`, `RSI` e `RDX`. Il risultato in `RAX` è
la lunghezza del messaggio oppure uno degli errori seguenti.

| Codice | Errore |
| --- | --- |
| -2 | Intervallo utente invalido o fault durante la copia |
| -3 | Lunghezza del messaggio fuori dai limiti |
| -5 | Handle invalido, scaduto o appartenente al peer |
| -6 | Coda piena per `send`, vuota per `recv` |
| -7 | `PEER_CLOSED` |
| -8 | Buffer di ricezione troppo piccolo |

La ricezione rimuove il messaggio soltanto dopo una copia completa. Un
fault durante la copia verso l'utente può lasciare un prefisso nel
buffer chiamante. Il chiamante riceve l'errore e la FIFO conserva il
messaggio. Il kernel verifica intervalli, permessi e durata delle
mappature per entrambe le direzioni. K6 ha collaudato il recupero in
lettura nel percorso di processo. La scrittura richiede prove dedicate.

Exit, fault o esaurimento del budget di uno dei due processi chiudono
il canale e scartano i messaggi pendenti in entrambe le direzioni.
L'endpoint del peer superstite restituisce `PEER_CLOSED` fino alla sua
raccolta. Il kernel azzera le code prima del riuso.

## Processo e dispatcher

K6 esegue un processo fino al termine con un solo contesto attivo. La
prima estensione conserva due record, ciascuno con root, lease delle
pagine, frame utente, stato x87/SSE e budget residuo. Prima di cambiare
processo, il dispatcher riacquisisce la root kernel. Lo stack kernel
condiviso è riutilizzabile solo quando il percorso di syscall, IRQ o
fault è rientrato completamente al dispatcher.

1. Caricare i due ELF e trattenere le due lease in record separati.
   Verificare che la finestra esistente possa parcheggiarle una alla
   volta e che il rollback liberi la prima se il caricamento della
   seconda fallisce.
2. Selezionare un processo pronto, associare il suo contesto al BSP e
   attivare la sua root. Al primo ingresso creare il frame, alle
   riprese ripristinare frame e stato x87/SSE salvati.
3. Su `yield`, tornare al dispatcher con root e stack del kernel.
   Riaccodare il processo pronto e selezionare il peer. Conservare
   il budget residuo, addebitando il tempo trascorso nel turno.
4. Su uscita, chiudere il canale e raccogliere le pagine soltanto dopo
   aver verificato che il BSP è tornato alla root kernel. Una revoca
   incerta trattiene le risorse e produce una diagnostica.

Le code e gli stati vivono in Rust. Il backend x86 riusa le transizioni
K6 in Assembly e il codice di mapping esistente in C. Nessun IRQ modifica
le FIFO. Il limite di esecuzione K6 resta attivo e il dispatcher ha un
limite separato sul numero di cambi di turno per il diagnostico.

Il primo blocco usa il profilo Meson `k6-process`. Le due immagini
verificano valori diversi di R12, XMM0 e memoria privata allo stesso
indirizzo virtuale. Il probe tenta anche un secondo caricamento non
valido dopo aver parcheggiato la prima lease.

Verifica del primo blocco: `k6-process` e `k6-ordinary` 10/10 ciascuno,
parità Meson e 23 test host Rust passati. I due ELF hanno entry a
`0x400000` e il contatore privato a `0x402000`. Evidenze in
`kernel/build/qemu-run-a1156aeff2b2428dbe242d3181c582ea` e
`kernel/build/qemu-run-cba69297018f4cc5afc01befa0fcbf06/k6-ordinary.log`.

## Sequenza di lavoro e accettazione

1. **Contesti.** Far alternare due ELF con `yield`. Verificare la
   conservazione del contesto e la separazione delle root anche quando
   gli ELF usano lo stesso indirizzo virtuale.
2. **Canale.** Verificare in host ordine FIFO, coda piena, messaggio troppo
   grande, handle invalido o scaduto, chiusura e riuso.
3. **Integrazione.** Dieci boot QEMU consecutivi con richiesta e
   risposta, copia verso una pagina utente non scrivibile, fault del
   peer e rollback del secondo caricamento. Verificare che una copia
   fallita lasci il messaggio in coda, che il superstite osservi
   `PEER_CLOSED` e che il budget fisico finale torni al valore iniziale.
   Rieseguire `k6-process` e `k6-ordinary`.

## Validazione IPC v0

- QEMU/OVMF: `k6-process` 10/10 e `k6-ordinary` 10/10. Il probe verifica
  coda piena e vuota, FIFO, handle del peer rifiutato, buffer piccolo,
  fault di lettura e di scrittura recuperati, chiusura dopo exit e fault
  e ripristino del budget fisico.
- Host: 12/12 test Rust a ciascun livello di ottimizzazione e 13/13 test
  del runner. Parità Meson e confronto dei piani PowerShell/Rust passati.
- Evidenze QEMU in `kernel/build/qemu-run-a1c0fc7566e54d6db8b7466d84c28d5d`.
