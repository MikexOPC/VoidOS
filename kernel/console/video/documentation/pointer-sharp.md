# Puntatore 18x18 nitido — 16 ottobre 2026

Scelte utente (16 ottobre): puntatore piu piccolo, 18x18 pixel invece di 24x24,
freccia nitida bianca con contorno nero, senza antialias. Sostituisce la freccia
arrotondata antialias di [pointer-design.md](pointer-design.md); i checkpoint
storici restano con le misure del loro tempo.

## Disegno

Bitmap a mano in `kernel/console/video/scripts/pointer/shape.py` (`ART`: `X` nero,
`o` bianco, `.` trasparente), freccia classica con coda, punta sull'angolo (0,0).
Ogni campione e 0 (trasparente), 16 (nero pieno) o 16<<5 (bianco pieno): nessun
valore intermedio. Lo script genera `kernel/console/video/pointer/shape.c` e la
copia di riferimento Python in `scripts/pointer/firmware.py`. Hotspot (0,0).

## Geometria derivata

| Grandezza | Prima (24x24) | Ora (18x18) |
|---|---|---|
| Pixel del bitmap | 576 | 324 |
| Backup sotto il cursore | 576 pixel (2304 byte) | 324 pixel (1296 byte) |
| Area massima di una transazione (cursore vecchio + gruppo da 8 celle + cursore nuovo) | 2176 | 324 + 1024 + 324 = 1672 |
| Accessi pixel massimi di una transazione (ripristino + testo + salvataggio + cursore) | 2752 | 3 x 324 + 1024 = 1996 |
| Posizione iniziale a 1024x768 | (500, 372) | (503, 375) |
| Limite destro/basso del bitmap | larghezza-24, altezza-24 | larghezza-18, altezza-18 |

Le celle restano 8x16: un bitmap tocca al massimo 4 colonne x 3 righe, come prima.

## Prove

Suite host `console`, `framebuffer` e `x86-framebuffer` passano (C/ASan, Rust, test Python
del runner, cppcheck); i test di bitmap, clamp Rust, budget delle transazioni e posizioni
attese sono aggiornati alle nuove costanti.

QEMU, immagine `-Ps2Input -Ps2Mouse -ConsoleShell` SHA256
`79AE150A732DD5AA51889046D5E37BA0D408361C0010574F0E5F528193F9FA30`: 10 boot
consecutivi, ciascuno con 6 scene confrontate pixel per pixel con la griglia di
riferimento e con i 324 pixel fisici del puntatore letti dal monitor (60 confronti
da 324 pixel), budget fisico 20416 ripristinato in tutti.
Evidenza `kernel/build/ps2-input-firmware-84a35a0b629646869a4f2f9dc6ba3b42`.

## Limiti

La resa visiva e quella del bitmap in QEMU/SDL; nessun giudizio estetico automatico.
La fluidita non cambia con la dimensione ([flush-cost.md](flush-cost.md)).
