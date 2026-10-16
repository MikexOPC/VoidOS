# Cursore di testo fisso

Scelta approvata: sottolineatura fissa, nessun timer. Il cursore è metadato
del renderer e non un carattere Unicode. Il servizio serializza gli aggiornamenti
RAM; il worker conserva gruppi di massimo otto celle e verifiche complete
dell'autorità framebuffer prima e dopo il disegno.

Rust valida dimensioni e coordinate, compreso X == Columns (wrap differito):
in questo caso la sottolineatura resta nell'ultima cella della riga. C marca
sporche la precedente e la nuova cella; la composizione non modifica il testo
per rappresentare il cursore. L'overlay usa le ultime due righe del glifo 8x16,
con colore foreground e senza scritture MMIO aggiuntive. I profili diagnostici
mantengono il cursore disabilitato. Alla chiusura resta l'ultima immagine,
senza accedere al framebuffer dopo la revoca del servizio.

Pseudocodice non eseguibile: valida nuova posizione; marca vecchia cella;
pubblica posizione; marca nuova cella. Per gruppo: cattura posizione e scalari;
componi bitmap con overlay; verifica posizione invariata; conferma celle.
Qualunque errore di disegno è terminale, senza fingere rollback dei pixel.
Il callback di gruppo legge il metadato Caret tramite il proprio contesto
posseduto; il batch ne verifica la stabilità prima dell'ack. Il vecchio
renderer singolo rifiuta una vista con cursore abilitato, per evitare un
successo apparente senza overlay. Le API framebuffer precedenti restano
wrapper senza cursore; l'indice relativo al gruppo viene validato prima
di qualunque scrittura.

Accettazione: bounds Rust; testo invariato; cancellazione/scroll/fine riga;
dirty vecchio/nuovo; rifiuto di mutazioni durante rendering; pixel dell'overlay
verificati indipendentemente in QEMU e dieci boot consecutivi dell'immagine finale.
Nessuna allocazione nuova, nessun lavoro negli handler IRQ.

Riferimento architetturale: [Linux v6.12 fbcon, Attaching/Detaching/Unloading](https://www.kernel.org/doc/html/v6.12/fb/fbcon.html#attaching-detaching-and-unloading):
console, renderer e framebuffer hanno ownership e lifetime distinti. Non viene
copiato codice Linux; non sono introdotti nuovi registri hardware.
