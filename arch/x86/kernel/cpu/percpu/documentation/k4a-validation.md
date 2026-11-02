# K4-A — Evidenze e lavoro rimanente

2 novembre 2026. Verifica dopo i refactor K4 di `26044e4` e i fix della review:
diagnostico IPI e panic con **2, 4 e 8 CPU**, boot ordinario con 1 e 4 CPU.
Campagna **80/80**: 50 boot positivi e 30 panic intenzionali previsti.
Il codice accetta gruppi di 2–64 CPU; non sono prove hardware su 64 CPU.
Gli AP del boot ordinario mantengono il parcheggio K3 a IF=0.
K4 non è chiusa: integrazione ordinaria, shootdown e preemption restano da fare.

## Prove eseguite

Comando dalla root: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test/run.ps1 -Suite ipi-service`.

- Rust: 6/6 test, con warning negati. Configurazione e limiti fino a 64 CPU,
  ownership logica, lifecycle, token stantii, doppio consumo, esaurimento
  generazioni, errore terminale, contesa con tre consumer e gara ACK/timeout.
- C/FFI: 20/20 processi, ciascuno con 15.000 richieste e tre consumer
  concorrenti: 300.000 richieste sul vero oggetto Rust ottimizzato.
  Tutti gli handle Windows posseduti vengono chiusi.
- Attesa C: 9/9 processi, ACK, errore clock, clock fermo, salto indietro,
  valore fuori maschera, wrap 24/32 bit, deadline e argomenti invalidi.
- Backend C: preparazione e rifiuto di identità CPU, stato, profondità lock,
  frame/stack o puntatore FXSAVE non validi; dipendenze privilegiate modellate.
- Python: 10/10 test con sottocasi: formato registri, righe seriali incomplete,
  raccolta dopo errore su 2/8 CPU, conservazione dell'errore originale,
  attesa di tutti i core, default storico a quattro e argomenti invalidi.
- Audit: cinque fixture COFF, senza eseguirle. Oggetto scalare accettato;
  helper SIMD, FNINIT, VZEROUPPER e helper mancante rifiutati.
- MSVC C11 `/W4 /WX`, ASan sui sorgenti C, Rust `-D warnings`;
  cppcheck sugli otto file C del servizio/test, incluso ICR, senza diagnostiche bloccanti.
- Sei suite pertinenti PASS con exit 0: `apic-icr`, `ipi-service`, `ap-native`,
  `ap-runtime`, `percpu-panic`, `layout`.
- Limiti correnti 1.000 righe e 8 file diretti rispettati.

Log finale della suite:
`kernel/build/test-run-3d1167f4a4cf4d918f5307f003d37c06/ipi-service.log`.
Fixture audit: `kernel/build/test-run-7a4aaa40cdc64565bfeb6a5b9d7ad92c/apic-icr.log`.
Il percorso `host evidence` nel log contiene oggetti, eseguibili e cppcheck.
Le altre suite e gli hash sono raccolti in `kernel/build/k4-review-manifest.json`.

## QEMU finale: livello complesso, dieci boot per profilo

QEMU 11.1.0, TCG, q35, CPU max, OVMF fornito con QEMU, ESP isolata per VM.
Il runner verifica l'hash dell'ELF copiato nell'ESP e registra anche quello
del loader. Ogni VM è posseduta dal test, con deadline e cleanup dedicati.

| Profilo | Risultato | Directory in kernel/build/ |
|---|---|---|
| `ping`, 2 CPU / 100 IPI per boot | 10/10 | `ipi-service-95a618ad414a49b1ba85bae0f660097e` |
| `ping`, 4 CPU / 300 IPI per boot | 10/10 | `ipi-service-88fb0f6bc2f343f19f1dd77b8f300f06` |
| `ping`, 8 CPU / 700 IPI per boot | 10/10 | `ipi-service-480462505f0b4792abc946e016c891d6` |
| NMI dentro handler, 2 CPU | 10/10 | `ipi-service-e3a567f071d44d958403e9e0be6cd44c` |
| NMI dentro handler, 4 CPU | 10/10 | `ipi-service-5f2b7ba867114dd78ae331a99783d04f` |
| NMI dentro handler, 8 CPU | 10/10 | `ipi-service-7819008bec0a4cd0bc337b12ebccd40c` |
| Boot ordinario, una CPU | 10/10 | `ap-runtime-1c065124d2954d40a30bb99cfb8b8d27` |
| Boot ordinario, quattro CPU | 10/10 | `ap-runtime-0b4a05aaacea41659c1c1eede97f3957` |

Immagini nuove: `kernel/build/k4-review-<profilo>/kernel.elf`, con
`<profilo>` fra `ping`, `panic-handler`, `ordinary`. Ogni immagine è usata
per tutte le configurazioni CPU applicabili riportate sopra.

Verificati: contatori ACK/ritorno/EOI, GPR distinti, x87/ST0 e XMM0–15,
DF, GS, CR3, WP/NX e prerequisiti FXSAVE, IDTR completo, GDT/TR e stack
privati. Il servizio controlla ISR prima/dopo EOI; lo spurious software
torna senza EOI. I record di startup sigillati restano invariati.
L'harness verifica accounting fisico, arena, pin e tabelle di transizione.
Nei panic intenzionali tutti i core confermano lo stop; nessun ACK mancante,
report completo e nessuna ricorsione inattesa.

Audit emergenza: **12 oggetti per immagine, 36/36** senza dipendenze FP/SIMD
rilevate nel disassemblato. Incluso `arch-x86-kernel-interrupts-apic-icr-icr.obj`.
Il rilevatore controlla opcode e operandi, anche per istruzioni senza
registri espliciti, e rifiuta output senza istruzioni riconosciute.
Il nuovo ingresso IRQ usa invece FXSAVE/FXRSTOR per contratto e non
appartiene al percorso di emergenza.

## Correzioni durante il collaudo

Il percorso di errore usa `args.smp` al posto di una variabile inesistente.
L'attesa del panic riceve lo stesso conteggio e controlla anche l'ultimo core;
il default a quattro resta per i chiamanti K3. Se la cattura diagnostica
fallisce, l'errore originale resta quello riportato dal runner.
L'audit copre ora anche l'helper ICR e riconosce FNINIT e VZEROUPPER.
Restano le regressioni per padding HMP e righe seriali incomplete.
Nessuna soglia guest è stata allentata.

La precedente campagna 70/70 e l'audit 66/66 appartengono al checkpoint
`16d16c1`, prima dei refactor K4. Restano evidenza storica in
`kernel/build/k4a-manifest.json`, non certificazione del codice successivo.

## Limiti delle prove

ASan strumenta C, non l'oggetto Rust. Clock/MMIO dei test host sono modellati;
QEMU esegue separatamente i veri accessi guest. Il modulo non alloca memoria
dinamica: i suoi dati sono residenti, gli stack e il LAPIC restano posseduti
e pinned da K3. Accounting guest e ASan non certificano tutto il kernel.
Valgrind non eseguito su Windows. Coverage linee/branch: N/D.
Profilo hardware verificato su 2/4/8 CPU QEMU xAPIC. In questo blocco i
profili `missing-ack`, `stale-ack` e `panic-idle` non sono stati rieseguiti.
Nessuna certificazione di 64 CPU, hardware fisico, x2APIC, hotplug,
task AP o sorgenti IRQ spostate dal BSP.
IDT immutabile tramite API, non nuova protezione PTE read-only.

## Prossimo incremento nello stesso modulo

Integrare il servizio nel boot ordinario con il relativo contratto di
inizializzazione/idle e profili CPU supportati; poi progettare lo shootdown
K4-B. Il solo PING non autorizza a liberare pagine usate da altri core e
non dimostra uno scheduler SMP. Vedere il [design](k4a-design.md).
