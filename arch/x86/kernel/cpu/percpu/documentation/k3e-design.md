# K3-E — contesti AP e vincolo di durata delle risorse

29 ottobre 2026. Scelta A approvata: rollback fino al tentativo di avvio AP.
Implementazione verificata nel profilo BSP; [prove e limiti](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu).

## Stato verificato e scopo

K3-D ha predisposto arena, stack, guardie e GDT/TSS degli AP, senza
avviarli. `percpu.c` inizializza soltanto `areas[0]`; `clock/binding.c`
associa il timer PM soltanto al BSP. `resources/resources.h` non espone
ancora uno stato committed che impedisca il rilascio prima dell'avvio.
Le prove K3-D sono riportate in [validazione del checkpoint](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu).

Questo blocco prepara i contesti software degli AP e il vincolo di durata.
Non invia INIT/SIPI, non installa GS sugli AP e non li ammette ai lock.
L'accessor operativo resta BSP-only fino al progetto dell'ingresso AP.

## Ricerca e compromesso

[Linux v6.12, smpboot.c](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/smpboot.c),
`common_cpu_up`, `native_kick_ap` e `do_boot_cpu`: preparazione dei dati
per CPU e degli stack prima del risveglio hardware. Il codice distingue
preparazione, richiesta di avvio e stato raggiunto dalla CPU secondaria.
Riferimento concettuale GPL-2.0-or-later: nessuna copia di implementazione.
Il nostro BSP statico non importa hotplug, avvio parallelo o protocollo
di arresto Linux. Il commit qui proposto e la relativa politica di
rilascio sono scelte locali, non una proprietà dedotta da Linux.

Il timer resta quello ACPI PM già verificato in K3-B: non sono introdotti
registri, offset o nuove istruzioni privilegiate. Un eventuale nuovo
accesso hardware richiederà verifica della fonte prima del codice.

## Contratto approvato

- Indici AP da 1 a Count-1 del registro immutabile K3-C, massimo 63 AP.
  Ogni contesto conserva il proprio Self e indice; stato PREPARED,
  EarlyContext nullo, lock senza frame attivi e NextToken iniziale 1.
  Nessuna copia del contesto lock, token o guardie del BSP.
- Descrittore di attesa e copia numerica della sorgente PM privati per
  ciascun AP. L'hardware del timer e comune; i puntatori software non
  dipendono da oggetti temporanei o dal contesto lock del BSP.
  Policy e lettore restano quelli K3-B, senza calibrazioni nuove.
- Tutta la preparazione e eseguita dal BSP con IRQ disabilitati e lock
  rilasciati. Nessun accesso concorrente AP e ammesso in questo blocco.
  IF=0 non viene presentato come sincronizzazione SMP.
- C possiede oggetti e lifetime; Rust conserva la validazione delle identita K3-C e della geometria
  K3-D; C controlla gli indici e le transizioni dei contesti. Nessun codice Assembly aggiuntivo necessario.
- Preparazione ripetuta o parziale rifiutata; errore prima del commit
  ritira solo oggetti creati dal tentativo, senza alterare il BSP.
  Il teardown deve scollegare le attese prima di azzerarne gli oggetti.
- Prima di qualsiasi futuro tentativo di risveglio, il chiamante deve
  rendere committed le due lease K3-D e i contesti predisposti. Committed
  significa non rilasciabile; non significa AP attivo o pronto.
- Dopo il commit, unbind e rollback sono rifiutati prima di modificare
  PML4, pin, registro owner o contesti. Nessuna API decommit in questa v0:
  anche un timeout futuro non dimostra che l'AP non usi gia le risorse.
- Le lease restano complessive, come approvato in K3-D. Nessun rilascio
  individuale AP, hotplug o invalidazione remota simulata.

Sequenza concettuale, non eseguibile:

    RESOURCES_PREPARED -> CONTEXTS_PREPARED -> COMMITTED
                         |                     |
                         v                     v
                     ROLLBACK           RELEASE_FORBIDDEN

L'ingresso hardware e l'eventuale stato ACTIVE saranno un blocco separato.

## Decisione approvata

A, scelta dall'utente: preparare contesti e API di commit, mantenendo rollback
nel boot ordinario finche nessun avvio AP viene tentato. Provare il commit
irreversibile solo in un profilo diagnostico isolato. Il futuro ingresso
dovra chiamarlo prima del primo INIT/SIPI, non dopo una conferma AP.

B: effettuare gia il commit nel bootstrap ordinario attuale. Rende le
risorse non rilasciabili immediatamente, ma anticipa il punto di non ritorno
senza poter ancora avviare gli AP. Non offre CPU operative aggiuntive.

## Perimetro e prove previste

Modulo `arch/x86/kernel/cpu/percpu/`; soli adattatori necessari a contesti,
clock, lifetime delle risorse e bootstrap, piu build e test centralizzati.
Nessuna modifica a scheduler, allocator, timer hardware o vecchio OS.
Se emergono dipendenze ulteriori, discuterle prima di estendere il modulo.

Test host con toolchain supportata, warning come errori e ASan per il C:
conteggi 1/2/4/64, indici fuori limite, alias, duplicati, contesti sporchi,
failure di ogni fase, rollback e rifiuto di rilascio dopo commit. Verificare
che rifiutare il rilascio lasci invariati root, lease, pin e budget.
Rust positivo/negativo; contesto e contatori del BSP invariati.

QEMU: almeno dieci boot consecutivi dell'immagine finale positiva, prova
1 CPU e 4 CPU configurate, marker di preparazione, snapshot dei contesti
e accounting. Diagnostico commit separato dal rollback, nessuna attesa di
recuperare il budget delle lease intenzionalmente mantenute vive.
Regressioni delle interfacce interessate; prove host e guest distinte.
Nessuna dichiarazione SMP finche gli AP non eseguono codice verificato.

La stesura iniziale era solo documentale: build, ASan e QEMU erano N/A.
Per l'implementazione valgono i gate eseguibili; le prove sono separate in
[validazione del checkpoint](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu).
