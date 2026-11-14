# Input PS/2 — design

I documenti completi precedenti sono in `kernel/drivers/input/ps2/documentation/` al commit `49f8cef`.

Il controller possiede il trasporto, valida negoziazione e stato
e passa i byte al decoder. L'ISR distingue AUX dallo status,
produce dati o perdita nella coda e lascia decodifica e testo al
consumer. Il decoder Rust mantiene la macchina scan set 2.
L'input tastiera converte gli eventi in modificatori e testo per
editor e shell. Su errore fuori banda, il consumer elimina la
sequenza incerta prima di riaprire decoder, modificatori e riga.
Scadenza, perdita ripetuta o ownership incerta seguono le
politiche del controller e della sessione.

Il percorso interattivo PS/2 fa parte di `ordinary` e gira sul BSP.
Il test controller può aprire e chiudere il dispositivo senza
mantenere una sessione utente. Gli script host e i firmware
diagnostici distinguono trasporto modellato ed emulato.
Hardware fisico e USB restano fuori dal contratto.

[Validazione del modulo](validation.md).
