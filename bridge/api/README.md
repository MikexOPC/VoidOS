# API del bridge

`status` chiede al servizio se è operativo. La risposta riguarda il
bridge stesso. L'API distinguerà una richiesta malformata o non
supportata da una mancata risposta dovuta alla chiusura del canale.

Per la prima prova è sufficiente una richiesta in volo per canale.
Ogni messaggio deve rientrare nei 128 byte del trasporto IPC v0.
Il formato binario e i codici saranno fissati con il primo client.
