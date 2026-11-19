# Timer e watchdog x86-64

Il LAPIC fornisce IRQ del timer e deadline. Il watchdog usa HPET
come sorgente indipendente per gli scenari di attesa gestiti dal
BSP. La calibrazione e le scadenze usano un clock di riferimento.
I backend espongono apertura, armamento, attesa e chiusura con
riscontro degli stati e restituzione delle risorse dopo lo stop.

**Immagine ordinaria.** Il boot calibra il timer. Il runtime BSP mantiene
timer LAPIC periodico e watchdog HPET durante la sessione PS/2 e dopo
il suo rilascio, anche senza controller PS/2. **Profili separati.**
Le prove AP, IRQ LAPIC perso e fallback sono
casi dedicati. [Watchdog](../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).

**Limiti.** Le prove descritte usano QEMU/OVMF. Il timer per uno
scheduler preemptive ordinario sugli AP richiede ancora integrazione.
[Stato](../../../../docs/status.md).
