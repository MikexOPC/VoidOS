# Servizio del bridge

Il servizio Rust riceve la richiesta sul proprio endpoint, ne valida
il formato e restituisce lo stato. Per il primo collaudo conclude
dopo la risposta. Il kernel gestisce il canale e il cleanup anche in
caso di uscita o fault del processo.

Il servizio permanente dovrà poter attendere richieste mentre le
altre task proseguono. Il comando `bridge status` dipende da questa
integrazione con la shell del kernel.
