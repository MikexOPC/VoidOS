# Discovery MADT

Il kernel individua una sola tabella `APIC` nella directory ACPI
persistente e conserva una topologia normalizzata nell'arena.
Questo blocco legge informazioni firmware: non configura controller,
non abilita interrupt e non avvia le altre CPU.

## Lettura e pubblicazione

Gli header figli vengono letti attraverso la finestra temporanea
supervisor R/NX gia disponibile. Un header illeggibile, una MADT
duplicata o una MADT assente rendono non valido questo profilo x86.
L'assenza di ACPI resta il percorso separato gia previsto dal kernel.

Il parser controlla header, MAXPHYADDR, lunghezze dei singoli record
e checksum incrementale dell'intera MADT. Prima legge il prefisso di
due byte, poi il record completo, confrontando entrambi i prefissi.
Non alloca un buffer grande quanto la tabella. L'output del parser
rimane invariato in caso di errore; viene pubblicato dopo la verifica
della bozza locale. La pubblicazione e logica, non una primitiva
atomica utilizzabile da CPU concorrenti.

Il lettore controlla la riserva ACPI della mappa UEFI e revoca i mapping
temporanei. La discovery deve lasciare invariati il budget dei frame
e il registro paging. Il modello corrente e UP, con IF=0.

## Record e limiti del profilo

- Header: revisioni 1-6, dimensione 44-65536 byte, massimo 1024 record.
- CPU: record Local APIC e Local x2APIC, massimo 64. UID e APIC ID
  sono identita distinte; non si assume che coincidano con un indice.
  Enabled viene normalizzato a 1, Online Capable a 2. Un record con
  entrambi i bit a zero viene ignorato secondo la specifica.
- I/O APIC: massimo 16, indirizzi, ID e GSI iniziali distinti.
- Override ISA: massimo 16, una voce per sorgente 0-15, bus ISA.
- NMI: massimo 64 record globali, Local APIC o Local x2APIC.
  LINT e limitato a 0/1; UID wildcard conservato esplicitamente.
- Override dell'indirizzo LAPIC: al massimo uno, indirizzo a 64 bit.

I controlli Rust operano su valori scalari, senza puntatori o `unsafe`:
firma e dimensioni dell'header, lunghezze dei record, flag MPS e campi
CPU normalizzati. Il C gestisce letture, ownership e relazioni tra record.

I bit firmware riservati vengono ignorati in lettura; i campi riservati
della nostra copia e le code inutilizzate devono invece restare zero.
Le codifiche riservate di polarita/trigger MPS vengono respinte.
Il profilo richiede pagine controller da 4 KiB allineate e disgiunte
dalla RAM della MADT. E una restrizione iniziale, non una pretesa di
supportare ogni piattaforma permessa da ACPI.

NMI locali sovrapposti sullo stesso LINT, inclusi wildcard e UID
specifici, vengono respinti come ambigui. Un NMI globale non puo
condividere un GSI esplicitamente destinato a un override ISA.
La verifica delle rotte implicite resta al futuro gestore IRQ.

I record non implementati vengono saltati solo dopo il controllo
della lunghezza e inclusi nel checksum. Il contatore genera un avviso:
la discovery non autorizza la futura attivazione di un modello hardware
parzialmente interpretato. Le lunghezze dei tipi implementati devono
essere esattamente quelle del profilo corrente.

## Memoria persistente

La topologia occupa 2624 byte, allineamento 8, nell'arena esistente.
Gli oggetti persistenti complessivi passano da 5368 a 7992 byte.
Non vengono allocati nuovi frame per questa fotografia. L'handle
rimane nel kernel; la copia non dipende dallo stack o dal buffer firmware.
I frame di bozza e parser sono separati e ciascuno sotto 4 KiB;
il consumo annidato resta entro lo stack principale da 64 KiB.

Il checksum ACPI rileva corruzioni, non autentica il firmware.
La successiva verifica della copia controlla invarianti strutturali,
non ricostruisce i byte firmware originali o la loro provenienza.

## Cosa resta prima dei driver IRQ

Il [preflight BSP/APIC/PAT](../interrupts/apic-profile.md) verifica ora
stato BSP, modalita ereditata, base LAPIC e associazione dell'Initial
APIC ID con la MADT. Restano identita attuale del LAPIC, mapping MMIO
con cache policy appropriata e intervalli GSI dai registri I/O APIC.
MADT fornisce il GSI iniziale, non il numero degli ingressi. Seguono
handler con ritorno e una prova del driver seriale tramite interrupt.

## Test

`test/kernel/host.ps1` include il parser C e quattro test Rust:
mutazioni del checksum, letture fallite, troncamenti, duplicati,
limiti delle collezioni, record ignorati e output invariato su errore.
`test/kernel/firmware.ps1 -UseInstalled` confronta la copia persistente
con la RAM MADT originale nel monitor QEMU e verifica la finestra
revocata. Il caso a quattro CPU prova la discovery, non l'avvio SMP.

## Specifica

[ACPI 6.5, sezioni 5.2.1 e 5.2.12, MADT e strutture APIC](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html?highlight=gsi).
