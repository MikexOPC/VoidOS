# Libreria del processore x86

`arch/x86/lib/processor/` separa le istruzioni hardware dai controlli
numerici e dall'adattamento UEFI. Non alloca memoria e non modifica
registri di controllo, stack o tabelle di paging.

## Accesso hardware

`x86_query_cpuid` scrive EAX, EBX, ECX ed EDX in un array di quattro
`uint32_t`; il subleaf e zero. Il chiamante deve usare leaf disponibili
per la CPU. Un puntatore nullo non esegue l'istruzione; un puntatore
non nullo deve indicare quattro elementi scrivibili.

`x86_read_paging_registers` richiede esecuzione privilegiata x64 e una
CPU con CR0, CR3, CR4 e IA32_EFER utilizzabili. Il loader verifica prima
le capacita long mode e NX. Un processo Windows ordinario non puo usare
questa funzione. Il supporto del compilatore attuale e MSVC x64.
Un output non nullo deve indicare una struttura scrivibile e viva per
tutta la chiamata; la libreria non acquisisce la proprieta del buffer.

La funzione restituisce 1 e pubblica la struttura soltanto se CR3 ha lo
stesso valore all'inizio e alla fine. Su puntatore nullo o cambiamento
di CR3 restituisce 0 senza aggiornare l'output. Il confronto non rende
atomica la lettura di tutti i registri; il chiamante gestisce sincronizzazione
ed eventuali eccezioni dell'ambiente in cui la usa.

## Controlli numerici

`x86_validate_paging_profile` restituisce la larghezza fisica supportata
dal nostro profilo, oppure zero. `x86_validate_paging_state` restituisce
`X86_PAGING_VALID` insieme ai flag NXE, WP e PCID, oppure zero.
Sono funzioni Rust con interfaccia C: elaborano soltanto interi e possono
essere chiamate nei test host senza istruzioni privilegiate.

Il profilo conserva le regole del bootloader: long mode e NX richiesti,
larghezza fisica 32..52 bit, indirizzi lineari 48 o 57 bit supportati dalla
CPU, paging attivo a quattro livelli. LA57 attivo e rifiutato.
Vedere [capacita CPU](../paging/cpu-capabilities.md) e
[stato attivo](../paging/active-state.md) per i limiti del profilo.

## Verifica del confine fra linguaggi

Le intestazioni usano `stdint.h` e `extern "C"` per i consumatori C++.
Il test `test/libraries/shared_interface_test.cpp` del boot include le
sole intestazioni condivise e chiama funzioni C e Rust. Verifica anche
la dimensione della fotografia grezza dei registri e la maschera CR3.
Gli altri test CPU esistenti continuano a verificare risultati, limiti,
ordine CPUID e invalidazione degli adattatori UEFI.

Fonti: [Microsoft, CPUID](https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170),
[Microsoft, CR3](https://learn.microsoft.com/en-us/cpp/intrinsics/readcr3?view=msvc-170),
[Rust Reference, simboli e linkage](https://doc.rust-lang.org/reference/attributes/codegen.html#the-no_mangle-attribute).
