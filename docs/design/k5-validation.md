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
`kernel/build/qemu-run-e5574bb60f2348fa8999ee7fa570f2ee`.

In ogni boot due IRQ del timer LAPIC (vettore 41h) interrompono il payload.
La traccia QEMU li vede a `cpl=3`, il kernel a RIP utente con ingresso a
0x404fd8, cioè RSP0 meno il frame. Il primo riceve un NMI dentro l'handler,
con GS kernel. Il secondo convive con un NMI nel payload a IF=1. L'handler
azzera XMM0-1 e il payload ritrova GPR e XMM con i valori noti. Dopo il
ripristino il boot arriva a `KERNEL_READY`.

| Negativo, 3 boot ciascuno | Esito atteso | Stato |
| --- | --- | --- |
| `k5-irq-simd`, SIMD non ripristinato | panic | 3/3 |
| `k5-irq-gpr`, GPR alterato | panic | 3/3 |
| `k5-irq-rsp0`, RSP0 errato | panic | 3/3 |
| `k5-irq-timeout`, nessun timer | panic, nessun IRQ da CPL3 | 3/3 |
| `k5-irq-cpl0`, IRQ a CPL0 | arresto `4b354641` | 3/3 |

Evidenze in `kernel/build/qemu-run-63078009502b4928ab565aaa06637ca0`.
Il caso senza NMI passa 1/1. Regressioni: `ap-runtime` 3/3,
`ap-runtime-panic` 10/10 dopo la correzione del runner, suite host `layout`
e `build-plan`, 2 test Rust del validatore.

`ap-runtime-panic` era fallita in un boot su 3 per una lettura del log
durante la scrittura di una riga, attribuita al marker troncato. La VM era
sana. Il runner ora ammette i prefissi di `KERNEL_SMP_PANIC`.

Limiti. Una vCPU, TCG, nessun hardware reale, nessuna preemption. Non provati:
clippy e cppcheck su K5-B, IRQ su AP. Il confronto SIMD legge i 64 bit bassi
di XMM0 e XMM1. I numeri di sezione dell'SDM rev. 090 derivano dallo
scostamento di un capitolo rispetto alla rev. 078, non da una lettura del PDF.
