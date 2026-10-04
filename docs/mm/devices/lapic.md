# Mapping LAPIC dedicato agli handler

`arch/x86/mm/devices/` espone un owner non trasferibile di 40 byte.
La foglia LAPIC e nello slot 288 della PT gia ereditata, all'indirizzo
0x0000700000120000, con guardie vuote nei due slot adiacenti. E separata
dalla finestra RAM, dagli IST, dallo stack principale e dall'arena.
Non vengono allocate nuove tabelle o pagine RAM.

## Contratto di bind, accesso e revoca

Il bind richiede IF=0 e owner vergine, disgiunto da tutti gli input
persistenti e dalla memory map. Riusa il preflight APIC per riserve
firmware, assenza di alias fisici, PAT UC e snapshot CPU corrente.
La radice e i genitori vengono verificati prima di leggere la foglia;
slot occupati o guardie non vuote vengono rifiutati senza adozione.

Rust genera la PTE con bounds fisici CPUID 32..52, allineamento 4 KiB
e soli bit cache PWT/PCD/PAT. Present, RW e NX sono obbligatori;
user, global e huge sono assenti. La scelta effettiva di UC viene dal
preflight del PAT, non dalla sola maschera Rust. Il bind pubblica la
foglia, esegue INVLPG e readback, e conserva il budget dell'allocator.
Un errore nella pubblicazione revoca soltanto lo slot appena acquisito.

Durante l'handler, Self/Ready/radice/guardie/PTE devono ancora coincidere.
Sono consentite solo letture dword ISR/IRR, con output disgiunto da
owner, MMIO, finestra RAM e vista ricorsiva. EOI richiede esclusivamente
ISR del vettore 0x40, scrive zero e verifica ISR vuoto. Non vengono
modificati Window.Active, lease o PTE della finestra temporanea.

L'unbind richiede IF=0 e ISR/IRR vuoti. Il chiamante deve prima chiudere
IER e mascherare la rotta. Una foglia estranea o corrotta impedisce la
revoca: non si azzera un mapping che non corrisponde all'owner.
Dopo PTE=0, INVLPG e readback, l'owner viene azzerato. Non si libera
la pagina fisica LAPIC, che e un dispositivo e non RAM allocata.

## Durata e limiti

Il mapping persiste attraverso gli arm e gli IRQ della prova COM1 e resta
pubblicato a KERNEL_READY. IER e rotta vengono chiusi, Active torna zero,
IF resta zero; l'owner LAPIC conserva Ready=1. Prima di aprire la sorgente
e dopo la chiusura si verifica l'inventario misto RAM/MMIO con questo owner.
La revoca resta disponibile nei percorsi di errore, dopo la chiusura
verificata della sorgente e della rotta; non forza la rimozione di PTE
estranee o di un dispositivo con interrupt pendenti.

L'autorita dell'owner richiede anche CPUID, MSR APIC_BASE e PAT correnti:
pagina fisica, modalita BSP xAPIC e tipo UC dell'indice selezionato devono
coincidere. [Contratto dell'inventario misto](../paging/mixed-inventory.md).
Non e ancora un ioremap generale; solo LAPIC xAPIC, BSP operativo,
nessun nesting maskable, nessuno shootdown TLB su altre CPU.
I/O APIC usa ancora la finestra durante il setup con IF=0.

Host: bind/unbind, doppio bind, slot/guardie occupati, rollback, foglia
corrotta, owner spostato, bounds/offset/output e IRQ con finestra RAM
simulata occupata. Due test Rust coprono limiti fisici e flag cache.
QEMU controlla tre consegne reali, owner LAPIC finale attivo, PTE presente
e guardie vuote dal monitor indipendente, oltre alle regressioni esistenti.

[Intel SDM Vol. 3A, paging, PAT e invalidazione TLB](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf).
