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

Il profilo prova un payload incorporato con IF=0. Il percorso ordinario
degli IRQ da CPL3 su RSP0, le syscall e il caricamento ELF restano
blocchi distinti. [Design K5](k5-design.md).
