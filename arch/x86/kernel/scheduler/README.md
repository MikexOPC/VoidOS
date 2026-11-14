# Backend scheduler x86-64

Collega gli stati dello scheduler comune a stack, cambio contesto,
interrupt e idle x86. Il dispatcher seleziona il contesto, il backend
gestisce il passaggio fra stack e consegna gli eventi IRQ. Le risorse
di uno stack si rilasciano dopo il ritorno al dispatcher e la revoca
della mappatura.

**Interfaccia.** Il kernel comune fornisce le operazioni e gli stati
di scheduling. Il backend x86 associa i contesti alle task e collega
timer, switch e servizi inter-CPU. [Design](../../../../docs/design/consolidamento.md#lock-cpu-e-scheduler).

**Immagine ordinaria.** Il BSP esegue task cooperativi nella sessione
interattiva. La shell può inviare un job alla volta ad AP1.
**Profili separati.** `meson-bsp-periodic` verifica la preemption sul BSP.
`ap-task-four` la verifica su quattro AP con task kernel fidati.
[Validazione](../../../../docs/validation/consolidamento.md#cpu-e-scheduler).

**Limiti.** Otto slot di task nel core comune, driver sul BSP e
affinità fissa nelle prove AP. Preemption ordinaria su tutte le CPU
ancora aperta.
