# Bridge

Il bridge è un servizio di sistema previsto in ring 3. La prima
operazione, `status`, servirà a verificare il percorso di richiesta e
risposta. L'[API](api/) descrive cosa può chiedere un client. Il
[servizio](service/) descrive come gestisce la richiesta.

Il kernel identifica il mittente tramite il contesto del processo e
applica i controlli sulle operazioni privilegiate. Quando il bridge
introdurrà decisioni di autorizzazione, il kernel dovrà poterle
associare alla richiesta specifica, all'identità del richiedente e
alla risorsa. La prima operazione riguarda soltanto la disponibilità
del servizio.

## Primo collaudo

La proposta è eseguire due programmi ring 3 sul BSP nel diagnostico
IPC già disponibile. Il client invia `status`, il servizio risponde e
il kernel raccoglie i due processi. I test coprono anche richieste
malformate, chiusura del canale e recupero delle risorse.
Il trasporto è descritto nel [contratto IPC v0](../docs/design/ipc.md).

L'alternativa è collegare subito `bridge status` alla shell ordinaria.
Richiede un client nel kernel e un dispatcher in grado di riprendere
il servizio ring 3 mentre console e input continuano a ricevere IRQ.
Oggi il runtime K6 usa temporaneamente l'IDT e il timer del BSP.
Il primo percorso può quindi verificare protocollo e servizio prima
dell'integrazione con la shell.
