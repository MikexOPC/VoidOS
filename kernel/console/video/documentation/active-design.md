# Console e sorgenti attive: piano di integrazione

Scelta utente del10 ottobre2026: un framebuffer autorizzato con lease pinned,
non un registro generico di dispositivi. Perimetro: collaudo console e sole
interfacce necessarie; nessuna modifica all'architettura IRQ o ai registri.

## Primo gate: convivenza delle risorse

Il kernel attuale avvia il framebuffer dopo timer, watchdog e sessione PS/2.
Il watchdog e il driver LAPIC verificano lo snapshot con verify_lapic;
l'API mantiene il contratto senza video. HPET owned_preflight usa la stessa
verifica prima del prestito UC. Il profilo video usa verify_video, con
autorita esplicita del mapper: le due API non sono intercambiabili.

Un profilo diagnostico TestConsoleActive, non installabile, inizializza il
mapping video prima del watchdog. Un adattatore di sola verifica riceve
context, pool, LAPIC e snapshot originali; il wrapper kernel conserva privato
l'owner video. Owner live: Ready2, Published1, lease/pin coerenti e puntatori
identici, poi verify_video completo con x86_fb_authority. Descriptor assente:
solo owner completamente vuoto e verify_lapic. Video dichiarato senza owner,
owner parziale/copied o caller diverso: rifiuto, nessun fallback permissivo.

Le sole interfacce native di timer/HPET usano l'adattatore nel nuovo profilo;
le API paging esistenti e i profili ordinari restano invariati. HPET esclude
anche tutta AperturePages video, non soltanto le foglie Used mappate.
Il mapping resta live durante apertura, prestito dei tre stack, stop input,
collect e chiusura watchdog. Nessuna allocazione video durante il prestito
del budget watchdog; il suo baseline include gia i lease video pinned.

Pseudocodice non eseguibile:

```text
UART chiusa -> bind video -> snapshot video verificato
 -> timer/watchdog -> task PS/2 nella finestra IRQ Assembly gia ammessa
 -> stop input -> collect stack -> cleanup watchdog
 -> snapshot video verificato -> KERNEL_READY
```

Accettazione: test negativi dell'adattatore C/ASan; build senza warning;
dieci boot consecutivi con immagine verificata, input italiano/shell seriale,
IRQ hardware, heartbeat HPET e cleanup riusciti mentre video resta live.
Questo gate NON misura il rendering sotto carico: non lo dichiara pronto.

## Secondo gate: disegno e latenza

La registrazione IRQ PS/2 ammette RIP soltanto nella finestra Assembly
x86_scheduler_irq_compute_begin/end della task1. La produzione C delle task
rimane IF0. Non estendere questo controllo a qualsiasi indirizzo kernel.
Disegno in un punto posseduto con IF0, finestre IRQ auditate tra le celle e
misura PM timer con clock acquisito dalla discovery sono un candidato; prima
di adottarlo servono limite temporale, carico definito e contatori perdite.
Alternativamente occorre progettare e validare un contesto C interrompibile.
Il primo gate non autorizza quest'ultima modifica automaticamente.

Fonti del contratto: [inventario video](../../../../arch/x86/mm/paging/documentation/video-resources.md),
[watchdog](../../../../arch/x86/kernel/timer/hpet/watchdog/documentation/design.md),
[sessione PS/2](../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md),
[gateway](rendering.md). Nessun nuovo accesso MMIO/I/O/CR/MSR in questo blocco.
