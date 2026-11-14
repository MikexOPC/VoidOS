# Scheduler — design

I documenti completi precedenti sono in `kernel/scheduler/documentation/` al commit `49f8cef`.

Il core Rust mantiene slot, generazioni, coda pronta, task corrente
e contatori di scheduling. Il profilo corrente ha otto slot.
Un handle abbina indice e generazione, così una vecchia richiesta
non può agire su uno slot riusato. Reserve lascia la task invisibile
al dispatcher fino a publish. Abort tratta una riserva non pubblicata.
Select porta READY a RUNNING, yield riaccoda, wait porta a WAITING,
exit passa a FINISHED e collect restituisce lo slot. Gli slot con
generazione esaurita sono ritirati.

Il backend x86 prepara stack con guardie e lease, conserva il frame
e torna al dispatcher prima di raccogliere. La coda e i contatori
sono protetti dal lock del core. IF=0 delimita il contesto locale,
il lock sincronizza le CPU. `scheduler.h` espone operazioni con
risposte tipizzate, più le varianti con indice CPU esplicito.
Le notifiche IRQ vengono drenate dal dispatcher. Observe e wait
usano un'epoca per chiudere la finestra del risveglio perso.
Lo stop è cooperativo nei worker che eseguono il teardown.

Nel profilo ordinario le task fidate cooperano sul BSP nella sessione della shell. L'implementazione timer periodico conserva frame,
registri e stato SIMD e torna al dispatcher quando il contesto
interrotto è quello di una task, interrompibile e senza lock
trattenuti. La preemption periodica sul BSP e sugli AP è coperta
dai relativi profili diagnostici. Il batch AP usa affinità fissa,
contesti per CPU, ACK e quiescenza esplicita prima della raccolta
degli stack. Conferma assente trattiene le risorse. La shell PS/2
può inviare un job fidato alla volta ad AP1 e riprendere sul BSP
al completamento. Driver e IRQ dei dispositivi restano sul BSP.

La capacità fissa, la mancanza di un runtime preemptive permanente
su tutte le CPU e la gestione dei processi utente restano limiti.

[Validazione del modulo](validation.md).
