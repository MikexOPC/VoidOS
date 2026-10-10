# Console video: griglia posseduta, 10 ottobre 2026

Snapshot del primo nucleo. Il gateway video e ora implementato nel
[blocco successivo](rendering.md), con limiti di latenza espliciti.

Scelta utente: griglia in RAM e aggiornamenti per cella. Modulo
kernel/console/video/, interfacce pubbliche del framebuffer come dipendenza.
Non e una task renderer nuova. Questo blocco realizza lo stato della console;
il collegamento agli output della shell e un blocco successivo, dopo i gate.

## Ricerca e separazione

[Linux v6.12 fbcon](https://www.kernel.org/doc/html/v6.12/fb/fbcon.html),
sezione Attaching, Detaching and Unloading: la console e un livello distinto
dal driver framebuffer e ne rispetta il lifetime. Qui non si copia codice
Linux: BSP, scheduler cooperativo, font fisso, niente VT, processi o DRM.
Il [font esistente](../../../../arch/x86/kernel/drivers/video/framebuffer/documentation/font.md)
offre celle8x16, UTF-8 italiano e fallback visibile.

## Contratto del nucleo

C possiede griglia e dirty bitmap; Rust safe decide transizioni con soli
interi, senza puntatori FFI, heap, hardware o stato globale. Oggetto originale
identificato da Self, RAM kernel fidata, un owner/task, nessun accesso IRQ
o concorrente. Self non e una difesa contro scritture arbitrarie in ring0.
Massimo240 colonne x67 righe: copre il limite video1920x1080 con celle8x16;
griglia fissa64320 byte, dirty2010 byte piu metadati/allineamento. Dimensioni
inferiori usano la stessa riserva; costo esplicito, nessuna allocazione.

Input per scalar Unicode, non stream UTF-8: il chiamante deve completare
il decoding prima della chiamata. C0/C1 respinti salvo LF, CR, Backspace.
Un scalar occupa una cella, anche un glifo mancante; grapheme/shaping fuori.
Wrap differito: X puo essere Columns; il prossimo carattere va alla riga
seguente. LF va alla colonna0 della riga seguente, CR alla colonna0, BS
cancella una cella, attraversando la riga se necessario; in origine e noop.
L'editor di riga mantiene il proprio confine: non deve inviare BS quando
la riga input e vuota. Tab/ANSI non interpretati.

Scorrimento in RAM: copia ordinata O(celle), ultima riga spazi, tutte le
celle dirty. Nessuna lettura/copia dal framebuffer UC. Peek non consuma;
ack azzera dirty solo per indice/scalar ancora corrispondenti. Su store
video fallito non fare ack: contenuto da ridisegnare conservato. Non e un
token generazionale o una sincronizzazione fra task/core; non consentire
mutazioni fra peek/store/ack. Output invalidi lasciano lo stato invariato.

Schema NON eseguibile del collegamento successivo:

```text
scalar completo -> owner griglia -> transizione validata -> celle dirty
peek -> gateway BSP: salva IF, CLI -> disegna una cella128 pixel
 -> ripristina IF originale su successo e errore -> ack solo su successo
 -> budget di celle esaurito: torna/yield, conserva il lavoro residuo
```

Il limite128 store non dimostra latenza IRQ: l'attuale renderer verifica
anche tutto l'inventario. Misurare la finestra completa e il progresso
PS/2/watchdog prima del collegamento operativo. Nessuna riduzione delle
verifiche per ottenere un tempo favorevole. Nessun blocco video a IRQ
disabilitati per tutto lo schermo; nessuna promessa di latenza oggi.
Una shell separata e input echo richiedono un owner console condiviso
attraverso un contratto esplicito, non due mutatori diretti della griglia.

## Gate

Rust debug/release/Clippy: geometria, stati, scalar invalidi, wrap/scroll.
C MSVC W4/WX ASan: canary, ownership originale, invalidita senza scritture,
ack stantio, dirty bounds, scroll1riga, riuso/stress, massimo240x67.
Probe griglia nella sola immagine diagnostica; dieci boot consecutivi
finali, regressione ordinaria, nessun fault. Prove effettive in validation.md.
Nessun nuovo accesso hardware; Valgrind Windows/coverage N/D. ASan host
non certifica leak fisici; zero nuove allocazioni in questo modulo.
