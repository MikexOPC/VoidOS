# Contratto del context switch x86-64

Progetto dell'8 ottobre 2026; backend implementato e verificato su host Windows.
Vedere [implementazione, fonti correnti e prove](../../../arch/x86/kernel/scheduler/documentation/backend.md).
Mapping nativi, dispatcher e gate QEMU sono ora completati nel profilo cooperativo
BSP: [rapporto finale](../../../arch/x86/kernel/scheduler/documentation/integration.md).
Leggere con [design](design.md) e [accettazione](validation.md).

## Fonti e ABI

[Microsoft x64 calling convention](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170),
sezioni Calling convention defaults, Alignment, Caller/callee saved registers,
FPCSR e MXCSR: registri non volatili RBX/RBP/RDI/RSI/RSP/R12-R15 e XMM6-XMM15;
shadow space 32 byte, allineamento stack 16, stato di controllo FP da conservare.
Il backend deve rispettare questa ABI, usata dalla build MSVC/Rust del progetto.
Un esempio System V o RISC-V non sostituisce questo contratto.

[Intel SDM Vol. 2A, 253666-060US, settembre 2016](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf),
capitolo 3, voci FXSAVE e FXRSTOR: area 512 byte, allineamento 16, formato a
64 bit coerente fra salvataggio e ripristino. Fonte datata identificata
esplicitamente; prima dell'Assembly verificare anche l'edizione corrente della
[raccolta Intel](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

[Intel SDM Vol. 3, dicembre 2024](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf),
§7.8.1: IF maschera gli interrupt mascherabili; non dimostra assenza di NMI.
IF e RFLAGS[9], DF e RFLAGS[10]; lo switch v0 ammette IF=0 e DF=0 e non usa
POPFQ per ripristinare flag arbitrari. Nessun handler NMI entra nello scheduler.

## Layout scelto, da rendere assert in C/Assembly

Questi offset sono un formato software nuovo, non offset di registri hardware.

| Offset byte | Campo | Byte |
|---|---|---|
| 0 | RSP di ingresso nella routine switch | 8 |
| 8 | RBX | 8 |
| 16 | RBP | 8 |
| 24 | RDI | 8 |
| 32 | RSI | 8 |
| 40 | R12 | 8 |
| 48 | R13 | 8 |
| 56 | R14 | 8 |
| 64 | R15 | 8 |
| 72 | padding azzerato | 8 |
| 80 | immagine FXSAVE64 | 512 |

Totale 592 byte, allineamento 16; nove contesti (otto task e dispatcher)
occupano 5.328 byte, esclusi record nativi e owner Rust. RIP di ripresa e il
return address sullo stack salvato: non serve duplicarlo nel record.
Gli assert devono controllare ogni offsetof, sizeof e alignof, non solo il totale.
Evitare struct packed e conversioni a un layout Rust implicito.

La firma interna prevista e switch(old_context, next_context), puntatori in
RCX/RDX secondo ABI. Entrambi sono owner statici verificati, distinti, scrivibili,
allineati e non alias dello stack attivo. Sono backend privati, non user API.

## Sequenza logica, non Assembly eseguibile

1. C termina ogni FFI/prestito e verifica CPU, IF/DF, owner e stack.
2. Assembly salva RSP d'ingresso e gli otto GPR non volatili senza chiamate C.
3. Salva lo stato FP/SSE del contesto uscente nell'area verificata.
4. Ripristina lo stato FP/SSE del contesto entrante dal formato autorizzato.
5. Carica i GPR e il nuovo RSP; RET usa il return address sullo stack entrante.

Le routine non toccano CR3, descriptor, FS/GS base o page table. Il profilo non
ammette TLS per-task o modifica dei controlli CPU da parte dei task. Una CALL
volontaria richiede preservazione ABI; un futuro switch da IRQ dovra invece
conservare il frame interrotto completo e lo stato esteso applicabile.

## Primo ingresso: calcolo del frame sintetico

Top e l'estremo superiore esclusivo di uno stack RW/NX verificato, multiplo di
16. Tutti gli accessi seguenti devono essere dentro le pagine possedute:

```text
context.RSP = Top - 40
[Top - 40] = indirizzo RX della routine Assembly bootstrap
[Top - 32, Top) = 32 byte shadow space, inizialmente azzerati
RET dello switch -> bootstrap con RSP = Top - 32, allineato a 16
CALL bootstrap_C -> ingresso C con RSP = Top - 40, congruo a 8 modulo 16
```

La routine bootstrap Assembly e un ingresso speciale, non una funzione C chiamata
con il frame sintetico. Effettua CALL a un bootstrap C senza argomenti; il C legge
il task attivo dal record verificato e invoca la entry. Se il bootstrap C ritorna
malgrado il contratto noreturn, il backend entra in un percorso terminale previsto.
Non effettua RET verso dati arbitrari. L'exit normale cambia stack verso il
dispatcher e non torna sullo stack che sara raccolto.

Uno switch successivo salva il normale RSP d'ingresso alla routine Assembly,
con return address e shadow space gia predisposti dal caller ABI. C controlla
il proprio RSP prima di chiamare switch; i test Assembly verificano il frame
effettivamente salvato. Non si pretende che un controllo C anticipato conosca
il prologo e gli spill che il compilatore deve ancora emettere.

## Politica stato esteso e gate CPU

FXSAVE64/FXRSTOR64 e il formato v0 scelto per SSE/x87; non salva stato AVX esteso.
Prima dell'implementazione: verificare CPUID FXSR/SSE/SSE2, CR0.EM/TS e CR4.OSFXSR,
e ricontrollare sezioni/bit nell'edizione Intel usata dal backend. Non impostare
bit CPU per tentare di far passare un test. Il gate legge prerequisiti esistenti.

Vietati AVX/AVX-512/AMX nei task e nel backend v0: verificare flag di compilazione
e disassembly di oggetti C/Rust/Assembly, inclusi helper generati. Se compare una
istruzione fuori profilo, fermare il gate; FXSAVE non e un sostituto di XSAVE.

Il template FP iniziale deve essere prodotto da uno stato inizializzato valido,
con MXCSR compatibile con la mask rilevata, non da 512 byte zero o da un task
precedente. Azzerare i dati dei registri del nuovo task; non ereditare contenuti
del dispatcher. Documentare inizializzazione e salvataggio/ripristino senza
alterare lo stato FP del preparatore al ritorno.

Nessun unwinding attraverso lo switch, panic Rust abort, niente C++ exceptions,
SEH o setjmp/longjmp. L'Assembly cooperativo non sostituisce il backend degli
interrupt esistente. La validazione nativa deve verificare stack, GPR, XMM
non volatili e controllo FP: il semplice marker stampato non basta.
