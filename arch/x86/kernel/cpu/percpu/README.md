# Contesto per CPU e avvio degli AP

Mantiene l'identità logica e il contesto di lock per CPU, raggiungibile
via GS. Il BSP prepara e sigilla risorse private e tabelle di
transizione prima di avviare gli AP. Il servizio inter-CPU gestisce
PING e richieste di invalidazione TLB, con ACK prima del riuso
delle risorse. Il panic coordinato dispone di un trasporto NMI.

**Interfaccia.** I lettori ottengono il contesto corrente con
`x86_percpu_current_local` o `x86_percpu_current_context`. Le operazioni
di startup e servizio possiedono le lease fino ai relativi punti di
rilascio. [Design](../../../../../docs/design/consolidamento.md#lock-cpu-e-scheduler).

**Immagine ordinaria.** Nel boot QEMU a quattro vCPU il BSP avvia tre
AP, riceve tre ACK e lascia gli AP parcheggiati. **Profili separati.**
La preemption dei task fidati sugli AP e il panic provocato su più
core sono provati nelle campagne dedicate.
[Validazione](../../../../../docs/validation/consolidamento.md#cpu-e-scheduler).

**Limiti.** Capacità fissa di 64 CPU, percorso operativo xAPIC.
Hotplug e avvio su PC fisico restano non verificati. Gli AP
parcheggiati dell'ordinaria servono IPI durante il boot.
