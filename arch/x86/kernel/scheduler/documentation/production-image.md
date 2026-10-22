# Immagine di produzione senza codice di test (B6) — 19 ottobre 2026

## Problema verificato

Il kernel ordinario linkava `test/arch/x86/kernel/scheduler/assembly/preservation/probe.asm` e ne
usava `x86_scheduler_probe` a ogni switch (yield, richieste di evento, dispatch). Verifica sulla
mappa del link (`/map`, immagine identica a `kernel.exe`): `x86_scheduler_probe` presente a RVA
`0x27db0`. Gli altri file di test erano gia innocui nel binario: `fault.c` e `drawing/probe.c`
sono interamente dietro macro (nessun simbolo), `cpu-fault.obj` e il probe della finestra
entrano solo con le rispettive opzioni.

Il probe riempie GPR, XMM6-15, MXCSR e x87 con pattern, chiama `x86_scheduler_context_switch`,
verifica i pattern e fa un `fxsave64`/`fxrstor64` aggiuntivo con un frame da 600 byte. E una
verifica, non il cambio di contesto: `context.asm` salva e ripristina da solo RBX/RBP/RDI/RSI/
R12-R15 e l'intera immagine FXSAVE (Intel SDM Vol.2A FXSAVE/FXRSTOR; ABI Microsoft x64: XMM6-15
non volatili), e `x86_scheduler_task_start` lo chiamava gia direttamente.

## Cambio

- `kernel.c`: `switch_private` chiama `x86_scheduler_context_switch` e restituisce 1; con
  `KERNEL_TEST_SCHEDULER_PRESERVATION` chiama invece il probe. I controlli dopo il ritorno
  (task valido, `current`, generation, evento) restano identici.
- `kernel/scripts/build.ps1`: nuova opzione `-TestSchedulerPreservation` (macro e
  `probe.asm` solo li); `fault.c` solo con `-TestSchedulerFault guard-low|guard-high|nx|retired`;
  `drawing/probe.c` solo con `-TestFramebufferMapping`; il link scrive sempre `kernel.map`.
- `kernel/scripts/audit-symbols.ps1`: legge la mappa. Nel profilo di produzione vieta
  `x86_scheduler_probe`, `x86_scheduler_test_fault`, `x86_fb_drawing_probe` e qualsiasi oggetto
  derivato da un sorgente `test/` (nome con `-test-`); con `-Required` e `-AllowTestObjects` un
  profilo diagnostico dimostra che l'audit vede i simboli. Controprova: sulla vecchia mappa fallisce.
- `kernel.exe` ordinario: 210.944 -> 209.920 byte.

## Costo misurato (host, CPU reale, assembly reale)

`test/arch/x86/kernel/scheduler/context/cost_test.c`: 500000 andate e ritorni dispatcher -> task ->
dispatcher (2 switch) per campione, 7 campioni, mediana. Dieci esecuzioni della suite:

| | per andata e ritorno |
|---|---|
| switch grezzo (produzione) | circa 108-159 ns |
| con probe (diagnostico) | circa 243-342 ns |
| differenza | circa +135-193 ns, 1,9-2,3 volte |

La dispersione tra esecuzioni e quella di un host non isolato (altri processi); il rapporto resta
stabile. E un costo CPU su host, **non** una misura in QEMU/TCG ne su hardware reale, e non e un
requisito: nel kernel un yield avviene al massimo a ogni passo del dispatcher, quindi il risparmio
assoluto e piccolo; il beneficio principale e che la produzione non contiene ne dipende dal test.

## Prove

12 suite host (inclusa la misura), audit sulle mappe: ordinaria e mouse PASS, preservazione con
`x86_scheduler_probe` presente PASS. Build di controllo: `-TestSchedulerFault guard-low|rollback|blocked`,
`-TestFramebufferMapping`. QEMU tutti exit 0 sulle immagini finali: ordinario 10 boot, preservazione
10 boot, mouse+puntatore+shell 10 boot, stress da 200 tasti, preservazione+mouse 10 boot, idle da 240 s
(`kernel/build/b6`).

## Consolidamento del 19 ottobre

L'adattatore produzione/diagnostica dello switch e ora in
`integration/switch/bridge.h`; `integration/kernel.c` e di 398 righe.
La politica resta quella del checkpoint precedente: chiamata diretta nella build
ordinaria, probe solo con `TestSchedulerPreservation`.

Suite host backend passata: `kernel/build/consolidation-scheduler-host.log`, evidenze
`kernel/build/scheduler-context-851bcf5263a942bb841dd2828017140e`.
Build diagnostica verificata tramite MAP: 1.608 simboli, probe richiesto presente,
altri due probe vietati assenti (`kernel/build/consolidation-preservation-audit.log`).
Per questa immagine diagnostica e stato verificato build/audit; non una nuova
campagna QEMU dedicata alla preservazione.

L'immagine interattiva finale ordinaria passa dieci boot QEMU consecutivi,
confronto di 120 scene, input e cleanup; MAP con 2.117 simboli e nessun probe
vietato/oggetto test. Hash ed evidenze nello [stato corrente](../../../../../docs/status.md).
La validazione dei profili di build e separata in `kernel/scripts/build/profiles.ps1`:
6 profili validi e 11 combinazioni rifiutate verificati, build principale 358 righe.
