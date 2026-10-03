# Discovery ACPI e copia del RSDP

La discovery legge la ConfigurationTable UEFI prima di ExitBootServices.
Si cerca il GUID ACPI 2.0; il GUID ACPI 1.0 viene usato soltanto se manca
quello moderno. Una voce moderna presente ma invalida provoca un errore,
senza ripiego silenzioso. Voci duplicate del GUID scelto sono respinte.
Il numero di voci e limitato a 4096.

## Accessi e profilo supportato

Prima di leggere la lista e il RSDP si verifica la copertura completa
in un singolo descrittore della mappa UEFI. Il RSDP deve trovarsi in
EfiACPIReclaimMemory o EfiACPIMemoryNVS con capacita WB, senza read
protection. La lista puo trovarsi anche nella RAM del firmware.
Overflow, MMIO, intervalli non coperti e alias degli output sono respinti.
Il profilo iniziale richiede indirizzi fisici accessibili nel limite
inferiore canonico di 47 bit e nella larghezza dichiarata dalla CPU.

Vengono letti prima 20 byte. La revisione 0 e il formato legacy da
20 byte; la revisione 1 e respinta. Per revisioni da 2 in poi si richiede
il formato corrente da 36 byte: lunghezze diverse sono segnalate come
non supportate, prima di leggere eventuali estensioni. La firma include
lo spazio finale. Sono obbligatori sia il checksum dei primi 20 byte
sia quello dell'intero formato moderno. I byte firmware riservati
sono conservati e ignorati semanticamente, come richiesto da ACPI.

La libreria comune `lib/firmware/acpi/` e riusata dal loader e dal kernel.
Il C legge soltanto la copia bounded; Rust verifica schema, checksum
e limiti fisici attraverso argomenti scalari, senza puntatori o unsafe.
Non si assumono allineamenti inventati per le tabelle ACPI.

## Contratto e durata di vita

BOOT_START_INFO passa alla versione 3, 168 byte. Il campo ACPI occupa
56 byte: indirizzo fisico originale, lunghezza, GUID selezionato e copia
dei 20/36 byte. L'assenza e rappresentata da tutti i campi zero ed e
segnalata con WARN; un RSDP malformato interrompe la preparazione.
Non si richiedono nuove pagine: si riusa la pagina del contratto R/NX.

La mappa finale di ogni tentativo di uscita UEFI ricontrolla che il
RSDP presente resti riservato. Il refresh salva la copia prima di
ricostruire il contratto e la riattacca dopo la mappa aggiornata;
anche i retry preservano i dati. Attach esegue rollback del campo
ACPI se la verifica della nuova riserva fallisce.
Il kernel valida la copia all'ingresso,
la conserva nel proprio contesto e la verifica dopo la revoca dello
stack loader. L'indirizzo fisico rimane un numero: nessun puntatore
firmware viene dereferenziato dal kernel in questa fase.

Contesto kernel da 120 a 176 byte; contesto piu inventario da 144 a
200 byte. Con il registro paging, oggetti persistenti da 3240 a 3296
byte. Queste cifre rappresentano payload, non frame ulteriori.
Le immagini loader e kernel devono essere aggiornate insieme: v2
viene respinto, non reinterpretato come v3.

## Prossimo passo e limiti

La discovery valida numericamente l'indirizzo RSDT/XSDT. Il kernel
ora ne legge e verifica la tabella tramite la finestra R/NX descritta
in [directory ACPI](../../kernel/firmware/acpi-root.md). Le tabelle
figlie, inclusa MADT, richiedono ancora lettura e validazione proprie.
Nessun controller IRQ viene programmato e IF resta zero.
Il codice assume il normale modello di memoria UEFI prima dell'uscita
e input C leggibili; non promette isolamento da firmware o kernel ostili.
Non si dichiara ancora supporto su hardware fisico.

## Specifiche

- [ACPI 6.6, sezioni 5.2.5.2 e 5.2.5.3](https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf).
- [UEFI 2.11, ConfigurationTable e GUID ACPI](https://uefi.org/specs/UEFI/2.11/04_EFI_System_Table.html).
