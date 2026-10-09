# Scheduler cooperativo v0: integrazione e validazione finale

Snapshot del traguardo cooperativo finito (due task, IF=0). Stato
corrente: [docs/status.md](../../../../../docs/status.md). Wait, IRQ in
task, idle e persistenza sono report successivi, non questo file.

8 ottobre 2026. Modulo funzionale: scheduler, backend x86 e relative interfacce,
script, test e documentazione. I tre blocchi autorizzati sono completati:
mapping degli stack, dispatcher nel kernel, dieci boot consecutivi finali.
Questo risultato riguarda task kernel fidati, BSP singolo e carico finito con
IF=0; non certifica un sistema operativo pronto per hardware reale.

## Implementazione

La politica rimane nel core Rust: otto slot, FIFO, generazioni, transizioni
reserve/publish/select/yield/exit/collect e rifiuti senza mutazioni. Il gateway
FFI mantiene il prestito Rust entro una chiamata; nessun prestito attraversa
lo switch Assembly. Il dispatcher C gestisce owner, mapping e mailbox scalare.

- `mapping/native.c`: associa owner e contesti stabili al pool fisico e al CR3
  verificato. Controlla i parent prima di accedere alle viste ricorsive.
- `stacks.c`: allocate/pin/map/zero, pubblicazione solo dopo verifica, poi
  revoke/unpin/release prima della raccolta Rust. Un cleanup fallito conserva
  l'owner e termina il percorso; non inventa un rilascio riuscito.
- `integration/kernel.c`: prepara due task, esegue cento yield per task,
  raccoglie i due task terminati dal dispatcher e ripristina il budget fisico.
- `assembly/switch/context.asm`: switch reale di stack, GPR non volatili e
  stato x87/SSE con FXSAVE64/FXRSTOR64; ABI Microsoft x64 e shadow space.
- `assembly/profile/permissions.asm`: verifica read-only di CPL, CPUID e
  EFER prima di utilizzare stack NX. Nessuna modifica a CR3/CR0/CR4/EFER.
- `runtime/memory.c`: primitive freestanding richieste dal codice Rust
  effettivamente linkato; nessuna dipendenza dal runtime Windows.

Otto slot riservano 144 PTE: sedici pagine dati da 4 KiB e due guardie per slot.
Le guardie rimangono non presenti; i dati sono supervisor RW/XD. Uno stack
occupa 64 KiB fisici, quindi i due task occupano temporaneamente 128 KiB.
Il primo slot usa guardia `0x700000140000` e dati `0x700000141000`.
Tutte le 144 PTE devono essere assenti prima del bind e dopo il cleanup.
Non si adottano mapping estranei; sono tollerati soltanto i bit hardware A/D
nelle entry dati attese. Ogni pubblicazione/revoca esegue INVLPG per le 18 pagine.

Il dispatcher salva il contesto sullo stack principale esistente. Prima del
ritorno da ciascun task verifica identita del contesto, lease fisica unica e
pinnata, tutte le PTE dello stack principale, RSP, formato FP e return address
in codice supervisor RX. La stessa verifica di ownership/permessi protegge
l'ingresso nei task. Questi controlli riguardano contesti kernel fidati; non
costituiscono un'API user-copy o un isolamento di task ostili nello stesso ring.

L'integrazione in `kernel/main.c` avviene dopo la migrazione sullo stack principale
e prima dell'installazione dei driver IRQ. Alla fine il kernel prosegue fino
a `KERNEL_READY`. Il carico diagnostico esegue 202 dispatch, ossia 404 switch;
controlla canary locali, progressione, registri non volatili e stato FP/SSE.

## Fonti hardware e prerequisiti

[Intel SDM, raccolta ufficiale](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
edizione 093 settembre 2026, verificata con le copie registrate nel
[rapporto backend](backend.md). Vol. 3A §5.5.4: PTE P0/RW1/US2/XD63, A5/D6 e
formato a quattro livelli; §5.10.4: invalidazione delle traduzioni; §2.2.1 e
Table 2-1: EFER.LMA10/NXE11. Vol. 1 capitolo 21: CPUID.80000001H.EDX NX20/LM29.
Vol. 2A FXSAVE/FXRSTOR, Table 3-48: formato REX.W=1 e area 512 byte allineata16.
[Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)
definisce shadow space, allineamento e registri non volatili.

Il profilo richiede CPL0, BSP, CR3 esatto, IF/DF=0, CR0.PE/WP/PG e
CR4.OSFXSR, CPUID FXSR/SSE/SSE2, LMA/NXE. Respinge LA57, CET, PGE e PCIDE;
l'ampiezza fisica proviene dal pool/bootstrap validato. Nessun handler IRQ/NMI
entra nel gateway; NMI/DF usano IST e i fault sono terminali.

## Debug e miglioramenti applicati

1. Il primo link Rust staticlib includeva codice inutilizzato del core Windows
   con riferimenti a runtime ed exception handler. LTO fat/codegen-units=1 ha
   eliminato tali dipendenze. Non sono stati aggiunti unwinder fittizi.
2. Le primitive memoria compilate MSVC richiedono `#pragma function` per evitare
   definizioni di intrinsic rifiutate. Loop byte volatile evitano sostituzioni
   ricorsive del compilatore; non sono funzioni di copia da spazio user.
3. La verifica PowerShell delle PTE NX confrontava inizialmente un literal
   signed con UInt64. Maschere UInt64 esplicite hanno corretto il verificatore;
   non era un difetto delle PTE del kernel.
4. La review ha aggiunto la validazione del contesto dispatcher prima di
   yield/exit, oltre a quella dei task. Build, suite backend e tutti i boot
   riportati sotto sono successivi a questa modifica eseguibile finale.

## Evidenze finali

Percorsi relativi alla root `D:/OS`. Gli artefatti rimangono in `kernel/build/`.
La coverage di linee/branch non e stata misurata: **N/D**.

| Prova | Risultato | Directory evidenze |
|---|---|---|
| Rust debug e release | 13/13 per profilo, oracle 320.000 comandi per profilo | `kernel/build/scheduler-host-cea26f0ab8bd4dd5ab015a8fffa05f1f/` |
| FFI C/Rust, ASan | 486.644 verifiche, 80.000 select/yield | stessa directory |
| Backend CPU/contesto, ASan | 55.800 verifiche per run, 10 run | `kernel/build/scheduler-context-650e9ab43cfc45c1ac9b8b97ecd0c18e/` |
| Switch reale host | 4.004 switch per run, 10 run; quattro fault guardie per run | stessa directory |
| Risorse con mock allocatore | 361.000 verifiche per run, 10 run | stessa directory |
| Mapper/PTE con backing host | 554.149 verifiche per run, 10 run | stessa directory |
| QEMU positivo finale | **10/10 consecutivi** | `kernel/build/scheduler-firmware-3988eb4148e549678b83c03210f9496f/` |
| Guardia bassa | PF14, CR2 `0x700000140000`, error0 | `kernel/build/scheduler-firmware-c6776a75efb04428b7fd5712e8b11949/` |
| Guardia alta | PF14, CR2 `0x700000151000`, error0 | `kernel/build/scheduler-firmware-192dc41896e14c558f1caaa26c6094eb/` |
| Stack NX | PF14, CR2 `0x700000141000`, error17 | `kernel/build/scheduler-firmware-d27f2a31466b44e38bd0de4023b961ce/` |
| Stack revocato | PF14, CR2 `0x700000141000`, error0 | `kernel/build/scheduler-firmware-542d9df75d40483baa4f1389700d1679/` |
| Errore zeroing e rollback | budget ripristinato, mapping assenti, retry unico, KERNEL_READY | `kernel/build/scheduler-firmware-1e5af66443ec4b4485fecc811bbe0988/` |

Immagine positiva finale: `kernel/build/scheduler-native/kernel.elf`.
SHA256: `1FBEC8FCE70470D7ABD15FA1C38782AE3810013FF4A1460CFBD200B96EA76B77`.
Le cinque varianti hanno immagini distinte in `kernel/build/scheduler-variants-final/`;
ogni directory firmware registra gli hash in `images.json`, oltre a seriale,
stato registri CPU e lettura delle 144 PTE. ESP isolata per ogni boot, ELF copiato
e hash verificato, OVMF, q35/TCG/max, 128 MiB, una CPU, rete disabilitata.
Deadline 40 secondi; lo script termina soltanto la VM di cui possiede il processo.

Il PASS positivo richiede marker scheduler e KERNEL_READY, 202 dispatch,
budget iniziale/finale uguale, CR3 atteso, RSP nello stack principale, marker
CPU di arresto corretto e tutte le 144 PTE zero. Non basta una VM silenziosa.
I quattro fault intenzionali sono distinti da crash inattesi; la prova rollback
richiede il marker di recupero e i normali controlli positivi.

MSVC/MASM/Rust compilati con warning come errori; Clippy senza warning.
Cppcheck backend: zero error/warning/performance/portability, 13 style e 20
information rimangono nel report XML. Le segnalazioni style riguardano const
possibili, membri fixture non usati e funzioni invocate da Rust/Assembly o
dall'integrazione esterna al perimetro dell'analisi. Non sono state soppresse.
Le information includono header/configurazioni non modellati; cppcheck non
dimostra correttezza di registri, ABI o lifetime.

ASan strumenta i percorsi C delle fixture, della gestione risorse e del mapper;
lo switch reale host e separato, senza ASan/fiber hooks. Valgrind **non eseguito**.
Nel kernel sono verificati lease/pin, budget fisico e revoca PTE; questo e
accounting reale del carico provato, non una misura universale di assenza leak.

## Riproduzione

Da PowerShell nella root progetto:

```powershell
& ./kernel/scripts/build.ps1 -OutputDirectory D:/OS/kernel/build/scheduler-native
& ./arch/x86/kernel/scheduler/scripts/firmware.ps1 -Boots 10 -KernelDirectory D:/OS/kernel/build/scheduler-native
```

Per le varianti usare `-TestSchedulerFault guard-low|guard-high|nx|retired|rollback`
nella build e il valore corrispondente in `firmware.ps1 -Fault ... -Boots 1`,
con directory di output separate. Le build di fault rifiutano l'installazione.
Nessuna immagine di questa task e stata installata nell'ESP principale.

## Limiti di questo blocco

Completato il traguardo cooperativo finito a IF=0. In questo snapshot
restavano fuori preemption, ring 3, SMP, IRQ durante una task, wait
integrato con device, TLS/unwind, AVX/AMX/CET e hardware fisico.
SMEP/SMAP/KPTI non sono dichiarati implementati.

I blocchi successivi hanno verificato wait/epoca, IRQ 0x41 in una task
con IF=1 nel lavoro e mutazioni Owner a IF=0, idle e stop persistente.
Non riscrivono le prove di questa tabella.

Le verifiche native restano conservative: scansione PTE/lease a ogni
transizione. Nessun benchmark di overhead e dichiarato. Preemption
diretta romperebbe ancora il contratto cooperativo.
