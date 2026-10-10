# Convivenza video/input/watchdog verificata — 10 ottobre 2026

Primo gate del [piano concordato](active-design.md), profilo isolato
TestConsoleActive. Framebuffer live prima della sessione PS/2; quattro lease
di tabelle pinned conservati durante timer, prestito dei tre stack e cleanup.
Il wrapper espone soltanto la verifica, non l'owner. Nessuna modifica ad ABI
boot, registri, Assembly IRQ o politica cache. Non e rendering sotto carico.

## Risultati host

In kernel/build/console-active-host.log:

- 576 controlli C/ASan sul gateway inventario: owner originale, stato live,
  identita degli input, pin completi1..11, rifiuto di owner copied/parziale,
  assenza video e validatore fallito senza fallback permissivo.
- 1100 controlli C/ASan della policy HPET,100 ripetizioni: esclusione
  dell'intera AperturePages video, incluse due pagine non mappate in Used;
  vicini consentiti, snapshot fallito, alias tabella/foglia e disallineamento.
  Inventario, map e letture sono mock; non e un secondo test hardware HPET.
- Regressione console:374635 CHECK,10000 riusi, flusher/retry/rientro,
  2228226 casi scalar del gateway con clock/IRQ mock, quattro test Rust per
  profilo debug/release e Clippy. Nuovo codice di ownership in C; validazioni
  paging/lease e planner Rust esistenti restano autorevoli.
- cppcheck:11 TU console/gateway piu cinque TU con macro del profilo attivo;
  nessuna diagnostica error/warning/performance/portability. Note style e
  information presenti, comprese funzioni non usate nel sottoinsieme analizzato.

Wrapper Python: quattro test del nuovo validatore in console-active-harness.log
e cinque della regressione grafica in console-active-old-harness.log. Casi
positivi, hash/conteggi errati, marker mancanti/fuori ordine, fault, budget
assente/diverso, IRQ e heartbeat mancanti. Provider mock per questi test.

Watchdog host: console-active-watchdog-host.log e
watchdog-host-3e932cc0c7414423b3f06dab9c6bc114/: otto casi Rust, C/ASan con
10000 attese riuscite e200 armamenti ritardati, compatibilita10 scenari x100,
diagnostica27 x100, recovery23 x100 e adattatore MMIO reale con backend mock.
Framebuffer host: console-active-framebuffer-host.log, regressioni lifecycle,
pixel, font/drawing, Rust/Clippy, ASan/accounting e cppcheck14 TU passano.

Corretta durante la compilazione la firma del mock x86_hpet_table_span:
bytes e uint32_t, non uint64_t. Nessun warning soppresso; suite finale ripetuta.

## Immagini finali

| Profilo in kernel/build/ | SHA256 kernel.elf |
|---|---|
| console-active-resources | 813C2EB1A81D60E7239816AB9765BB11F436A3FC97374B1E8CBE33618754700C |
| console-active-diagnostic | E584BC18BDDCE2B9D54C74D5DB6E509787FDFD798D3B73E3DE13924B694F5AFA |
| console-active-ordinary | 4450AF9B96302A4A3A287554E0D4E775D08A74AE1E10A19E8E3893BC3C17FFDA |

Loader EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7,
OVMF33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A.
Immagini costruite con MSVC/MASM supportati e warning come errori.

## QEMU:40 boot finali

Percorsi relativi a kernel/build/. Ogni campagna positiva ha dieci boot
consecutivi, KERNEL_READY e nessun fault inatteso, VM possedute terminate.

| Campagna | Evidenze |
|---|---|
| Risorse attive10/10 | ps2-input-firmware-e11790c079d243779fa2885e080b1880/ |
| Grafica10/10 | framebuffer-live-6c70d13dfa344146b2a3ffa79e218fb7/ |
| Ordinaria10/10 | framebuffer-live-b03cc1a7605344b9bb866752fbbeab73/ |
| Senza display10/10 | framebuffer-live-b0d2fbc769b9417a91eb014882ec6005/ |

Un boot smoke iniziale aggiuntivo e passato, escluso dai40 finali. Il profilo
combinato riceve in ogni boot414 byte,130 pressioni,128 rilasci,98 scalar e
10 comandi della shell seriale. Include32 pressioni aggiuntive di carico;
sequenze UTF-8 italiane e righe confrontate con quelle attese dal provider.
PS2_TASK_IRQS234..268, PS2_IDLE38..48; budget input20457 pagine prima/dopo.
Il video usa quattro tabelle: budget20461 ->20457 prima del watchdog; non
si pretende di recuperarle mentre il mapping e live. La verifica finale del
video avviene dopo collect degli stack e chiusura watchdog, prima di READY.

La regressione grafica verifica26053 pixel fisici/visibili,768 foglie video,
guardie e rollback. Il profilo combinato non disegna e non misura latenza.
Il validatore finale e stato applicato nuovamente ai dieci record combinati
dopo il rinforzo dei controlli su budget mancante e loader invariato; immagine
eseguibile invariata. console-active-results.json registra questa verifica.

## Limiti e riproduzione

Nessuna nuova allocazione nel gateway. ASan riguarda C host; Rust, Assembly
e RAM fisica non strumentati. Accounting dei lease e readback sono prove
distinte. Valgrind non disponibile su Windows; coverage linee/branch N/D.
Hardware fisico non testato. Nessun nuovo accesso hardware: fonti e bit dei
backend gia presenti sono elencati nel piano, non sostituiti da nuovi valori.
Manifest console-render-review.json: sorgenti massimo293 righe, cartelle
funzionali entro cinque sorgenti, whitespace/link/diff verificati.
reuse/ intatto, ESP ordinaria invariata; nessun push.

Da D:/OS: kernel/scripts/build.ps1 -TestConsoleActive -OutputDirectory
kernel/build/console-active-resources; poi Python con
kernel/console/video/scripts/active.py --kernel
kernel/build/console-active-resources/kernel.elf --boots 10.
Test host: kernel/console/video/scripts/test.ps1.

**Gate ancora aperto:** misura PM clock e disegno con input/watchdog attivi,
budget motivato, contatori perdite e finestre IRQ ammesse. Nessun IRQ C
arbitrario autorizzato, nessun output shell grafico o deadline dimostrata.
