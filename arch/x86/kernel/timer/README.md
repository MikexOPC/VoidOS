# Timer e watchdog x86-64

Il LAPIC fornisce IRQ del timer e deadline. Il watchdog usa HPET
come sorgente indipendente per gli scenari di attesa gestiti dal
BSP. La calibrazione e le scadenze usano un clock di riferimento.
I backend espongono apertura, armamento, attesa e chiusura con
riscontro degli stati e restituzione delle risorse dopo lo stop.

**Immagine ordinaria.** Il boot calibra il timer, esercita deadline
e watchdog sul BSP e chiude il percorso timer. **Profili separati.**
Le prove di preemption periodica, IRQ LAPIC perso e fallback sono
casi dedicati. [Watchdog](../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).

**Limiti.** Le prove descritte usano QEMU/OVMF. Il timer per uno
scheduler preemptive permanente su tutte le CPU è fuori dal
percorso ordinario. [Stato](../../../../docs/status.md).
