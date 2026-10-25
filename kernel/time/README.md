# Clock comune del kernel (G4)

25 ottobre 2026. Decisioni approvate: C1-B libreria comune per calcoli e scadenze (la cattura
unica del clock arriva con G6), C2 API, C3-A migrazione a tappe, C4 sorgente sostituibile.

## API (`kernel/time/clock.h`)

- `KERNEL_CLOCK {Read, Context, Mask, Hz}`: chi chiama fornisce la lettura. Oggi è il timer ACPI
  PM (ACPI 6.5 §4.8.3.3, 3,579545 MHz, 24 o 32 bit); più avanti potrà essere un TSC calibrato o
  l'HPET senza toccare chi usa la libreria.
- `kernel_clock_now`, `kernel_clock_elapsed` (restituisce `UINT32_MAX` se il clock fallisce),
  `kernel_clock_delta` e `kernel_counter_delta` per chi ha già i campioni.
- `kernel_clock_ms`/`kernel_clock_us`: tick arrotondati per eccesso, rifiutati se non stanno sotto
  metà periodo del contatore.
- `KERNEL_DEADLINE`: `kernel_deadline_expired` vale 1 anche se il clock fallisce (fail closed).
- `KERNEL_STOPWATCH`: accumula oltre l'azzeramento del contatore. Va campionato almeno ogni metà
  periodo (~2,3 s a 24 bit); un intervallo più lungo viene segnato in `Lost`.

## Migrazione (C3-A)

| Modulo | Cosa usa ora |
|---|---|
| Present VirtIO (`iommu/present/`) | costanti da `KERNEL_PM_TIMER_HZ`, `kernel_clock_now`/`elapsed` per SLOW e scadenza, `KERNEL_STOPWATCH` per la distanza di 5 s del recupero (prima un accumulatore scritto a mano) |
| Console (`console/runtime.c`) | `kernel_counter_delta` in `elapsed()` |
| Profilo dei flush (`console/profile.c`) | `kernel_counter_delta` |
| PS/2, VT-d, LAPIC, code VirtIO | invariati: migreranno quando si toccano |

## Prove

| Prova | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test host (ASan, cppcheck): contatori a 24 e 32 bit, azzeramenti, scadenze al limite esatto, conversioni estreme, stopwatch e `Lost`, guasti del clock | PASS | `clock-host` |
| Suite host `console`, `x86-virtio`, `x86-framebuffer` | PASS | `run.ps1` |
| Smoke set | 11/11 | `qemu-run-5ae40d21625f4362ab1a619eb81cd6f1` |
| Stallo iniettato: SLOW, timeout a 1 s e recupero dopo 5 s misurati con `KERNEL_STOPWATCH` | 5/5 | `qemu-run-1b6640081d5a46da9206249e3d2f5a62` |
| Immagine di profilo (`-ProfileVgpuFlush`), fasi coerenti con `kernel_counter_delta` | 1/1 | `pointer-interactive-check-f78d7498a5554d59847b26f56b87b258` |
