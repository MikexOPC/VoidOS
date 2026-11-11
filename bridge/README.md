# Bridge

Il bridge gestisce la prima operazione, `status`, nel diagnostico
`k6-process`. L'[API](api/) definisce il formato dei messaggi. Il
[servizio](service/) valida la richiesta e produce la risposta in Rust.

Il kernel identifica il mittente tramite il contesto del processo e
applica i controlli sulle operazioni privilegiate. Quando il bridge
introdurrà decisioni di autorizzazione, il kernel dovrà poterle
associare alla richiesta specifica, all'identità del richiedente e
alla risorsa. La prima operazione riguarda soltanto la disponibilità
del servizio.

## Primo collaudo

Due programmi ring 3 si alternano sul BSP nel diagnostico IPC. Il client
verifica le risposte a `status`, a due lunghezze errate, a una versione
sconosciuta e a un'operazione sconosciuta. Dopo l'uscita del client, il
servizio verifica la chiusura del canale. Il kernel raccoglie i processi
e confronta il budget di pagine con quello iniziale.
Il trasporto è descritto nel [contratto IPC v0](../docs/design/ipc.md).

Validazione iniziale: quattro test host Rust, dieci boot con una vCPU e
tre boot con quattro vCPU. In entrambe le campagne il bridge gira sul BSP.

Per ripetere il collaudo da PowerShell 7, dalla radice del progetto:

```powershell
./scripts/test/qemu.ps1 -Campaign 'k6-process,k6-process-smp' -Fresh
```

Il [progetto del servizio persistente](service/README.md#servizio-persistente)
descrive come collegare `bridge status` alla shell ordinaria.
