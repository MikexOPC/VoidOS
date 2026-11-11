# API del bridge

`status` chiede al bridge se è operativo. La richiesta ha due byte,
versione e operazione, entrambi con valore 1. La risposta ha due byte,
versione 1 e codice di esito.

| Codice | Esito |
|---|---|
| 0 | Bridge operativo |
| 1 | Lunghezza della richiesta diversa da 2 |
| 2 | Versione sconosciuta |
| 3 | Operazione sconosciuta per la versione corrente |

Il client diagnostico attende ogni risposta prima di inviare la
richiesta successiva. La chiusura del canale è un esito del trasporto.
