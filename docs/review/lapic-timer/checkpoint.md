# Checkpoint: timer LAPIC calibrato

Validazione del 4 ottobre 2026, Windows/MSVC, Rust stable, QEMU q35
con CPU max e TCG. Questo checkpoint riguarda la prova finita del timer.

## Implementazione e correzioni

- FADT unica con checksum completo; PM timer System I/O 24/32 bit.
- Due campioni LAPIC mascherati; conversione e profilo verificati in Rust.
- Tre IRQ 0x41 registrati e consegnati senza riarmo nell'handler.
- EOI accettato solo per l'unico vettore previsto in ISR.
- Cleanup: timer fermo, unbind quiescente, LVT/divisore ripristinati.
- Attesa IRQ condivisa con COM1, senza duplicare il ciclo Assembly.

QEMU ha individuato un controllo iniziale troppo restrittivo: OVMF
lascia Initial Count=10.000.000 anche con Current Count=0 dopo una
scadenza terminata. L'acquisizione ora verifica oneshot mascherato,
Current Count=0 e LAPIC idle; azzera poi Initial Count prima di misurare.
Un test Rust rifiuta timer attivi, periodici o profili non validi.

Il monitor QEMU non legge correttamente i registri APIC con `x` MMIO.
La verifica indipendente usa `info lapic`, gia impiegato dal test APIC.

## Evidenza sullo stato corrente

- `kernel/test/host.ps1`: test C e 56 test Rust superati.
- FADT: checksum, GAS, duplicati, dimensioni, alias e profili invalidi.
- Calibrazione: wrap, clock fermo, overflow e scritture fallite.
- IRQ: stop, EOI fallito, vettore errato e piu ISR contemporanei.
- Build con warning come errori; immagine valida e 7 mutazioni respinte.
- 23 scenari QEMU superati, inclusi 128/256 MiB, smp4, NX, guardie,
  NMI, double fault e casi di rollback gia presenti.
- Tre IRQ 0x41 nella traccia, owner 120 byte, Count=Delivered=3,
  Error=Active=0, token azzerato, Initial/Current Count=0.
- Nove alterazioni del monitor timer respinte; restano verdi le 37
  alterazioni TX e le 16 del registro IRQ.

Log: `kernel/build/timer-host-final.log`, `timer-image-final.log`,
`timer-replay.log` e `firmware-ba75de291ad442309eca7339a0f5bf72/`.
La campagna e stata ripresa con gli stessi binari dopo la correzione
di un commento PowerShell nel verificatore; tutti i 23 casi rieseguiti.
ELF ready verificato: 122.880 byte, SHA256
`089CE9B0F8A4AB31CC0939471B92DD35937E537059409931EA9BD4D6B6542DAA`.
Le build possono differire nei timestamp incorporati dal linker.

## Limiti

Non e una validazione su PC fisico. Supporta il BSP xAPIC e il profilo
FADT dichiarato; AP attivi, PM timer MMIO, HPET e TSC deadline assenti.
IF resta zero dopo la prova. Non ci sono event loop permanente,
scheduler, isolamento user mode o filesystem. La tolleranza PM del test
non dimostra precisione temporale, prestazioni gaming o sicurezza globale.
Il prossimo blocco deve definire durata del timer e attesa eventi kernel
prima di introdurre scheduling o piu dispositivi concorrenti.
