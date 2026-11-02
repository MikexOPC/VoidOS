# K4-A — Evidenze e lavoro rimanente

2 novembre 2026. Diagnostico hardware K4-A verificato: tre AP ricevono
IPI fixed a IF=1, completano richieste numerate e ripristinano il contesto.
Campagna finale **70/70**: 30 boot positivi e 40 panic intenzionali previsti.
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
- Python: 5/5 regressioni sul formato registri e sulle righe seriali incomplete.
- MSVC C11 `/W4 /WX`, ASan sui sorgenti C, Rust `-D warnings`;
  cppcheck sui sette file C del servizio/test senza diagnostiche bloccanti.
- Sette suite pertinenti PASS con exit 0: `ipi-service`, `ap-native`,
  `ap-runtime`, `ap-admission`, `percpu-panic`, `ap-startup`, `layout`.
- Limiti correnti 1.000 righe e 8 file diretti rispettati; nuovo sorgente
  più lungo: runner QEMU, 192 righe.

Log finale della suite:
`kernel/build/test-run-01753d6919c84178abf10c2e0301d70c/ipi-service.log`.
Il percorso `host evidence` nel log contiene oggetti, eseguibili e cppcheck.
Le altre suite e gli hash sono raccolti in `kernel/build/k4a-manifest.json`.

## QEMU finale: livello complesso, dieci boot per profilo

QEMU 11.1.0, TCG, q35, CPU max, OVMF fornito con QEMU, ESP isolata per VM.
Il runner verifica l'hash dell'ELF copiato nell'ESP e registra anche quello
del loader. Ogni VM è posseduta dal test, con deadline e cleanup dedicati.

| Profilo | Risultato | Directory in kernel/build/ |
|---|---|---|
| `ping`, 300 IPI per boot | 10/10 | `ipi-service-aad7a92099b84f1ab1b6fcd7d3f1922b` |
| ACK mancante, canale FAILED e panic BSP | 10/10 | `ipi-service-2f73f158f927413c8d0fb9344cd44447` |
| Generazione errata, panic AP1 | 10/10 | `ipi-service-b879c624d48949d0a8d93e01df9f4b20` |
| NMI con AP in idle a IF=1/DF=1 | 10/10 | `ipi-service-9bda6c5af34f49e0902e38e363f43ee5` |
| NMI con AP1 nell'handler a IF=0/DF=0 | 10/10 | `ipi-service-18d43a666d4849b7b47f32fd25eeac09` |
| Boot ordinario, una CPU | 10/10 | `ap-runtime-030f74e9e90448c1921ba2bafaafec5d` |
| Boot ordinario, quattro CPU | 10/10 | `ap-runtime-41fa960fe1a4479594013265cc94df46` |

Le immagini sono in `kernel/build/k4a-release-<profilo>/kernel.elf`,
con `<profilo>` fra `ping`, `missing-ack`, `stale-ack`, `panic-idle`,
`panic-handler`, `ordinary`. I risultati ordinari usano la stessa immagine.

Verificati: contatori ACK/ritorno/EOI, GPR distinti, x87/ST0 e XMM0–15,
DF, GS, CR3, WP/NX e prerequisiti FXSAVE, IDTR completo, GDT/TR e stack
privati. Il servizio controlla ISR prima/dopo EOI; lo spurious software
torna senza EOI. I record di startup sigillati restano invariati.
L'harness verifica accounting fisico, arena, pin e tabelle di transizione.
Nei panic intenzionali tutti i core confermano lo stop; nessun ACK mancante,
report completo e nessuna ricorsione inattesa.

Audit emergenza: 11 oggetti per ognuna delle sei immagini, 66/66 senza
dipendenze FP/SIMD. Il linker map ordinario non contiene simboli `xis_*`
del servizio diagnostico. Il nuovo ingresso IRQ usa invece FXSAVE/FXRSTOR
per contratto e non appartiene al percorso di emergenza.

## Correzioni durante il collaudo

Il lettore locale accetta il padding HMP (`R8 =`). Un secondo difetto del
runner interpretava un prefisso ancora incompleto della riga FAIL come
un errore diverso: ora valuta soltanto righe terminate. Entrambi hanno
regressioni; dopo i fix è passata una nuova campagna completa.
Alcune prime suite host hanno scritto PASS senza terminare nel contesto
ristretto dell'agente: non sono state contate; le ripetizioni fuori da
quelle restrizioni hanno restituito exit 0. Nessuna soglia guest è stata allentata.

## Limiti delle prove

ASan strumenta C, non l'oggetto Rust. Clock/MMIO dei test host sono modellati;
QEMU esegue separatamente i veri accessi guest. Il modulo non alloca memoria
dinamica: i suoi dati sono residenti, gli stack e il LAPIC restano posseduti
e pinned da K3. Accounting guest e ASan non certificano tutto il kernel.
Valgrind non eseguito su Windows. Coverage linee/branch: N/D.
Profilo hardware limitato a quattro CPU QEMU xAPIC: nessuna certificazione
su hardware fisico, x2APIC, hotplug, task AP o sorgenti IRQ spostate dal BSP.
IDT immutabile tramite API, non nuova protezione PTE read-only.

## Prossimo incremento nello stesso modulo

Integrare il servizio nel boot ordinario con il relativo contratto di
inizializzazione/idle e profili CPU supportati; poi progettare lo shootdown
K4-B. Il solo PING non autorizza a liberare pagine usate da altri core e
non dimostra uno scheduler SMP. Vedere il [design](k4a-design.md).
