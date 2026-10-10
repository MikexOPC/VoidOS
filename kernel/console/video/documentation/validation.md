# Verifica del nucleo console video — 10 ottobre 2026

Modulo kernel/console/video/, scelta griglia RAM confermata dall'utente.
[Contratto e ricerca](design.md). Implementati cursore, LF/CR, Backspace,
wrap differito, scorrimento, dirty peek/ack e clear. Non e ancora output
grafico della shell: il gateway IRQ/video e il collegamento restano successivi.

## Host e review

Log kernel/build/console-grid-host.log:4 test Rust in debug e4 in release,
Clippy con warning come errori; geometrie/stati validi e invalidi, tutti gli
scalar Unicode fino a110000 esadecimale, wrap/scroll e cancellazione.
Rust safe riceve solo interi, nessun puntatore o helper runtime nel link.
C MSVC x64 W4/WX con ASan:374635 CHECK, canary, copie dell'owner respinte,
errori senza mutazione, ack incoerente/duplicato, dirty bounds,1x1 e240x67,
scroll controllato cella per cella e10000 cicli di riuso. sizeof66360.
Zero nuove allocazioni; riserva fissa esplicita, non prova di leak globali.

Cppcheck2.22.0:3 TU, nessun error/warning/performance/portability. Note
style variableScope e unusedFunction del probe analizzato senza il caller;
information per header di sistema/checkers. Non sono assenza di ogni nota
statica. Review manuale: indice validato prima degli array, shift dirty0..7,
copy scroll crescente da indirizzi successivi senza sovrascrivere sorgenti,
clear/reset bounded; dipendenze senza include circolari. Chiamante kernel
fidato, buffer di output peek validi e separati dall'owner; nessuna user copy.

Regressione framebuffer: kernel/build/console-grid-framebuffer-host.log,
16 test Rust per profilo, Clippy,7 gruppi lifecycle x100,3 pixel e4 drawing
x100 C/ASan/accounting, cppcheck14 TU senza diagnostiche bloccanti.
ASan strumenta il C host, non Rust/RAM fisica. Valgrind Windows e coverage
linee/branch N/D. Non dichiarare sicurezza hardware/SMP da queste prove.

## Immagini e boot finali

Diagnostico: kernel/build/console-grid-diagnostic/kernel.elf, SHA256
268697DFA1EE96F2296A5FDFF2C02EFF6DA1790BCCC9A59893994845328C9585.
Ordinario: kernel/build/console-grid-ordinary/kernel.elf, SHA256
C800F503570CC5C1B4A7C131FAA22BA1F9F8CF6C4770124EB0CC031963D10B33.
Entrambe le build finali passano warning come errori con la toolchain supportata.
Il nucleo/probe sono compilati soltanto con TestFramebufferMapping.
Nessuna modifica a sorgenti IRQ, registri, cache policy o mapper.

- Diagnostico10/10 consecutivi:
  kernel/build/framebuffer-live-d500ac4a2cd3485fa6bd18545353d224/console-grid-results.json.
  Ogni boot:100 riusi3x2, accenti/euro/Backspace/scroll/invalid scalar,
  dirty consumati una volta e griglia240x67 piena piu wrap/scroll verificato.
  KERNEL_CONSOLE_GRID_OK e KERNEL_READY, nessun fault inatteso.
  Regredisce anche unbind/budget/rebind,768 foglie video e guardie,
  CR3/PAT,16837 pixel fisici e nel PPM. Il raster non mostra la nuova griglia:
  e il precedente probe font, verificato senza cambiamenti.
- Ordinario10/10 consecutivi:
  kernel/build/framebuffer-live-73240fe264674752ad1c710f26e4b2ba/console-grid-results.json.
  Mapping live e KERNEL_READY, nessun marker/probe console e nessun fault.
- Senza display10/10 finali sulla stessa immagine ordinaria:
  kernel/build/framebuffer-live-c25b132c5f0d4fa98bbbb1749988d91a/console-grid-results.json.
  Descrittore azzerato, nessun mapping/probe console, KERNEL_READY.

Una campagna precedente senza display non e PASS: nove boot completi,
decimo arrivato a KERNEL_READY ma timeout host5 secondi nella chiusura QEMU.
Evidenza kernel/build/framebuffer-live-4f6dd0e5452548f7b75417f960c4467d/results.json.
Monitor aveva ricevuto quit; finally ha terminato soltanto la VM posseduta.
Non e un panic del kernel e non dimostra la causa interna dello stallo host.
Esposto il timeout host gia supportato dal provider: wrapper console15 secondi
(range1..30), durata della chiusura registrata. Deadline boot40 secondi,
assenza fault, hash e controllo exit non modificati; timeout resta FAIL.
Dieci boot finali senza display: shutdown0.078..0.094 secondi. Questa prova
non dimostra che lo stallo QEMU non possa ripetersi su un host sotto carico.
Quattro test Python del wrapper verificano FAIL su provider fallito, hash/
conteggio/loader incoerenti, marker errato e deadline invalida; subprocess
mock, nessuna VM. Log kernel/build/console-grid-harness-host.log.
Il wrapper non interrompe il provider con un secondo timeout che potrebbe
lasciare una VM orfana: lifetime/deadline/finally restano al provider proprietario.

I provider verificano hash di immagine/ESP/OVMF/QEMU e terminano soltanto
la VM posseduta; il wrapper console richiede/esclude il marker specifico
in ciascuno dei dieci log. Hash loader invariato:
EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7.
Nessuna installazione nella ESP ordinaria in questo blocco.

## Problemi corretti nel ciclo

1. no_std incluso come modulo nei test produceva unused-attributes con
   D warnings: cfg_attr(not(test), no_std), senza esclusioni dei warning.
2. /TC sul comando link interpretava l'oggetto Rust come C: separata
   compilazione /c dal link, mantenendo W4/WX e ASan.
3. Gli add Rust con overflow-checks=yes richiedevano un helper core panic
   esterno. Add saturating espliciti entro limiti240/67 gia validati;
   nessun overflow possibile nel dominio, stessi test e checks mantenuti,
   niente runtime Windows aggiunto. Le prove finali usano questa versione.

## Riproduzione e prossimo gate

Da D:/OS: kernel/console/video/scripts/test.ps1; kernel/scripts/build.ps1
-TestFramebufferMapping -OutputDirectory <diagnostico>; build ordinaria
con altro OutputDirectory. Python scripts/firmware.py --kernel <ELF>
--boots 10 --diagnostic per diagnostico, senza flag per ordinario,
--absent per ordinario senza display.
Manifest finale kernel/build/console-grid-review.json, righe/cartelle,
whitespace/link e hash. Sorgenti del modulo massimo150 righe.

Prima del prossimo blocco: gateway owner framebuffer senza esporre il
puntatore, restore IF sia su successo sia su errore,128 pixel per cella,
misura finestra completa di verifica/store, progresso IRQ e timeout,
budget redraw e owner console unico. Nessuna preemption, AP o GUI.
