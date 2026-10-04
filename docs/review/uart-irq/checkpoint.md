# Checkpoint: prima consegna COM1 a IRQ

## Risultato e limiti

Tre interrupt hardware del modello UART QEMU arrivano al vettore 0x40
e tornano al kernel. L'handler conferma THRE, chiude IER, esegue EOI
LAPIC soltanto dopo ACK e verifica ISR vuoto. Il contatore aumenta
dopo il completamento. La prova termina con IF=0, rotta mascherata,
OUT2 ripristinato, owner inattivo, IDTR invariata e finestra revocata.

E il primo percorso IRQ hardware con ritorno verificato in emulazione.
Non e ancora una seriale TX/RX con code: nessun byte viene trasferito
da questa prova, e i log restano in polling. Il riarmo THRE a UART gia
vuota e verificato sul modello QEMU, non garantito su ogni UART fisica.
L'attesa e bounded in iterazioni, senza una scadenza temporale calibrata.
Hardware fisico non testato.

## Revisione e correzioni

- Rotta ISA con override MADT e una sola copertura I/O APIC: rifiuto
  di collisioni con IRQ ISA espliciti/impliciti e NMI globali.
- Rust accetta soltanto cause IIR 0x02/0xc2: niente bit riservati o
  FIFO non funzionante. Gli encoding di polarita/trigger sono bounded.
- Count/Error hanno offset ABI verificati; owner e rotta hanno dimensioni
  fisse, lifetime statico e nessuna allocazione dinamica.
- IF=1 soltanto nella loop Assembly: il foreground non usa la finestra
  condivisa, log o allocator. Configurazione e handler richiedono IF=0.
- EOI richiede un solo bit ISR, quello del vettore 0x40; un EOI ignorato,
  una causa inattesa o un IER non chiudibile impediscono il successo.
- Revoca PTE e budget verificati anche sui guasti recuperabili. Gli
  errori dell'handler sono terminali e non simulano un evento riuscito.
- La prima compilazione ha segnalato una variabile potenzialmente
  non inizializzata nel callback della rotta; inizializzazione corretta
  e build successiva senza warning.

## Evidenza finale

- Suite host kernel completa passata, inclusi guasti su ogni accesso
  della rotta e ogni lettura ISR prima/dopo EOI, ACK/EOI ordinati,
  IIR inatteso/riservato, IER bloccato, DLAB errato e owner inattivo.
- 43 test Rust complessivi: due nuovi per encoding MADT e causa UART.
- Immagine ELF valida e sette mutazioni invalide verificate.
- Campagna QEMU finale: kernel/build/firmware-c5ea2673bc4e467684263a6621da9829.
  QEMU 11.1.0, build v11.1.0-12130-ge470268ff4.
  Verifica di 23 scenari, ciascuno con tre IRQ 0x40 nella traccia CPU
  e snapshot Count=3, Error=0, Active=0. Incluse configurazioni 128/256
  MiB, quattro CPU dichiarate e regressioni NMI, double fault, NX e guardie.
- SHA256 ELF: 20A87754152CBA421A0B28DCC87202D6B39B43B1BCFC8F811EABE0E9D7E8B35D.
- ELF 102400 byte; segmenti residenti arrotondati a pagina 114688 byte,
  una pagina in piu rispetto al checkpoint IRQ precedente. Il nuovo
  modulo non alloca frame durante la prova.

I test includono le modifiche utente preesistenti ai diagnostici e
all'header ACPI, preservate fuori dal commit. L'hash identifica la
working tree testata, non una ricompilazione del solo commit.

## Prossimo blocco

Prima di ampliare il driver: mapping MMIO persistenti per gli handler
e ownership separata dalla finestra RAM temporanea; poi TX/RX con
buffer bounded e prove di trasferimento reale. Restano UP operativo,
ring zero, assenza di nesting maskable e niente AVX/XSAVE o user mode.
Quattro CPU nella MADT non significano quattro CPU kernel attive.

[Contratto e fonti verificate](../../kernel/serial/interrupts.md).
