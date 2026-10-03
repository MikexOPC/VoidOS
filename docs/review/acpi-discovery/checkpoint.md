# Checkpoint: discovery ACPI e consegna del RSDP

## Risultato e revisione

Il loader cerca il RSDP nella ConfigurationTable UEFI, preferisce il
GUID moderno e verifica copertura RAM, firma, dimensioni e checksum.
Un riferimento moderno malformato non provoca fallback sul legacy.
La copia viene pubblicata soltanto dopo verifica; l'assenza e uno
stato esplicito, distinto da una tabella presente ma invalida.

Il contratto v3 contiene una copia numerica del RSDP, senza puntatori
da dereferenziare nel kernel. La libreria C comune legge campi bounded;
Rust verifica schema, checksum e limiti fisici con argomenti scalari,
senza unsafe. Il kernel controlla la copia e la conserva nel contesto
persistente dopo la revoca dello stack loader.

La prima prova QEMU ha trovato un difetto reale: il refresh finale
ricostruiva il contratto e perdeva il campo ACPI. Il refresh ora salva
la copia e la riattacca dopo la mappa aggiornata. Le regressioni
verificano due e tre tentativi di ExitBootServices e il rifiuto di una
riserva ACPI che diventa memoria convenzionale tra i tentativi.
L'attach ripristina il campo precedente se la nuova riserva fallisce.

Sono stati corretti anche due problemi di build: un include stdint
non disponibile nel kernel diagnostico freestanding e una copia di
struttura che generava una dipendenza dal runtime. La copia del
contratto usa un ciclo bounded; l'immagine finale non richiede CRT.

## Validazione sullo stato finale

- Build e regressione host completa del bootloader, inclusi 29 test
  Rust e 17 scenari simulati di uscita terminale.
- Nuova suite C: selezione GUID, legacy, assenza, 288 mutazioni del
  RSDP, checksum separati, alias, limiti e durata di vita della copia.
- Tre nuovi test Rust: revisioni, firme, checksum e limiti CPU.
- Regressione host kernel e 25 test Rust complessivi superati;
  56 corruzioni della rappresentazione ACPI assente respinte senza
  pubblicare un nuovo contesto; contratto v2 respinto.
- Compilazione con warning trattati come errori; convertitore ELF:
  immagine valida e sette mutazioni respinte.
- 22/22 scenari QEMU: RAM 128/256 MiB, panic, mapping, stack revocati,
  guardie, NX, NMI, double fault, fault annidato e ritorno imprevisto.
  Nessun triple fault rilevato.
- Il monitor verifica il contratto v3, la copia ACPI dentro l'arena,
  firma, indirizzo originale, XSDT e corrispondenza con i log loader.

Log host: `arch/x86/boot/uefi/build/acpi-build.log` e
`kernel/build/acpi-host.log`. Il primo log conserva la vecchia etichetta
"16 scenari": il codice eseguito include gia il diciassettesimo; il
messaggio nello script e stato corretto prima del commit.
Build e conversione: `kernel/build/acpi-build.log` e
`kernel/build/acpi-image.log`. Firmware: `kernel/build/acpi-firmware.log`,
cartella `kernel/build/firmware-c04f9001932d4fc19a03d128ac09d0f4/`.

ELF installato e copiato nei casi ready, SHA-256:
`C6693B72D658D4E28FD2A711CFA5A4803924B98613FEFFCD01A0F400BCD19ACE`.
EFI installato e copiato nei casi firmware, SHA-256:
`3989AFA5A43C255E58CC558206595997E646FB97F5D29399D7C0B52DDF9A703B`.

Un tentativo aggiuntivo con `q35,acpi=off` non ha prodotto output
seriale entro il timeout con questo OVMF. E stato rimosso dalla suite:
non dimostra il percorso ACPI assente nel kernel. L'assenza e verificata
nei test host, non in questa campagna firmware. I log del tentativo
restano in `kernel/build/firmware-bbe1ac5f706a48508e4fcb5147b61e6e/`.

## Costi e limiti

BOOT_START_INFO: 168 byte; copia ACPI: 56 byte; contesto kernel:
176 byte. Contesto e inventario: 200 byte; con registro paging:
3296 byte. Nessun nuovo frame; arena e pagina del contratto gia esistenti.
ELF su disco: 65536 byte; quattro segmenti residenti arrotondati a
pagina: 69632 byte. Modulo discovery C: 201 righe; test C: 228;
test terminale: 367; main kernel: 355; script firmware: 372.

Profilo bounded: massimo 4096 voci, RSDP contenuto in un singolo
descrittore ACPI WB leggibile, revisione 0 da 20 byte o revisioni
da 2 con lunghezza 36. Formati futuri piu lunghi non supportati.
I byte firmware riservati sono conservati senza imporne il contenuto.
Il checksum rileva alterazioni, non autentica il firmware.

XSDT/RSDT e MADT non sono ancora lette o validate. CPU singola, IF=0,
nessun controller IRQ programmato; hardware fisico ancora non validato.
La copia possiede i byte RSDP, non le tabelle fisiche puntate.

## Prossimo blocco verso il driver a interrupt

Il prossimo modulo deve leggere XSDT/RSDT tramite una vista fisica
controllata, verificarne header, dimensioni e checksum e individuare
MADT senza dereferenziare indirizzi firmware arbitrari. Seguono parsing
MADT, mapping MMIO, controller IRQ e ingresso/ritorno dagli interrupt.
La seriale COM1 in polling e gia presente. Stima orientativa aggiornata:
3-5 task mirate prima di un driver seriale a interrupt su QEMU;
la stima dipende da controller e problemi trovati nelle verifiche.

[Contratto e limiti della discovery](../../boot/acpi/discovery.md).
[ACPI 6.6, RSDP](https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf).
[UEFI 2.11, ConfigurationTable](https://uefi.org/specs/UEFI/2.11/04_EFI_System_Table.html).
