# Console video — design

I documenti completi precedenti sono in `kernel/console/video/documentation/` al commit `49f8cef`.

La console mantiene una griglia di celle da 9×18 pixel e una bitmap
delle celle sporche. Il font bitmap viene generato offline. I limiti
della griglia sono 240 colonne e 67 righe. Modello, rendering e backend
restano separati: il modello cambia celle, il renderer sceglie gruppi
delimitati, il driver verifica il target e applica il gruppo. Il danno
resta pendente se il backend non conferma il lavoro.

Il worker di console sul BSP è l'owner del renderer e dell'output
hardware. Il puntatore ha un lock dedicato. Il consumer rinvia gli
aggiornamenti del mouse mentre il gruppo grafico è in corso. I produttori passano richieste WRITE, EDIT e SUBMIT nella
coda protetta, con testo copiato fino a 256 byte. La coda distingue
OPEN, DRAINING, CLOSED e FAILED. Take e complete delimitano una
richiesta in volo. Un enqueue riuscito segnala il worker. In chiusura
prima si fermano i produttori e si completano le richieste, poi si
ritirano window e stack. Un close incerto conserva le risorse.
I job AP fidati comunicano attraverso la coda, mentre il driver
continua a girare sul BSP.

Caret e puntatore sono overlay. Un movimento sporca la posizione
vecchia e quella nuova. Il gruppo aggiorna sfondo, caratteri e
overlay in ordine e non impegna la posizione visualizzata prima
della conferma. Il puntatore è limitato alla superficie attiva.
L'audit completo degli owner e delle pagine è separato dal controllo
breve usato nel percorso di rendering. Le misure distinguono coda,
preparazione, disegno e presentazione.

KERNEL_DISPLAY offre Check, Poll, Busy, Redraw e Rect. Senza Ops si
disegna sul GOP. Con VirtIO il worker avanza la presentazione con
polling e gestisce il ritorno a GOP quando il target è ritirato.
Il percorso senza video può usare la seriale. Meson `ordinary` usa GOP se presente e include worker, input,
overlay e shell PS/2. La console VirtIO con VT-d usa un profilo separato.
Font bitmap e sessione a griglia non costituiscono una GUI utente.

[Validazione del modulo](validation.md).
