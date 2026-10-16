# Puntatore 18x18 nitido — 16 ottobre 2026

Scelte utente (16 ottobre): puntatore piu piccolo, 18x18 pixel invece di 24x24, nitido,
senza antialias, con l'aspetto dell'immagine di riferimento fornita dall'utente:
freccia nera con contorno bianco ([reference-cursor.png](../scripts/pointer/reference-cursor.png)).
Sostituisce la freccia arrotondata antialias di [pointer-design.md](pointer-design.md); i checkpoint
storici restano con le misure del loro tempo.

## Disegno

L'immagine di riferimento e stata ridotta a 18x18 con media d'area e poi portata a
pixel netti (contorno bianco di 1 pixel attorno a un riempimento nero, bordo sinistro
leggermente inclinato, tacca e coda in basso). Il risultato e la bitmap `ART` in
`kernel/console/video/scripts/pointer/shape.py` (`X` nero, `o` bianco, `.` trasparente):
ogni campione e 0 (trasparente), 16 (nero pieno) o 16<<5 (bianco pieno), nessun valore
intermedio. Lo script genera `kernel/console/video/pointer/shape.c` e la copia di
riferimento Python in `scripts/pointer/firmware.py`. Hotspot: la punta, in (2,0).

## Geometria derivata

| Grandezza | Prima (24x24) | Ora (18x18) |
|---|---|---|
| Pixel del bitmap | 576 | 324 |
| Backup sotto il cursore | 576 pixel (2304 byte) | 324 pixel (1296 byte) |
| Limite destro/basso del bitmap | larghezza-24, altezza-24 | larghezza-18, altezza-18 |

I limiti delle transazioni del cursore dipendono anche dalla cella del testo e sono
descritti in [font-cell.md](font-cell.md).

## Prove

Suite host `console`, `framebuffer` e `x86-framebuffer` (C/ASan, Rust, test Python del
runner, cppcheck) e campagne QEMU: vedere [font-cell.md](font-cell.md), che copre la
stessa immagine (font 10x20 e puntatore 18x18 sono stati validati insieme).
Il primo checkpoint con la freccia disegnata a mano (prima dell'immagine di riferimento):
10 boot con mouse, pointer, testo e shell, 60 scene pixel per pixel con 324 pixel fisici
e 10 boot ordinari, immagine SHA256
`79AE150A732DD5AA51889046D5E37BA0D408361C0010574F0E5F528193F9FA30`.

## Limiti

La resa visiva e quella del bitmap in QEMU/SDL; nessun giudizio estetico automatico.
La fluidita non cambia con la dimensione ([flush-cost.md](flush-cost.md)).
