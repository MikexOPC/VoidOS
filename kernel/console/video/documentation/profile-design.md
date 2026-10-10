# Profilazione delle celle — 10 ottobre 2026

Modulo: console video, con soli punti di misura nel backend framebuffer già
usato dal collaudo. Nessun nuovo registro, mapping, dispositivo o criterio di
autorità. Restano tutti i controlli dell'[inventario posseduto](measured-design.md).
Le misure sono compilate solo con TestConsoleDraw; nessuna stampa tra i pixel.

## Sequenza e proprietà

Sette letture del clock PM già acquisito dalla sessione delimitano sei fasi:

| Fase | Intervallo |
| --- | --- |
| GATE | Ingresso callback, controlli task/stack, snapshot, gateway e ingresso testo |
| PLAN | Validazione font e preparazione bitmap UTF-8 |
| INVENTORY | pixels_begin: bounds, ownership, inventario completo e apertura sessione |
| PIXELS | Conversione colori, 128 store UC e readback |
| AUTHORITY | pixels_end: verifica finale dell'autorità |
| RETURN | Ritorno nel gateway, ripristino stato e chiusura callback |

GATE/INVENTORY non sono misure di una singola funzione: includono i controlli
indicati e il costo del clock. Il profiler osserva questa stessa sequenza su
ogni cella, a IF0; non attraversa una finestra IRQ. Contesto clock, owner e
mask originali; nessun buffer dinamico. I timestamp di fasi incomplete non
producono un campione valido. Il fallimento è terminale nel profilo diagnostico.

Il profiler rifiuta copie, ordine errato, bit esterni alla mask, overflow e
campioni senza progresso o oltre il gate esistente di 100 ms. Sono ammesse
fasi singole di zero tick. Le sei differenze modulo devono sommare alla
differenza totale dello stesso campione; gli aggregati vengono aggiornati
soltanto dopo tutti i controlli. Massimi di fasi diverse non si sommano per
ricostruire una cella che non è stata osservata. Il limite di un giro del
contatore e le deadline host restano quelli del precedente misuratore.

Fonte clock: [ACPI 6.6 §4.8.2.1 e §4.8.3.3, tabella 4.14](https://uefi.org/specs/ACPI/6.6/04_ACPI_Hardware_Specification.html#power-management-timer-pm-tmr),
già verificata nel design precedente. Solo x86_pm_read sul clock della
sessione: nessuna porta fissa o nuova istruzione privilegiata. La policy
temporale Rust resta condivisa; il profiler C possiede contatori e sequenza.

## Accettazione e interpretazione

Test C/ASan su wrap 24/32 bit, ordine, copie, clock fermo, overflow e
aggiornamento indivisibile degli aggregati. Wrapper Python respinge report
incompleti e somme/massimi incompatibili. Dieci boot dell'immagine finale,
input/shell seriale, pixel fisici e visibili, cleanup e budget ripristinato.

La profilazione aggiunge letture I/O e bookkeeping: non è un benchmark del
kernel ordinario né separa il tempo sottratto dal sistema host a QEMU. La
scelta dell'ottimizzazione segue gli aggregati misurati; ridurre controlli o
introdurre un'autorità memorizzata richiederebbe un contratto nuovo. Una
possibile task successiva è un batch bounded di celle, usando il disegno
testo esistente e mantenendo scansione completa prima e verifica finale dopo.
