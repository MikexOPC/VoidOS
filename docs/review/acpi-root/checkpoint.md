# Checkpoint: directory XSDT/RSDT e lettura fisica ACPI

## Risultato e revisione

Il kernel legge XSDT/RSDT dopo ExitBootServices e dopo la revoca dello
stack loader. La finestra esistente espone una sola pagina supervisor
R/NX alla volta; la foglia viene rimossa e invalidata dopo ogni copia.
Si leggono soltanto pagine completamente riservate ACPI reclaim/NVS,
WB e non read-protected. Nessun lease, pin o frame viene assegnato.

Rust verifica layout SDT, conteggio massimo di 256 voci e intervalli
MAXPHYADDR. Il C gestisce copia, checksum, confronto dei due header,
duplicati e pubblicazione della directory. Gli indirizzi figli sono
numeri: non vengono dereferenziati o considerati tabelle gia validate.
La directory e conservata nell'arena e il suo handle resta nel kernel.
Il registro paging viene verificato nuovamente dopo le letture.

La build iniziale ha rilevato frame locali oltre la soglia che produce
__chkstk. Copia della tabella e bozza sono state separate in funzioni
con frame bounded; i controlli del compilatore non sono disabilitati.
Sono stati corretti un nome errato nella fixture e un ciclo di test
che veniva ottimizzato in memset. Il link finale resta senza CRT.
La revisione ha esteso gli alias alla capacita intera della mappa e
alla memoria posseduta dall'arena; i test sono stati ripetuti.

## Validazione sullo stato finale

- Regressione host kernel completa e 27 test Rust, inclusi due nuovi.
- Parser C: 514 capacita legacy/moderne da 0 a 256 voci; 257 respinto
  prima di leggere il contenuto completo; 416 alterazioni di bit
  respinte senza modificare l'output precedente.
- Errori della prima/seconda lettura, header incoerente, revisioni
  non supportate, lunghezze ostili, duplicati, limiti CPU e alias.
- Fotografia persistente: formato, conteggio, campo riservato e
  tutte le 254 voci inutilizzate verificate.
- Lettore x86: richiesta non allineata da 8192 byte su tre pagine,
  sei invalidazioni nell'ordine corretto, PTE R/NX con accessed,
  budget e NextId invariati; ACPI/NVS accettate, altri tipi respinti.
- Finestra occupata, alias metadati, read protection, overflow,
  limite di richiesta, buffer backend assente e riuso con lease.
- C11 /W4 /WX, Rust -D warnings; convertitore ELF: immagine valida
  e sette mutazioni invalide respinte.
- 22/22 scenari QEMU: RAM 128/256 MiB, panic, NX, stack e guardie,
  NMI, double fault, fault annidato, revoche e ritorno imprevisto;
  nessun triple fault rilevato.
- Monitor: directory nell'arena, indirizzo XSDT, lunghezza, conteggio,
  width e riservato; tutte le 16 PTE della finestra vuote nei casi
  che non le riutilizzano intenzionalmente per i fault di test.

Log host: `kernel/build/acpi-root-host.log`; build:
`kernel/build/acpi-root-build.log`; conversione:
`kernel/build/acpi-root-image.log`; firmware:
`kernel/build/acpi-root-firmware.log`.
Log finali: `kernel/build/firmware-8b09316d9f3146babefa55f121b5dd83/`.

SHA-256 ELF installato e copiato nei casi ready:
`D2F54F7DD51A04C1DF1950B0EFEFB52467AFBA10EE1836BC62341ACFEB56EBB8`.
EFI invariato rispetto al checkpoint precedente:
`3989AFA5A43C255E58CC558206595997E646FB97F5D29399D7C0B52DDF9A703B`.

## Costi e limiti

Directory 2072 byte, arena da 3296 a 5368 byte di payload; nessun
nuovo frame per letture o oggetto directory. ELF su disco 69632 byte;
quattro segmenti residenti arrotondati a pagina 73728 byte, una pagina
in piu rispetto al checkpoint RSDP. Nei casi ready: XSDT da 84 byte,
6 voci, 15 tabelle paging e 107 foglie ordinarie.

Root C 220 righe, test root C 201, finestra C 273, test finestra C 275,
main 359, firmware script 393. I piccoli header e validatori Rust
restano compatti secondo il chiarimento dell'utente.

Profilo iniziale: revisioni SDT root 1, massimo 256 voci, tabella
coperta da un singolo descrittore ACPI, CPU singola e IF=0. La
finestra non offre esclusione SMP. Gli output C richiedono buffer
validi delle dimensioni dichiarate; un eventuale errore dopo l'inizio
di una lettura puo lasciare un buffer parziale, mai pubblicato dal parser.

Il checksum non autentica il firmware. La fotografia valida struttura
e indirizzi, non rende impossibile una modifica verso altri indirizzi
validi. Non vengono interpretati FADT, AML o MADT; nessun controller
IRQ viene programmato. Assenza ACPI e RSDT sono coperte da test host;
la campagna OVMF corrente verifica XSDT. Hardware reale non validato.

## Prossimo blocco

Leggere gli header figli, individuare MADT e validarne checksum e
record senza uscire dalle dimensioni dichiarate. Seguono mapping MMIO,
controller e ingresso/ritorno IRQ. Stima orientativa: circa 3-4 task
prima del driver seriale a interrupt su QEMU, da aggiornare dopo MADT;
non e una percentuale di completamento dell'OS o del supporto hardware.

[Contratto del lettore e della directory](../../kernel/firmware/acpi-root.md).
[ACPI 6.5, RSDT/XSDT](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html?highlight=gsi).
[Intel SDM, paging e invalidazione](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
