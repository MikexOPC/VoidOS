# Checkpoint: preflight BSP, APIC e PAT

## Task e risultato

La revisione ha separato il preflight CPU dalla futura estensione
MMIO: la finestra attuale espone RAM WB, mentre il LAPIC richiede UC.
Non si assume il PAT di reset e non si modifica il PAT ereditato.

Assembly legge CPUID, IA32_APIC_BASE e IA32_PAT soltanto dopo i
controlli delle feature e IF=0. Il C associa l'Initial APIC ID al
record MADT Enabled e verifica la base LAPIC; Rust controlla modalita,
bit, intervalli e selezione del primo slot UC forte. Due catture
devono concordare prima della pubblicazione del profilo persistente.

La revisione ha aggiunto il controllo della MADT prima di qualsiasi
lettura privilegiata, verifica degli intervalli e degli alias dei
buffer e assert sugli offset usati dall'assembly. Il profilo gia
pubblicato non puo essere sovrascritto dall'inizializzatore.
Nessun MSR o registro controller viene scritto; IF resta zero.

## Verifica sullo stato finale

- Regressione host kernel completa; 34 test Rust complessivi,
  tre nuovi per APIC/PAT e gli otto encoding PAT/PCD/PWT.
- C: BSP non primo nella MADT, UID distinto da ID, Enabled contro
  Online Capable, base incoerente, feature e limiti mancanti,
  x2APIC, bit riservati, PAT non standard e assenza di UC forte.
- Alias, output invariato sui fallimenti, corruzione dei campi
  derivati, doppia cattura incoerente, IF che cambia, riuso del
  profilo e MADT invalida respinta senza chiamare il backend.
- Tutti i 256 valori PAT nei sette slot non zero verificati
  contro le codifiche architetturali supportate.
- C11 /W4 /WX, Rust -D warnings, assembly /W3 /WX, link senza CRT.
- Convertitore ELF: immagine valida e sette mutazioni invalide respinte.
- Campagna finale QEMU 23/23: RAM 128/256 MiB, MADT con 1/4 CPU,
  preflight confrontato con la copia nel monitor, panic, guardie,
  NX, NMI, double fault, fault annidati e revoche. Nessun triple fault.

La prima campagna QEMU e passata 23/23. Dopo il controllo anticipato
della MADT, suite host, build e conversione sono state ripetute;
la campagna finale ha verificato l'immagine installata aggiornata.

Log host finale: `kernel/build/apic-profile-final-host.log`;
build: `kernel/build/apic-profile-final-build.log`; conversione:
`kernel/build/apic-profile-image.log`; firmware finale:
`kernel/build/apic-profile-final-firmware.log`.
Log finali: `kernel/build/firmware-ba50cff93e2649e0b60cd86eca7d9f43/`.
Il kernel ELF installato coincide con la copia ready usata da QEMU.

SHA-256 ELF installato:
`C30451DB1E6129A71906B780A73FCB5749FE98C97BF1A6D60C11A0F636DBF79E`.
EFI invariato:
`3989AFA5A43C255E58CC558206595997E646FB97F5D29399D7C0B52DDF9A703B`.

## Memoria e limiti

Profilo persistente: 80 byte di BSS, nessun nuovo frame o oggetto
arena. Il payload arena rimane 7992 byte. Due snapshot locali
occupano 96 byte, bozza e confronto 80 byte ciascuno in frame
separati. ELF 77824 byte su disco; segmenti residenti arrotondati
a pagina 86016 byte, una pagina in piu rispetto al checkpoint MADT.
Nei casi ready: 15 tabelle paging e 110 foglie ordinarie.

Modulo C 200 righe, test C 219, main 369, script firmware 374.
Header, validatori Rust e piccola routine assembly restano compatti.
Nessuna cartella del componente supera il limite di file.

Profilo operativo xAPIC gia abilitato, BSP, PA0 WB, uno slot UC
forte presente, indirizzo LAPIC entro 36 bit e MAXPHYADDR.
x2APIC viene rilevato e respinto senza tentare un downgrade.
Assenza ACPI mantiene il percorso senza preflight gia previsto.
Queste restrizioni non costituiscono supporto universale x86.

Initial APIC ID e ID attuale possono differire: la verifica MMIO
resta necessaria prima del routing. Il dump `info lapic` mostra
impostazioni LVT e spurious vector ereditate da OVMF, non una
configurazione del kernel. Nessun IRQ con ritorno, SMP o driver
seriale a interrupt viene dichiarato funzionante. Hardware fisico
non validato; le precondizioni negative delle letture privilegiate
sono coperte dal modello host e dalla revisione dell'assembly.

## Prossimo blocco

Mapping MMIO UC: controlli su regione fisica, riserve, alias e
MTRR; letture a 32 bit dei registri LAPIC e I/O APIC; verifica
dell'ID attuale e dei range GSI. L'inventario paging deve riconoscere
esplicitamente i mapping dispositivi, mantenendoli distinti dalla RAM.

Seguono configurazione controller/ingresso IRQ con ritorno e prova
della seriale a interrupt. Restano circa tre blocchi coerenti prima
di questa prova QEMU; il blocco hardware iniziale e stato diviso
per verificare la cache policy prima di effettuare accessi MMIO.

[Contratto preflight](../../kernel/interrupts/apic-profile.md).
[Intel SDM, Volumi 2, 3A e 4](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
