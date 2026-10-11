# Ciclo kernel con timer attivo

`runtime.c` gestisce inizializzazione degli eventi, riarmo, consumo, chiusura
e ciclo continuo. `driver.c` conserva discovery, calibrazione, callback e
cleanup finale. Non sono state aggiunte allocazioni o mapping MMIO.

## Due modalita

La build ordinaria verifica tre scadenze e chiude il timer come prima.
La build con `-EventLoop` mantiene la subscription sul BSP, aspetta con
STI/HLT/CLI, consuma l'evento con IF=0 e solo allora riarma il oneshot.
Il gestore non stampa e non riarma. Ogni otto eventi il consumer stampa
il contatore; il log non misura secondi o garantisce frequenza scheduler.

```powershell
./kernel/scripts/build.ps1 -EventLoop -OutputDirectory kernel/build/event-loop
./test/kernel/timer/lapic/scripts/loop-firmware.ps1 -UseBuilt
./test/kernel/timer/lapic/scripts/loop-firmware.ps1 -UseBuilt -Ram 256 -Processors 4
```

La prova usa una ESP e una VM temporanee; non cambia la ESP ordinaria.
Per installare esplicitamente la modalita continua aggiungere `-Install`
alla compilazione. L'immagine ordinaria resta disponibile separatamente.

## Arresto e proprieta

Il controllo statico di 48 byte conserva indirizzo proprio, owner timer,
richiesta volatile di stop, stato e contatori. Non puo essere trasferito
con una copia. Tutti gli accessi kernel richiedono owner valido e IF=0.
`x86_timer_loop_request_stop()` e un'interfaccia kernel fidata per il BSP;
non e una syscall, una primitiva SMP o un comando interattivo.

Nel test, dopo almeno otto eventi QEMU viene fermato dal debugger locale.
Lo script legge e scrive soltanto i quattro byte di StopRequest, verifica
il readback e si scollega. Il kernel consuma l'eventuale scadenza in corso
prima di uscire. La richiesta e accettata solo con valore 0 o 1; un altro
valore provoca errore. Il controllo Rust rifiuta Count diverso da Consumed
e decide arresto senza overflow quando Count raggiunge UINT32_MAX.

Il cleanup ferma e maschera la sorgente, verifica ISR/IRR, rimuove il token,
chiude l'owner e ripristina LVT e divisore. Solo dopo queste verifiche
vengono stampati LOOP_CLOSED e i risultati finali. KERNEL_READY descrive
la conclusione del bootstrap dopo l'arresto, non l'entrata nel ciclo.

## Limiti verificabili

La durata del ciclo e intenzionalmente indefinita. La richiesta viene
osservata fra due scadenze: non sveglia da sola HLT. Se il timer smette
di generare IRQ, manca ancora una sorgente watchdog indipendente per
garantire un timeout. Il budget di otto risvegli non e un timeout temporale.
Errore di hardware o contatori invalidi termina il bootstrap con FAIL;
non viene dichiarato un recupero automatico. La somma dei risvegli e
controllata prima dell'addizione; saturazione di Sleeps e un errore.

Il supporto resta BSP, ring 0 e QEMU q35/TCG. Quattro CPU nella VM
verificano la topologia ma non avviano gli AP. Non ci sono ancora
scheduler, processi isolati o prova su hardware fisico.

## Riferimenti

- [Intel SDM](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf): timer APIC oneshot e arresto tramite Initial Count zero.
- [QEMU GDB](https://www.qemu.org/docs/master/system/gdb.html): debug della VM; accesso limitato a localhost nel test.
- [Protocollo GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html): richiesta di stato, lettura/scrittura memoria e detach.
