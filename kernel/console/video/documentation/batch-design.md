# Batch bounded — 10 ottobre 2026

Scelta utente: massimo8 celle adiacenti sulla stessa riga; scansione completa
prima e verifica dell'autorità dopo. Nessuna autorizzazione riutilizzata tra
gruppi. La [profilazione](profile-validation.md) ha individuato la fase
inventario come costo dominante. Si riusa x86_fb_text, già limitato e con
font/UTF-8/bounds validati, anziché introdurre un nuovo writer hardware.
Fonti hardware e cache UC rimangono quelle del [gateway](rendering.md).

```text
prima cella dirty -> cattura fino a8 vicine dirty, senza attraversare riga/gap
 -> counter preflight -> owner/render Busy
 -> UTF-8 bounded32 byte -> snapshot dei soli stack verificati
 -> inventario completo -> fino a1024 store/readback -> autorità finale
 -> ripristino inventario baseline -> verifica celle catturate/geometria/counters
 -> ack delle sole celle catturate -> accounting -> rilascio Busy
```

Owner batch originale C, legato al renderer/griglia e callback stabili;
la RAM scalars catturata sullo stack è valida soltanto durante la callback
sincrona. Un solo gruppo per flush, budget1..8. Render ABI precedente intatto;
il flusher singolo conserva il proprio comportamento. Il misuratore condivide
clock/policy Rust, ma per il batch conta il delta1..8 celle e un solo flush.
La griglia è posseduta dalla task sul BSP; non è un'API concorrente/SMP.

Failure di draw, tick zero, mutazione delle celle catturate/geometria o overflow
sono terminali per l'owner batch. Nessun ack dopo callback fallita; prima
dell'ack si verificano tutte le celle catturate e lo stato del renderer.
Non si promette rollback dei pixel già scritti. Gli errori di argomento
prima dell'avvio (budget/copied owner) sono respinti senza disegno.
Nessuna allocazione dinamica, nuovo lease o cambio cache/IRQ.

TestConsoleBatch è isolato/non installabile, implica il profilo Draw;
TestConsoleDraw conserva una cella. Il worker8x1 produce A/è/€ ripetuti,
confronta gruppi/celle/profiler e torna alla finestra IRQ Assembly esistente.
La misura MAX_PM riguarda l'intero gruppo, non la singola cella; SlowCells
conta le celle appartenenti a gruppi oltre10ms. Gate100ms invariato.
Letture clock e pause host sono incluse. Nessuna protezione da stallo IF0.

Accettazione: workload sparse su tutti i256 mask e8 budget, row/gap/reentry,
mutation/failure/overflow/accounting C/ASan, UTF-8 e limiti gateway, ripristino
snapshot. Dieci boot finali batch e singoli; regressioni ordinarie e senza
video, pixel fisici/visibili di tutto il gruppo, input/perdite e cleanup.
L'output shell rimane seriale; il blocco misura/ottimizza solo il renderer.
