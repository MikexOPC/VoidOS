# Durata della source timer e consumo degli eventi

Il timer espone quattro operazioni separate: inizializzazione degli
eventi dopo calibrazione, riarmo, consumo e chiusura. Una subscription
IRQ rimane registrata tra i riarmi; il gestore pubblica una sola consegna
alla volta e il kernel la consuma fuori dall'interrupt. Non ci sono
allocazioni, callback dinamiche o una coda senza limite.

## Stati e proprieta

L'owner statico di 144 byte conserva Count, Consumed e Phase.
Rust valida questi stati prima delle operazioni C:

| Phase | Stato | Vincolo |
| --- | --- | --- |
| 0 | Prima dell'inizializzazione | Count=Consumed=0, Active=0 |
| 1 | Pronto | Count=Consumed, Active=0 |
| 2 | Armato | Count=Consumed, Active=1, Count non saturo |
| 3 | Evento disponibile | Count=Consumed+1, Active=1 |
| 4 | Chiuso | Count=Consumed, Active=0 |

L'handler ferma la source, verifica EOI e solo allora incrementa Count
e pubblica Phase=3. Il consumer richiede source mascherata e ferma,
LAPIC idle, IF=0 e un output u32 valido, mappato e disgiunto dall'owner,
mapping MMIO, finestra e vista ricorsiva. Il chiamante possiede quattro
byte scrivibili per l'output; il driver non conserva il puntatore.
Un rifiuto conserva output e sequenza. Il consumo pubblica il numero
dell'evento e porta Consumed a Count, Phase a 1 e Active a zero.

Riarmare un evento pendente o consumarlo due volte viene rifiutato.
Il contatore non va in wrap: l'ultima consegna u32 puo essere consumata,
ma il riarmo successivo fallisce. Il registro IRQ verifica che il token
sia corrente, senza fault e senza callback gia in esecuzione prima
di permettere il riarmo. Un vecchio token non autorizza una source.

L'inizializzazione richiede calibrazione completa e hardware inattivo.
La chiusura richiede tutti gli eventi consumati e token azzerato dopo
unbind quiescente. Una source chiusa non viene riaperta implicitamente.
Errori hardware mantengono un fault e interrompono il bootstrap;
non viene dichiarato un recupero automatico del timer.

## Profilo corrente

Queste API sono per codice kernel fidato su BSP, con IF=0 ai confini.
Il caller possiede l'owner, la durata del mapping e il callback che
gestisce quella source. Non sono interfacce user mode o lock SMP.
La prova bootstrap esegue ancora tre eventi e poi chiude il driver:
QEMU verifica Count=Consumed=3, Phase=4, token vuoto e controller fermo.
Host verifica mille cicli sullo stesso owner con una fixture del token;
il registro reale viene testato separatamente con token vivi, scaduti,
riutilizzati, fault e callback in corso. QEMU usa la subscription reale.

La durata ora e esplicita e riusabile; non e ancora presente un servizio
timer permanentemente attivo dopo KERNEL_READY. Attesa HLT e watchdog
restano soggetti ai limiti descritti in [attesa eventi](../interrupts/event-wait.md).
Un futuro ciclo permanente deve definire chi decide arresto e timeout
prima di aggiungere scheduling o altri driver concorrenti.

[Intel SDM, timer APIC](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
definisce oneshot e arresto tramite Initial Count zero. Il modello degli
eventi e una scelta del nostro kernel sopra quel comportamento hardware.
