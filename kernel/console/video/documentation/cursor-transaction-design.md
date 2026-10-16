# Save/restore del cursore: proposta verificabile — 13 ottobre 2026

Stato: contratto approvato e implementato; vedere
[prove della transazione](cursor-transaction-validation.md).
Richiesta utente: save/restore sotto il cursore, double buffering,
dirty rectangles e accelerazione. Il save/restore sostituisce il precedente
ridisegno delle celle vecchie e nuove; gli altri punti non sono lo stesso modulo.

## Evidenza disponibile

Il renderer attuale aggiorna gruppi dirty di massimo8 celle, non1920x1080
a ogni evento. Il calcolo124416000 pixel/s descrive un framebuffer intero
a60Hz, non il carico corrente. La profilazione per-cella del10 ottobre
attribuiva94,3428% all'apertura/inventario e0,8978% a pixel/readback:
[checkpoint delle misure](profile-validation.md). Non e una misura del
renderer antialias attuale; va ripetuta prima di promettere un guadagno.
Le scomparse manuali documentate finora terminavano con perdita input e
panic: non dimostrano un difetto di ripristino dello sfondo.

## Contratto proposto

Un solo worker possiede snapshot, aggiornamenti framebuffer e backup.
La task mouse aggiorna solo la posizione richiesta; IRQ e task input non
leggono o scrivono framebuffer. Backup fisso576 pixel a32bit:2304 byte,
senza heap. Metadati legano il backup all'owner originale, framebuffer,
formato, coordinate e stato della sessione. Un backup copiato o relativo
a una sessione precedente viene rifiutato.

Sequenza non eseguibile per ogni transazione:

```text
valida ownership, lease, geometria e rettangoli autorizzati
verifica completamente il paging
se il cursore e presente: ripristina il suo backup e invalidalo
disegna il gruppo dirty di testo/caret, se presente
salva i pixel sotto la posizione richiesta attuale
componi la freccia antialias sullo sfondo appena salvato
verifica stato finale; solo allora conferma dirty e nuovo backup
restituisci il controllo e riabilita IRQ secondo il gateway esistente
```

Il ripristino precede ogni aggiornamento del testo, anche se la cella
non tocca il cursore: evita di mantenere un backup vecchio quando il
testo o lo scroll cambiano. Il salvataggio segue le scritture, quindi
descrive il contenuto framebuffer effettivo. Rettangoli sovrapposti
sono leciti; l'ordine ripristino/testo/salvataggio/cursore e obbligatorio.
Tra transazioni riuscite il cursore rimane visibile; nessuna promessa
di atomicita rispetto alla scansione video o di assenza di tearing.

La transazione autorizza al massimo tre rettangoli: vecchio cursore,
gruppo dirty e nuovo cursore. Gli accessi restano limitati a quei
rettangoli, anche se sono distanti; non autorizzare l'intera bounding box.
Budget massimo:576 scritture di ripristino,1024 scritture di testo,
576 letture di salvataggio e576 scritture del cursore =2752 operazioni pixel.
Ogni store include un readback aggiuntivo: il massimo teorico e4928 accessi
alla memoria video. Il limite2752 conta le operazioni API, non il traffico bus.
Il limite4096 resta sufficiente; conteggi, overflow, stride e formato
devono essere validati prima di qualsiasi accesso. Nessun memcpy generico
su MMIO e nessuna assunzione di contiguita tra righe.

Il gruppo attuale garantisce una scansione completa per gruppo di8 celle.
Questa proposta cambia l'unita del controllo in una transazione di massimo
2752 accessi, senza memorizzare autorizzazioni tra transazioni. Richiede
la decisione direttive-locali §7 prima di creare la nuova API dei pixels/gateway.
I profili diagnostici esistenti conservano le API e i controlli attuali.

## Failure e stop

Un errore prima della prima scrittura non cambia pixel o backup.
Dopo una scrittura fallita non e garantito il rollback dei pixel: errore
terminale, niente ack dirty o successo inventato. Risorse restano possedute
fino al cleanup verificato. Stop ripristina il backup e non ridisegna
la freccia; close richiede backup invalido, nessuna transazione e drain.
Perdita input resta distinta dagli errori video e non viene nascosta.

## Accettazione

C/ASan: original owner, stale backup, bounds, padding, formati RGB/BGR,
canary, access count, sovrapposizione, stessa posizione e bordi.
Integrazione: movimento sopra testo, caret, scroll, scrittura mentre il
mouse si muove, hide/stop, errori prima/dopo ogni fase, revoca negata
durante la transazione. Nessuna nuova allocazione dinamica; accounting
fisico separato da ASan. Dieci boot consecutivi sulla stessa immagine,
input continuo, confronto screenshot/pixel, zero perdite e cleanup.
Profilazione comparabile prima/dopo: tempo di verifica, pixel, durata
totale IF0, backlog input e latenza di presentazione. Nessun FPS inventato.

## Double buffering e GPU

GOP espone FrameBufferBase e geometria (§12.9); il contratto passato al
kernel non contiene page flip o sincronizzazione vblank:
[UEFI2.10 Console Support](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html).
Un back buffer RAM richiederebbe copia delle aree dirty al framebuffer
visibile; scambiare due puntatori RAM non modifica lo scanout hardware.
Un buffer1920x1080x4 occupa8294400 byte, circa7,91MiB; due circa15,82MiB.
Double buffering riduce esposizione dei disegni intermedi, non elimina
il costo della copia o garantisce uno swap atomico senza supporto hardware.
Un driver Intel richiede identificazione del device/revisione e un modulo
dedicato; le misure attuali non lo indicano come prerequisito per migliorare
la console e il cursore software.
