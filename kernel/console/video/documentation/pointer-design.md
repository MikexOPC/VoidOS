# Puntatore software della console — 12 ottobre 2026

Implementazione e prove completate: [verifica](pointer-validation.md).
Le successive correzioni di lag e del contratto temporale input sono in
[input continuo e watchdog](pointer-latency.md).

Scelte utente: freccia arrotondata bianca con contorno nero,24x24 pixel,
trasparenza esterna, punto attivo sulla punta; ripristino mediante ridisegno
dalla griglia RAM. Modulo console video, con adattatori mouse/framebuffer,
build e test necessari. Nessun accesso hardware nuovo o copia di asset del vecchio OS.

## Ricerca e alternative

[Linux v6.12 fbcon](https://www.kernel.org/doc/html/v6.12/fb/fbcon.html), sezione
Attaching, Detaching and Unloading, separa console e driver framebuffer.
Manteniamo tale separazione; non copiamo codice, locking o API Linux.
La maschera bitmap e originale, ispirata alla sagoma concordata, non
estratta dall'immagine o da una libreria di cursori esterna.

A, scelta: la RAM conserva testo e caret; la bitmap viene composta con il
testo nello stesso gruppo video verificato. Movimento invalida le celle
vecchie e nuove; scrittura e scroll usano lo stesso renderer e mantengono
il puntatore sopra il contenuto. Nessuna lettura del framebuffer.

B: salvare i pixel sottostanti e ripristinarli. Richiede invalidazione del
backup quando cambia il contenuto; rinviato. Per un desktop il proprietario
della scena dovra sostituire la griglia come fonte del ridisegno.

## Contratto v0

- Owner statico originale, associato a una sola griglia e sessione; nessun heap.
- Rust no_std valida coordinate e delta signed, calcola clamp senza overflow.
  C mantiene lifetime, dirty e bitmap; nessuna modifica Assembly o IRQ.
- DeltaX aumenta X; DeltaY PS/2 positivo verso l'alto diminuisce Y sul video.
- L'intera bitmap resta nell'area di celle console, non nei margini firmware
  non rappresentati dalla RAM. Coordinate della bitmap, non una promessa
  sullo spostamento del solo hotspot oltre il bordo. Partenza al centro.
- La task3 aggiorna lo stato a IF0, senza yield durante la mutazione;
  segnala evento6. Solo task1 compone/disegna gruppi di massimo8 celle.
- Un movimento marca al massimo12 celle vecchie e12 nuove. Movimenti
  intercalati lasciano dirty anche posizioni non ancora ripristinate.
- Il gruppo cattura coordinate/visibilita; la bitmap copre solo i propri
  pixel e viene composta prima della verifica finale e dell'ack dirty.
- Testo/caret restano sotto il puntatore. I pulsanti sono registrati senza
  introdurre azioni di click o finestre.
- Stop nasconde la bitmap e marca il rettangolo corrente prima del drain
  del worker; cleanup richiede assenza di dirty e owner chiuso.
- Video assente conserva seriale e mouse; nessuna bitmap viene inizializzata.
- Errori di ownership, geometria, disegno e clock restano terminali; soltanto
  ritardi validi mantengono la politica WARN gia approvata.

Sequenza non eseguibile:

```text
console RAM -> owner pointer al centro -> worker disegna testo + maschera
task mouse -> valida movimento -> dirty vecchio/nuovo -> segnale worker
worker -> controllo lease -> testo/caret + puntatore -> verifica/ack
output o scroll -> dirty testo -> stessa composizione
stop -> nascondi e dirty puntatore -> drain -> close -> collect stack
```

## Prove richieste

Rust debug/release/Clippy: overflow, delta min/max, clamp, dimensioni e
stato invalido. C/ASan: ownership, copied owner, canary, bitmap, dirty,
movimenti intercalati, bordi, rientro e close. Backend: pixels RGB/BGR,
trasparenza, caret, puntatore sopra testo, ripristino e rollback delle risorse.
Dieci boot consecutivi con mouse/tastiera/shell; confronto screenshot e
pixel durante movimento e dopo testo/scroll, poi assenza di scie al cleanup.
Stress e video assente; nessun PASS prima dell'immagine finale.
