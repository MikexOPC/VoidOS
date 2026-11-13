# Ricerca e alternative — scheduler v0

Consultazione: 8 ottobre 2026. Le scelte qui motivate appartengono al progetto;
nessun codice di altri sistemi e stato copiato. Baseline locale `14649ba`.

## Riferimenti comparabili

[MIT xv6-riscv, kernel/proc.c, branch riscv](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/proc.c):
scheduler, sched, yield e lifecycle separano selezione, passaggio del contesto
e terminazione. Il sistema usa anche processi utente, lock e multiprocessore;
questi prerequisiti non si trasferiscono automaticamente al nostro profilo BSP.
Riferimento educativo, non benchmark di produzione o architettura da copiare.

[MIT xv6-riscv, kernel/swtch.S, branch riscv](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/swtch.S):
lo switch e una routine Assembly distinta che conserva un contesto di chiamata.
I suoi registri e offset sono RISC-V, quindi non utilizzabili nel nostro backend
MSVC x86-64. Si adotta la separazione delle responsabilita, non le istruzioni.

La versione consultata e identificata come branch riscv alla data sopra:
non e una release numerata e il branch puo cambiare. Non si e ottenuto un SHA
immutabile dalla pagina consultata; il riferimento non viene presentato come
snapshot riproducibile. Il progetto locale non dipende dal suo sorgente.

[Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170):
la toolchain locale determina stack, registri e conservazione SIMD. Questa e
la fonte del contratto backend, non l'ABI dello scheduler xv6.

[Rust Reference, Type layout](https://doc.rust-lang.org/reference/type-layout.html)
e [Rustonomicon, FFI](https://doc.rust-lang.org/nomicon/ffi.html):
il gateway fra linguaggi richiede rappresentazioni esplicite e un confine unsafe
per i pointer; i riferimenti devono rispettare aliasing e durata. Il core safe
non certifica stack/pointer C o Assembly. Uso previsto no_std, panic abort,
ABI C e soli handle/stati scalari attraverso il gateway.

## Alternative considerate

| Scelta | Vantaggio nel nostro progetto | Costo/limite | Decisione |
|---|---|---|---|
| Callback su stack unico | Poco backend | Non sospende il contesto locale di una funzione | Scartata per il traguardo task |
| Switch diretto task -> task | Meno passaggi per yield | Exit/cleanup e failure distribuiti | Rinviata a misure |
| Dispatcher centrale | Raccolta e transizioni sullo stack stabile | Due switch per cessione | Scelta v0 |
| Preemption dal timer subito | Puo interrompere task non cooperanti | Frame IRQ, lifetime, sincronizzazione e stato esteso piu ampi | Blocco successivo |
| FIFO a capacita fissa | Limiti espliciti, ordine verificabile, niente heap | Nessuna priorita o fairness temporale | Scelta v0 |
| Code dinamiche/priorita | Crescita e policy piu ricche | Allocazione e requisiti workload non ancora definiti | Rinviata |

Questi sono compromessi progettuali, non risultati di performance. FIFO garantisce
ordine tra task che cedono, non progresso se uno non cede. Nessun quantum e
promesso: manca preemption. Non si impone CFS o altra politica Linux futura.

## Interfacce locali verificate

- [Allocatore fisico](../../mm/physical/allocator.h): lease con owner/identita,
  allocate/release/pin; l'indirizzo fisico non e un mapping task.
- [Stack principale](../../../arch/x86/kernel/stack/main.h): owner statico e
  passaggio permanente al nuovo stack; resta il dispatcher.
- [Stack bootstrap](../../../arch/x86/kernel/descriptors/stacks.h): commit
  permanente e profili bootstrap, non un'API di rilascio task riutilizzabile.
- [Finestra pagina](../../../arch/x86/mm/window/page.h): prestito temporaneo;
  non e storage per uno stack che sopravvive allo switch.
- [Build](../../scripts/build.ps1): MSVC C11/W4/WX e Rust target
  x86_64-pc-windows-msvc, panic abort; backend MASM.

I link arch di questa sezione sono relativi alla root del repository, attraversata
dalla cartella documentale; il controllo link deve risolverli realmente.
Il modulo scheduler non esisteva alla baseline. La verifica ha usato inventario
dei percorsi e interfacce pubbliche, senza una nuova review del codice degli
altri moduli. Non si assume che le loro prove sostituiscano i nuovi gate task.

## Esito della progettazione

Scelta cooperativa gia autorizzata dall'utente; dettagli v0 nel [design](design.md).
Contratto x86 nel [context switch](context.md), casi e gate nella
[validazione](validation.md). Esito del blocco originale: progetto documentato.
Questo e l'esito storico del progetto documentale. Il successivo [core Rust/FFI](core.md),
il [backend e le prove native](../../../docs/validation/consolidamento.md#validazione-dei-backend-x86)
e l'[estensione eventi/attese](../wait/documentation/validation.md) hanno report distinti.
