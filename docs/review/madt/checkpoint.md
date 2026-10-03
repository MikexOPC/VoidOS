# Checkpoint: discovery MADT e topologia persistente

## Risultato

Il kernel individua MADT dopo ExitBootServices, usando la directory
ACPI e la finestra temporanea R/NX. Il parser C legge a piccoli blocchi;
Rust verifica campi scalari e dimensioni dei record senza `unsafe`.
La copia persistente conserva CPU, I/O APIC, override ISA, NMI e
l'eventuale override LAPIC a 64 bit. Il kernel verifica nuovamente
il registro paging dopo la discovery. IF resta zero.

La revisione ha controllato dimensioni prima degli accessi, overflow,
alias dei buffer, capienza degli array, unicita delle identita, code
zero, cleanup dell'oggetto e durata della copia. I record non supportati
sono contati e segnalati; non autorizzano la configurazione dei controller.

## Correzioni durante la task

Un test di capacita riutilizzava byte della fixture precedente: il
costruttore dei record ora azzera ogni record nuovo e l'intera suite
host e stata ripetuta con esito positivo.

La prima prova QEMU ha completato la discovery nel kernel ma ha
fallito il confronto nel monitor: PowerShell interpretava `0xffffffff`
come maschera con segno. Il test usa ora una maschera UInt64 esplicita;
il replay del monitor precedente passa. La campagna finale parte da
una nuova build installata dello stato finale dei sorgenti.

## Validazione

- Regressione host kernel completa; 31 test Rust complessivi, quattro
  nuovi per MADT. Tutte le 65536 combinazioni u16 dei flag MPS coperte.
- Parser C: 640 alterazioni singole di bit nella MADT respinte,
  output invariato su errore, fallimenti di ogni lettura della fixture,
  header incoerenti, record troncati, MADT assente o duplicata.
- Limiti: 64 CPU, 16 I/O APIC, 16 override ISA, 64 NMI e 1024 record
  non supportati accettati; eccessi respinti. Duplicati, conflitti NMI,
  indirizzi ostili, alias e corruzioni della copia verificati.
- C11 /W4 /WX e Rust -D warnings; link senza CRT.
- Convertitore ELF: immagine valida e sette mutazioni invalide respinte.
- 23/23 scenari QEMU finali: RAM 128/256 MiB, discovery con 1/4 CPU,
  panic, NX, guardie, NMI, double fault, fault annidati, revoche e
  ritorno imprevisto. Nessun triple fault rilevato.
- Monitor: indirizzo, header MADT originale, conteggi, flag e copia
  nell'arena confrontati; finestra temporanea vuota nei casi che
  non la riutilizzano intenzionalmente per provocare fault.

Log host: `kernel/build/madt-host.log`; build finale:
`kernel/build/madt-final-build.log`; conversione:
`kernel/build/madt-final-image.log`; firmware:
`kernel/build/madt-final-firmware.log`.
Log finali: `kernel/build/firmware-355d1b4350a6494190280915547137de/`.
L'ELF installato coincide con quello copiato nei tre casi ready.

SHA-256 ELF installato:
`3176AF75DBF79B27BFE5014077065853BF750111831715B2E4DC04DA08A0E9C7`.
EFI invariato:
`3989AFA5A43C255E58CC558206595997E646FB97F5D29399D7C0B52DDF9A703B`.

## Costi e limiti

Topologia: 2624 byte, allineamento 8, payload persistente totale
7992 byte nell'arena esistente. Nessun nuovo frame per le letture
o la copia MADT. ELF su disco 73728 byte; segmenti residenti
arrotondati a pagina 81920 byte, due pagine in piu rispetto al
checkpoint XSDT. Nei casi ready: 15 tabelle paging e 109 foglie.

Parser C e gestione record C: 200 righe ciascuno; main 363,
script firmware principale 371. Header e validatori restano compatti
secondo il chiarimento dell'utente. I controlli firmware ACPI sono
separati in uno script del componente.

Profilo limitato a revisioni MADT 1-6, strutture implementate con
lunghezze esatte e pagine controller allineate da 4 KiB. Le NMI locali
ambigue vengono respinte. Checksum e validazione strutturale non
autenticano il firmware. Nessun AML, hotplug o avvio SMP implementato.
La discovery a quattro CPU non dimostra esecuzione concorrente.
Non si leggono ancora registri MMIO, non si conoscono gli intervalli
GSI completi e non si verifica ancora l'identita del BSP.
Hardware reale non validato.

## Prossime task

1. Verificare BSP/modalita APIC e preparare mapping MMIO con policy
   di cache appropriata; leggere e validare capacita e intervalli GSI.
2. Configurare controller e ingresso/ritorno IRQ, inizialmente mascherati.
3. Collegare e provare la seriale tramite interrupt su QEMU.

Stima orientativa: circa tre task coerenti prima del primo driver
seriale a interrupt, da rivedere se emergono vincoli hardware.
La seriale polling e gia utilizzata. Non e una percentuale dell'OS.

[Contratto MADT](../../kernel/firmware/madt.md).
[ACPI 6.5, MADT](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html?highlight=gsi).
