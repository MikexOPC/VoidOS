# Timer LAPIC: prima prova calibrata

Il BSP misura il timer locale rispetto al PM timer ACPI a 3.579.545 Hz.
Due campioni di circa 10 ms, con divisore LAPIC 16, devono concordare
entro il 25%. Il reload viene calcolato in Rust con prodotto u64 e
arrotondamento per eccesso; si sceglie il minore dei due campioni.
Non viene presunta una frequenza CPU o APIC specifica di QEMU.

La FADT viene selezionata senza duplicati e letta attraverso la finestra
ACPI, verificando intervallo, header e checksum dell'intera tabella.
Sono supportati PM timer System I/O a 24 o 32 bit, accesso dword,
FADT di 116–4096 byte, revisioni 1–6. GAS non supportato, HW_REDUCED,
porte disallineate o sovrapposte a COM1 vengono rifiutati. Questo profilo
non copre ancora PM timer MMIO, HPET o TSC deadline.

## Acquisizione e durata

Il mapping LAPIC UC gia verificato viene prestato dal driver UART.
LVT precedente deve essere oneshot mascherato, Current Count zero e
LAPIC idle. Initial Count puo conservare il reload di una scadenza
firmware gia terminata: non viene confuso con un timer ancora attivo.
Prima della calibrazione viene comunque azzerato e verificato.

L'owner statico occupa 136 byte, inclusi contatori del sonno e delle prove
di evento gia consegnato. Non vengono allocate nuove pagine
RAM o nuovi mapping MMIO. C controlla ownership, letture e cleanup;
Rust verifica profilo, wrap PM, snapshot precedente e conversione;
Assembly legge la porta e riusa l'attesa IRQ delimitata.

Il vettore 0x41 viene registrato prima di armare tre scadenze oneshot.
Ogni gestore, con IF=0, verifica il profilo, ferma e maschera la sorgente,
manda EOI solo se 0x41 e l'unico vettore in servizio, poi incrementa
Count. Non alloca memoria, non stampa e non riarma durante l'IRQ.

Dopo ciascuna scadenza viene confrontato anche l'intervallo PM:
circa 5–200 ms, una tolleranza diagnostica ampia per TCG. Non costituisce
una garanzia temporale per uno scheduler. Attese e calibrazione hanno
budget finito di letture o risvegli; errori interrompono il bootstrap.
L'attesa IRQ usa ora STI/HLT/CLI con controllo del contatore prima del
sonno; un evento gia consegnato ritorna senza dormire. Il budget di
risvegli non e un timeout: se il timer non consegna alcun IRQ, serve
un watchdog indipendente. In QEMU lo script impone un limite esterno.

Il cleanup tenta maschera e arresto, richiede quiescenza prima di unbind,
poi ripristina LVT e divisore precedenti. Initial Count resta zero;
riscrivere il vecchio reload riavvierebbe il timer firmware scaduto.
La subscription non viene rimossa se la sorgente non risulta fermata.

## Limiti e riferimenti

Questa e una prova finita su BSP: IF e attivo durante HLT e viene
disabilitato dopo ogni risveglio. Dopo il bootstrap IF resta zero.
Non sono ancora presenti tick permanente, event loop, scheduler,
AP attivi o validazione su hardware fisico.

[Intel SDM, sezione APIC timer](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf)
definisce oneshot, registri Count, divisore e arresto tramite Initial Count zero.
[ACPI, FADT](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html)
e [registro PM timer](https://www.uefi.org/sites/default/files/resources/ACPI_6_0_Errata_A.PDF)
definiscono indirizzo, frequenza e larghezza del contatore.
