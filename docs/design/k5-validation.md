# K5 — Validazione

## K5-A, primo ring 3 diagnostico

Il profilo `k5-privilege` usa QEMU 11.1.0, TCG, q35, OVMF e una
vCPU. La campagna positiva ha superato 10 boot consecutivi.

- La traccia CPU e il frame raccolto dal kernel concordano su quattro
  fault da CPL3. `CLI` dà #GP con codice 0. La lettura di testo
  supervisor dà #PF con codice 5. L'esecuzione dallo stack NX dà #PF
  con codice 21. `UD2` dopo il rientro dall'NMI dà #UD.
- Il runner osserva RIP e CPL prima di iniettare ciascun NMI. Il kernel
  verifica RIP interrotto, GS, stack IST2 e rientro nelle fasi CPL0 con
  GS kernel, CPL0 con GS simulato e CPL3. Il monitor HMP usa
  [`nmi`](https://www.qemu.org/docs/master/system/monitor).
- Prima di `KERNEL_READY` vengono verificati CR3, GDT, IDT, selettori,
  GS, finestra libera e budget fisico tornato al valore iniziale.
- La prova separata senza NMI raggiunge l'arresto previsto
  `RAX=4b354641` e `HLT=1` senza triple fault.

Il validatore Rust è stato compilato con warning come errori e
controllo overflow. I casi mutano vettore, error code, selettori, flag
e RSP. Clippy, cppcheck e la build MSVC/MASM con warning come errori
sono passati. Il C diagnostico accede a registri privilegiati. Il
rilascio delle pagine è verificato nel guest tramite accounting e
finestra, senza attribuire questo risultato a un leak detector host.

Comando da PowerShell 7:

```powershell
./scripts/test/qemu.ps1 -Campaign k5-privilege -Boots 10
```

Le evidenze sono in `kernel/build/k5-privilege-80b0d65f330c433ca14e3aa811d5326d`
e `kernel/build/k5-privilege-f997b7123917436e91b85eadb1a2b508`.
Lo SHA-256 dell'ELF è registrato nei rispettivi `results.json`.
La regressione ha superato 3 boot ordinari e 3 panic SMP attesi con
quattro vCPU. Evidenze in
`kernel/build/qemu-run-91541bc1453c4af7b8e4a86eadd01e6e`. Le suite host
`layout` e `build-plan` sono passate.

Il payload K5-A usa IF=0. Gli IRQ da CPL3 sono provati da K5-B qui sotto.
[Design K5](k5-design.md).

## K5-B, IRQ del timer da ring 3

Risultato: superato sul profilo `k5-privilege`, QEMU TCG, q35, OVMF, una
vCPU. Dieci boot consecutivi. Evidenze in
`kernel/build/k5-privilege-ee20f1ac41a24e2a80394368ea26fc13`.

In ogni boot quattro IRQ del timer LAPIC (vettore 41h) interrompono il payload:
tre riarmati sul primo ingresso, uno sul secondo.
La traccia QEMU li vede a `cpl=3`, il kernel a RIP utente con ingresso a
0x404fd8, cioè RSP0 meno il frame. Il primo riceve un NMI dentro l'handler,
con GS kernel. Il secondo convive con un NMI nel payload a IF=1. L'handler
carica uno stato FPU/SIMD pulito e il payload ritrova GPR, 128 bit di XMM0 e
XMM15, MXCSR e FCW. Dopo il
ripristino il boot arriva a `KERNEL_READY`.

| Negativo, 3 boot ciascuno | Esito atteso | Stato |
| --- | --- | --- |
| `k5-irq-simd`, SIMD non ripristinato | panic | 3/3 |
| `k5-irq-gpr`, GPR alterato | panic | 3/3 |
| `k5-irq-rsp0`, RSP0 errato | arresto `4b354641` al primo IRQ | 3/3 |
| `k5-irq-timeout`, nessun timer | panic, nessun IRQ da CPL3 | 3/3 |
| `k5-irq-cpl0`, IRQ a CPL0 | arresto `4b354641` | 3/3 |

Evidenze in `kernel/build/qemu-run-f56bebe180be4e5a81119e6a47691fbf`.
Il caso senza NMI passa 1/1. Regressioni sulla modifica finale: `ap-runtime`
e `ap-runtime-panic` 3/3 (`kernel/build/qemu-run-299a55cf479a4de599833fc568f5f8c6`).
`ap-runtime-panic` aveva già passato 10/10 dopo la correzione del runner.
Suite host `layout` e `build-plan` eseguite prima dei punti SIMD e riarmo,
che non toccano build né layout. Passati 2 test Rust, clippy con warning come
errori e cppcheck 2.22 su `probe.c` (warning, performance, portability).

`ap-runtime-panic` era fallita in un boot su 3 per una lettura del log
durante la scrittura di una riga, attribuita al marker troncato. La VM era
sana. Il runner ora ammette i prefissi di `KERNEL_SMP_PANIC`.

Fonti verificate sul PDF 253668-090 di Vol. 3A: §§7.14.2, 7.14.4, 10.2.3, 10.7,
13.5.4, 13.8.4, 13.8.5. RF=1 nel frame IRQ è un comportamento osservato su QEMU
e ammesso dal validatore, non un requisito tratto dall'SDM. Le citazioni di
Vol. 1 e Vol. 2 (MXCSR, FCW, FXSAVE, SWAPGS) non sono state ricontrollate.

Limiti. Una vCPU, TCG, nessun hardware reale, nessuna preemption, nessun IRQ su
AP. XMM controllati: XMM0 e XMM15, non tutti i sedici.

## K5-B2, IRQ da ring 3 su AP

Risultato: superato sul profilo `k5-ap`, QEMU TCG, q35, OVMF, tre vCPU. Dieci
boot consecutivi. Evidenze in `kernel/build/qemu-run-2ea0d95a42c74d02b8dc039197d3c80e`.

AP1 e AP2 eseguono la prova uno alla volta. La traccia vede otto IRQ D1h a
`cpl=3`. Il log mostra GS diversi e TSS distanti uno slot. Ogni AP riceve due
NMI inviati dal BSP con l'ICR, risponde a un `PING` dopo la prova e il BSP
ritrova il budget fisico.

| Negativo, 3 boot ciascuno | Esito atteso |
| --- | --- |
| `k5-ap-gs`, contesto di un'altra CPU | l'AP rifiuta, panic `FAI` del BSP |
| `k5-ap-tss`, TSS di un'altra CPU | l'AP rifiuta, panic `FAI` del BSP |
| `k5-ap-nmi`, nessun NMI | l'AP si ferma, panic per deadline |
| `k5-ap-shootdown`, shootdown durante la prova | panic per ack mancante |

Il runner controlla il codice di abort di ciascun caso.

Regressioni sul codice finale: `k5-privilege` 10/10, negativi BSP, `ap-timer`,
`ap-runtime` e `ap-runtime-panic` 3/3 ciascuno, suite host `layout` e
`build-plan`, cppcheck su `probe.c` nel profilo AP.

Limiti. Ring 3 su un AP alla volta, non simultaneo. Il coordinamento fra BSP e AP
usa letture `volatile` di campi a singolo scrittore, adatte al solo x86 di questa
prova. L'attesa di 100 ms prima dell'NMI a CPL3 è temporale, non un handshake:
il payload non può scrivere in memoria kernel. Nessun hardware reale. La prova
non attesta quale vCPU abbia eseguito ogni riga della traccia QEMU: l'identità
viene da GS, indice e TSS letti sull'AP.

## K5-C, prima syscall

Risultato: superato sul profilo `k5-syscall`, QEMU TCG, q35, OVMF, una vCPU. Dieci
boot consecutivi. Evidenze in `kernel/build/k5-privilege-b6bc6fa95d71458e906fd85c07838835`.

In ogni boot il payload esegue due volte tre chiamate: `ECHO`, `CPU` e un numero
sconosciuto, che restituisce -1. Ritrova GPR, 128 bit di XMM0, MXCSR e FCW. Un NMI
arriva nell'ingresso prima di `SWAPGS` (CPL0, GS e RSP utente), uno nel payload.
Dopo le chiamate un IRQ del timer interrompe il ring 3. Con SCE spento la stessa
istruzione dà #UD. Poi il boot arriva a `KERNEL_READY`.

| Negativo, 3 boot ciascuno | Esito atteso |
| --- | --- |
| `k5-sys-rip`, `k5-sys-kernel-rip`, `k5-sys-cs`, `k5-sys-iopl`: frame alterato | arresto senza ritorno a ring 3 |
| `k5-sys-rsp`, RSP kernel al momento della chiamata | arresto senza fault |
| `k5-sys-swapgs`, `SWAPGS` mancante | arresto prima di usare GS |
| `k5-sys-tf`, TF attivo | nessun #DB a CPL0, #DB a CPL3, arresto |
| `k5-sys-nmi`, nessun NMI nella finestra | arresto |
| `k5-sys-register`, registro non conservato | il payload lo rileva, panic |

Evidenze in `kernel/build/qemu-run-780e84b30e21437c8eb14c1d90a1dc62`. Regressioni
in `kernel/build/qemu-run-0a40facf82794c6ba50a0f551c106a35`: `k5-privilege`, `k5-ap`,
`ap-runtime` e `ap-runtime-panic` 3/3 ciascuno. Passati i 3 test Rust, clippy sul
validatore, cppcheck su `syscall.c` e `probe.c`, suite host `layout` e `build-plan`.

Limiti. Solo BSP e una vCPU, RSP utente in una variabile del probe, IF=0 in tutta
la syscall, nessun puntatore utente. `SYSRETQ` non è provato. Il frame alterato è
prodotto dal kernel di prova: il codice utente non può ottenerlo direttamente.
Gli indirizzi di STAR, LSTAR e CSTAR non sono ricontrollati su Vol. 4.

## K5-D, copie utente con SMEP e SMAP

Risultato: superato sul profilo `k5-copy`, QEMU TCG, q35, OVMF, una vCPU. Dieci boot
consecutivi. Evidenze in `kernel/build/k5-privilege-33562d880e954931a6ebe9ee3c1b4210`.

Il payload copia dentro una pagina, a cavallo di due, con lunghezza zero e con 256
byte. Riceve -2 per pagina assente, sola lettura, attraversamento verso pagina assente
o in sola lettura, RSP0, indirizzo kernel, non canonico e wrap; -3 per 257 byte. La
traccia QEMU mostra quattro #PF a `cpl=0` con error code 0, 3, 0, 3, tutti recuperati,
e un NMI con AC=1. Il boot prosegue fino a `KERNEL_READY`.

| Negativo, 3 boot ciascuno | Error code #PF | Esito |
| --- | --- | --- |
| `k5-copy-no-stac`, SMAP senza `STAC` | 3 | arresto |
| `k5-copy-smep`, kernel esegue da pagina utente | 11h | arresto |
| `k5-copy-cr2`, CR2 fuori dall'intervallo | 0 | arresto |
| `k5-copy-rip`, copia fuori dalla finestra con AC attivo e CR2 valido | 0 | arresto |
| `k5-copy-kernel-buffer`, fault sul buffer kernel | 2 | arresto |
| `k5-copy-ac-open`, AC attivo al rientro in C | nessuno | arresto |

Evidenze in `kernel/build/qemu-run-73f4dadd7197486f9eb6b73c2afbc7a2`, e
`kernel/build/qemu-run-085da30597624a7e9cb9930b6319ca08` per `k5-copy-no-stac`.

Il runner verifica il RIP e il CR2 dei fault di copia. Nel negativo RIP
controlla che il fault arrivi dall'istruzione esterna alla finestra e
che CR2 sia l'indirizzo utente assente richiesto dalla copia.

## Chiusura di K5

Sul codice finale: `k5-privilege`, `k5-ap`, `k5-syscall` e `k5-copy` 10/10
(`kernel/build/qemu-run-0e7bd9138e074ee7821e8f62d4d050f9` e il risultato sopra). Tutti i
negativi K5 3/3 (`kernel/build/qemu-run-42133718e0b2494aae457982ea501178`,
`kernel/build/qemu-run-0174e445d39d49c18c9125e50437bc05`,
`kernel/build/qemu-run-1145b529882c42f1902b9a8e35929451`). `ap-runtime` e
`ap-runtime-panic` 3/3. Suite host `layout` e `build-plan`, test Rust, clippy e cppcheck
passati. Il caso senza NMI non è stato rieseguito dopo K5-B.

Stato: K5 prova le primitive in profili diagnostici separati. Il kernel ordinario resta a
`SCE=0`, senza SMEP e SMAP.

Limiti per K6. Solo BSP per syscall e copie, una CPU alla volta su AP. CPU senza SMAP
non provata. RSP utente e buffer sono variabili del probe. Nessun demand paging né copia
concorrente. KPTI e mitigazioni non decisi: nessun codice non fidato prima di quella
scelta. Tutto su QEMU TCG. Gli indirizzi di STAR, LSTAR e CSTAR non sono ricontrollati
su Vol. 4.

### Verifica delle correzioni di review

Il BSP controlla il rilascio delle pagine AP e arresta il diagnostico
anche se il timer resta fermo per un milione di letture consecutive.
Questi due errori non sono stati iniettati.

`k5-copy`, `k5-ap`, `k5-privilege` e `k5-syscall` 10/10 ciascuno,
`k5-copy-rip` 3/3 e 7 test Rust per ciascun livello di ottimizzazione
(0 e 2). Log in `kernel/build/qemu-run-d262834e23094509a8a67d1077e54fd6`
e `kernel/build/qemu-run-53d6c6b9ec6644788086f40c29fa302d`.
