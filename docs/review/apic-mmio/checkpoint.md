# Checkpoint: probe APIC con MMIO UC temporaneo

## Risultato e revisione

Tre parti coerenti: prestito di una pagina con cache UC e revoca,
lettura dei registri LAPIC/I/O APIC, validazione Rust di capacita e
limiti GSI con confronto alla MADT. IF resta zero; nessuna rotta IRQ
viene configurata. IOREGSEL e l'unico registro dispositivo scritto.

La revisione ha aggiunto la rilettura del selettore ripristinato e
testato sia l'errore di lettura sia un valore diverso. I fallimenti
recuperabili revocano la PTE e mantengono il budget dell'allocator.
La revoca non dipende dal successo del readback. La fotografia viene
pubblicata dopo il secondo controllo CPU e dell'inventario paging.

Il primo confronto QEMU dello snapshot ha segnalato un errore del
test: il literal PowerShell 0xfec00000 era interpretato come negativo.
Il confronto usa ora un valore UInt64 esplicito. Il kernel aveva letto
correttamente il registro; la campagna finale usa il test corretto e
la versione del probe con readback del selector restore.

## Verifica e artefatti

- Regressione host completa: modello C isolato del probe e 37 test
  Rust complessivi, inclusi tre nuovi per versioni, conteggi e GSI.
- C: fallimenti nelle sei letture, corruzione PTE, readback selettore
  errato, output invariato prima della pubblicazione, alias, riserve,
  copertura e attributi, UC nello slot quattro, due I/O APIC,
  sovrapposizione GSI e ultimo GSI rappresentabile.
- Immagine ELF valida e sette mutazioni invalide verificate.
- Campagna QEMU finale: 23 scenari con monitor e fault intenzionali,
  copie persistenti confrontate con i log e finestra MMIO revocata.
  Directory: kernel/build/firmware-84db9cfce3014f88958e6667f61fff8c.
- SHA256 kernel ELF: E1B113A482D4A3CDA8A22475A4F827E6863720EA44B387881DB1EEDAB57BCF38.
  Build, ESP installata e immagine ready della campagna coincidono.

I test riguardano la working tree corrente, incluse le modifiche utente
preesistenti nei file di diagnostica e nell'header ACPI. Queste modifiche
sono state preservate e non sono incluse nel commit di questa task;
l'hash sopra identifica l'immagine testata, non una rebuild del solo commit.

ELF 81920 byte, segmenti residenti arrotondati a pagina 90112 byte:
una pagina in piu rispetto al checkpoint precedente. Inventario
normale: 15 tabelle e 111 foglie. Il probe non alloca frame e mantiene
solo 528 byte in BSS per le capacita, oltre alla bozza temporanea sullo
stack. I mapping dispositivi non rimangono presenti nell'inventario.

QEMU espone LAPIC versione 0x00050014, I/O APIC 0x00170020 e 24
ingressi nell'intervallo [0,24). Questi valori sono attesi per il
modello emulato della campagna, non per hardware generico.

## Limiti e prossimo blocco

UP con IF=0, xAPIC e profilo iniziale limitato; MTRR firmware assunti
validi. I fault hardware rimangono terminali. Mapping persistenti per
handler, configurazione IRQ, EOI e ritorno da interrupt non sono ancora
implementati. Nessuna prova su hardware fisico. Il modello host non
simula tutta la microarchitettura o il bus MMIO.

Circa due blocchi prima della prova del primo driver seriale a IRQ:
configurazione controller e percorso IRQ con ritorno, poi seriale con
una sorgente controllata. La durata dipende dai problemi rilevati nei
test; questa e una stima di lavoro, non una percentuale di un OS completo.

[Contratto e fonti hardware](../../kernel/interrupts/apic-mmio.md).
