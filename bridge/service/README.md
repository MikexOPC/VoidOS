# Servizio del bridge

Il servizio Rust riceve i byte sul proprio endpoint e restituisce
l'esito definito dall'API. Il collaudo esegue cinque scambi e termina
dopo che il servizio osserva la chiusura del canale. Il kernel gestisce
il canale e le risorse dei processi.

Il servizio permanente dovrà poter attendere richieste mentre le
altre task proseguono. Il comando `bridge status` dipende da questa
integrazione con la shell del kernel.
