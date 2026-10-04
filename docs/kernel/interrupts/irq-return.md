# IDT completa, ritorno e controller controllati

Questo blocco prepara il percorso di interrupt ring zero del kernel.
IF rimane zero fuori dalle finestre controllate del driver. I test INT 0xf0 e INT 0xff provano ingresso e ritorno
senza passare dal LAPIC; non dimostrano ancora la consegna di un IRQ
hardware. COM1 prova separatamente la consegna hardware attraverso
il [registro persistente degli handler](registry.md).

## IDT e frame

La tabella persistente contiene 256 gate, 4096 byte allineati a 16.
I primi 32 gate vengono confrontati con gli ingressi terminali del
kernel e copiati senza modifiche. IST1, IST2 NMI e IST3 double fault
rimangono separati. La variante diagnostica #DF conserva il gate #GP
assente intenzionale. Gli altri gate sono interrupt gate DPL0, CS=8,
IST=0 e puntano a stub che inseriscono il numero di vettore.

SIDT verifica LIDT prima della pubblicazione. Un readback discordante
provoca rollback e seconda verifica; rollback fallito arresta la CPU.
La tabella sorgente e quella destinazione hanno lifetime statico.
Gli input, target e operando IDTR sono bounded e disgiunti prima delle
scritture. Rust verifica il limite 4095 e gli indirizzi canonici bassi.

Gli ingressi esterni salvano tutti i 15 GPR diversi da RSP. Il frame CPU
preserva RSP/SS, RIP/CS e RFLAGS; il solo vettore aggiunto occupa 8 byte.
Il frame viene preso in prestito dallo stack principale persistente.
Il dispatch controlla i confini dello stack e il profilo ring zero.

Prima del C, lo stack viene allineato a 16 con 32 byte di shadow space.
FXSAVE64/FXRSTOR64 preservano x87, MMX, XMM e MXCSR in 512 byte separati
dallo shadow space. CLD garantisce DF=0 durante C e Rust; IRETQ recupera
RFLAGS e il frame originario. Il backend verifica FXSR/SSE/SSE2,
CR0.EM/TS=0 e CR4.OSFXSR/OSXMMEXCPT=1. OSXSAVE attivo e respinto:
il percorso iniziale non promette preservazione AVX/AVX-512 o altri
stati XSAVE. Prima di introdurli occorre cambiare la strategia.

Il self-test 0xf0 e armato una sola volta. Controlla i 15 GPR, RSP,
DF e tutti i byte delle sentinelle XMM0/XMM15. Lo stub di questo solo
vettore modifica esplicitamente i due registri SIMD dopo il salvataggio,
cosi la prova richiede davvero il ripristino. Il chiamante recupera
anche i registri non volatili della propria ABI e ritorna con DF=0.
Il test 0xff esercita il ramo spurious; non emette EOI. Gli altri
vettori non assegnati arrestano il kernel con un marker, senza
allocazioni o logging nel dispatch. I vettori hardware 0x40 e 0x41
passano dal registro e richiedono una subscription valida; 0x41 e
riservato al futuro timer, attualmente non configurato.

## Controller

La configurazione riusa i prestiti MMIO UC della task precedente:
preflight di riserve, alias, PAT, CPU e inventario prima e dopo.
Non alloca frame e revoca ogni mapping, anche sul fallimento recuperabile.
Capacita e topologia vengono confrontate prima di modificare l'hardware.
Bit di versione LAPIC fuori dal profilo standard supportato sono respinti;
lo spazio APIC esteso AMD non viene configurato. Hardware fisico non testato.

ISR/IRR LAPIC e Delivery Status/Remote IRR I/O APIC devono essere vuoti
prima della configurazione. Non vengono svuotati con EOI speculativi.
Sulle piattaforme PCAT, i PIC legacy vengono mascherati alle porte IMR
0x21/0xa1 e riletti. Non vengono rimappati ne azzerati i loro IRR.

Ogni ingresso I/O APIC e prima mascherato, poi riceve destinazione BSP
fisica e vettore 0x40, fixed, active-high, edge, ancora mascherato.
Il vettore 0x40 e una destinazione di parcheggio: non e un driver attivo.
La programmazione della vera rotta COM1 applica override e conflitti
NMI globali prima di aprire l'ingresso. IOREGSEL viene ripristinato
e riletto; tutte le rotte vengono verificate in un secondo passaggio.

I LVT presenti secondo MaxLVT ricevono un vettore legale e maschera;
TPR e zero, SVR e 0x1ff, EOI broadcast non e soppresso. I registri
opzionali sono selezionati da un validatore Rust bounded.
Gli NMI locali MADT pertinenti al BSP sono configurati sui rispettivi
LINT con delivery NMI e polarita dichiarata. UID wildcard e UID BSP
sono distinti dall'APIC ID. Il profilo iniziale accetta solo trigger
edge/default; trigger level o conflitti causano rifiuto prima delle
scritture. Gli NMI continuano a usare il terminale IST2 con IF=0.

Un errore puo lasciare una configurazione hardware parziale: il chiamante
deve arrestarsi con IF=0. Non si ripristinano deliberatamente le vecchie
rotte aperte. La revoca PTE e garantita sui ritorni recuperabili, mentre
un dispositivo che ignora la scrittura del selettore puo impedirne il
ripristino: il readback rileva l'errore e impedisce il successo.
Fault hardware e NMI nel mezzo del setup rimangono terminali.

## Verifica e limiti

Host: mutazioni di ogni byte IDT, rollback riuscito/fallito, input e
alias; modello di ogni lettura/scrittura della configurazione, ingressi
fino a 120, LVT opzionali, errori e scritture ignorate, selezione NMI.
Rust controlla frame, IDTR e encoding senza dereferenziare puntatori.
QEMU: monitor indipendente di IDTR/gate, LVT, SVR, ISR/IRR, rotte I/O
APIC e IMR PIC, oltre alla regressione NMI, guardie, NX e double fault.

Il modello e UP, solo stack kernel, senza nesting maskable o user mode.
SMP, ritorno a ring 3 e XSAVE non sono implementati. COM1 usa EOI
verificato e un mapping LAPIC persistente. La seriale di log
continua a funzionare in polling.

## Riferimenti

[Intel SDM, Vol. 3A, interrupt IA-32e, LVT, SVR e spurious](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf).
[Intel SDM, Vol. 2A, FXSAVE64/FXRSTOR64](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf).
[Microsoft, ABI x64 e shadow space](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention).
[ACPI, MADT e Local APIC NMI](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html).
