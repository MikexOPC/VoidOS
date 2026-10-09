# Shell PS/2 v0 — progetto, 9 ottobre 2026

Scelta utente: task shell separata con coda. Modulo input, profilo Ps2Input:
task0 decoder/editor, task1 finestra IRQ verificata, task2 shell. Lo scheduler
gia supporta8 task; il profilo input ne pubblica3. L'integrazione verifica
tutti e tre gli stack durante l'attesa. L'accounting watchdog accetta ora
una terza lease distinta, pinned e da16 pagine: gate esatto48 pagine;
la API precedente e il gate32 pagine restano per i profili a due task.
Nessuna modifica alla sequenza hardware, IRQ entry o Assembly.

## Ricerca, ownership e decisione

[xv6-riscv sh.c](https://github.com/mit-pdos/xv6-riscv/blob/riscv/user/sh.c),
branch riscv consultato il9 ottobre, distingue parsing ed esecuzione di
processi. Qui mancano userspace/processi/filesystem: niente copia del codice,
fork, exec, pipe, redirezioni o sintassi POSIX. Builtin case-sensitive:
help, echo con testo letterale, status del profilo input, exit con cleanup.
Rust riconosce il comando attraverso interi; C possiede buffer e azioni.

Coda FIFO4 righe, copie da massimo256 byte, nessun heap o puntatore ceduto
dall'editor. Rust safe valida head/tail/count/close e decide gli slot tramite
ABI intera. C copia prima di pubblicare, svuota lo slot prima di liberarlo.
Produttore0 e consumer2, BSP a IF=0; IRQ accede solo alla coda byte precedente.
Non e sincronizzazione SMP. Due eventi generazionali gia disponibili:
5 dati/EOF,6 spazio libero. Observe/recheck/wait evita risvegli persi.

Schema NON eseguibile:

```text
Enter -> riga posseduta dal producer -> tentativo copia FIFO
 FULL -> observe spazio/recheck -> wait -> riprova (riga conservata)
 copia pubblicata -> signal dati -> clear editor
shell -> observe dati/recheck -> pop in copia locale -> signal spazio
 -> builtin limitato -> clear copia locale -> yield
Esc/exit -> stop hardware -> producer chiude FIFO/signal EOF
 -> shell scarta eventuali pendenti -> termina -> tre stack raccolti
```

Overflow editor rende la riga tainted fino al prossimo Enter: la shell la
rifiuta interamente, anche se Backspace ha creato spazio. Nessun comando
troncato eseguito. EOF e lifetime distinti da coda temporaneamente vuota.
Exit non spegne il PC: richiede la chiusura della sessione e KERNEL_READY.
Errori di output/stato conservano owner e causano fail-stop.

## Minacce e accettazione

Input dispositivo: copia limitata, nessun format string/eval, command token
lungo o ignoto rifiutato; echo letterale UTF-8. Spazi ASCII iniziali ammessi,
argomenti solo per echo; Tab rimane evento editor. Riga vuota non esegue nulla.
Coda piena applica backpressure; un burst puo comunque saturare la coda IRQ,
il cui LOSS conserva la politica terminale precedente. Nessuna attesa senza
yield/wait e guardie dispatcher/HPET. La shell resta kernel fidata, senza
user copy, processi isolati o promessa di servizio illimitato.

Gate: FIFO/close/bounds/rollback e parser Rust, C/ASan e output failure mock;
build warning come errori, analisi statica;10 boot finali con comandi reali,
UTF-8, unknown/syntax/overflow, exit e raccolta di tutte e tre le task;
regressione ordinaria e stress definiti. Risultati eseguiti sotto.

## Prove finali

Input ELF SHA256:
`566B0C31D5B498EAC56B540D6EE8D0D1372537D79BEA0C013DC3B2AB3DD99F0A`.

- [Host input/shell](../../build/ps2-decoder-a5aca9c631fd43b58900eb44dde5a155/tools.log):
  24 test Rust sia debug sia release (20 precedenti +2 command +2 queue).
  Stati della FIFO e operazioni esplorati esaustivamente,10000 cicli di wrap,
  full/empty/close, token mutati e argomenti dei builtin. Rust/Clippy con
  warning come errori. C/Rust shell220038 CHECK: ordine FIFO, full, EOF,
  bounds, canary, echo UTF-8/251 byte e output failure senza mutare il comando.
  Test bridge aggiuntivo: coda piena→wait→pop→retry, producer conserva riga,
  cancellazione su stop e consumer separato fino a EOF; scheduler di questo
  test mock. Test C con W4/WX e ASan; Rust safe non strumentato ASan.
  Suite decoder/testo/editor/controller/discovery/runtime ancora verdi.
  Cppcheck20 file senza error/warning/performance/portability; note
  style/information non sono dichiarate assenti.
- [Backend e accounting](../../build/scheduler-context-0ff34f7c44e14b6aa91f2e206aa3639b/tools.log):
  suite backend completa passata. Test prestiti:290002 CHECK per esecuzione,
  ripetuto10 volte,10000 cicli sia a due sia a tre lease; allocator/CPU mock.
  Copia di identita, pin terzo mancante, budget alterato, rollback e fine
  prestito. ASan C; cppcheck24 file. Nessuna modifica a registri o Assembly.
- [Dieci boot shell](../../build/ps2-input-firmware-a2847258df784308a8a193f6ae6db181/results.json):
  10/10 consecutivi q35/TCG/OVMF/BSP, immagine ESP verificata. Per boot98
  press,66 caratteri e10 righe gestite dalla task2. Help/status, argomenti
  errati, comando ignoto, echo vuoto, `echo ciao è €` e `echo exit` letterale.
  Exit richiede stop e cleanup, con task input precedentemente in wait.
  Tre task completate dal profilo, FIFO closed-empty, tutti gli stack
  raccolti, budget20467 pagine prima/dopo, KERNEL_READY. Trace controllato
  per IRQ in task e fault/triple fault inattesi: nessuno rilevato.
- [Stress](../../build/ps2-input-firmware-2ac704c00bb74be78409ea74df0a9746/results.json):
  stessa immagine,612 press/611 release,578 caratteri,12 righe/12 consegne,
  un overflow editor rifiutato dalla shell,1628 IRQ in task e52 idle.
  Due righe da256 byte, incluso accento attraverso il confine dei blocchi
  di output; UTF-8 e righe confrontati esattamente. Cleanup/budget ripristinati.
  Il test FULL della FIFO e il relativo wait sono host/mock: il firmware
  stress non e dichiarato prova di saturazione della FIFO fra task.
- [Regressione ordinaria](../../build/ps2-decoder-firmware-a88e432c4cb642f99ad20b4d0a9bcecc/results.json):
  10/10 boot dell'immagine distinta
  `A0C87C2863BB19428A1C263E71BDD25319BA7512876843ED117E78D34E467B0D`,
  compresi i profili a due stack con accounting32 pagine.
- [Manifest sorgenti](../../build/ps2-shell-source-final.json): input,
  script di build e adapter scheduler/accounting. Massimo397 righe
  nell'integrazione scheduler; cartelle funzionali input massimo5 file.
  Nessun trailing whitespace/CR isolato, diff e link locali verificati.

## Problemi trovati durante il ciclo

1. La terza task veniva rifiutata dal gate watchdog32 pagine. Aggiunto
   prestito opzionale con terza lease distinta pinned16 e gate48, senza
   cambiare Budget o ignorare pin/ownership. API a due lease preservata.
2. L'idle scheduler verificava soltanto due stack/budget32. Ora controlla
   count del profilo, bounds2..8, tutte le lease e budget16*count. Il resto
   dello scheduler non viene dichiarato preemptive o SMP.
3. Il mock logger provocava una diagnostica statica sul suo loop di scansione:
   mock riscritto con lunghezza nota e copia limitata. Logger kernel invariato.
4. Harness contava come obbligatorio il break finale di Enter dopo exit.
   Come Esc, lo stop puo disabilitare scanning prima di quel break; minimo
   richiesto=press−2 (Pause non ha break). Tutti i caratteri/righe/risposte
   vengono comunque confrontati; nessun timeout o fault e accettato come PASS.

Le immagini delle prime prove fallite non sostituiscono i10 boot finali.
La correzione dell'accounting e un prerequisito del terzo stack, non una
riduzione delle verifiche per far passare il nuovo profilo.

## Limiti e riproduzione

Shell kernel di bring-up, nel profilo esplicito Ps2Input, con log seriale
diagnostico. Non e una console grafica o un terminale con redraw della riga.
Status mostra uno snapshot dei contatori input fornito dal producer/consumer;
ShellCommands conta anche righe vuote/ignote/rifiutate ricevute, non soltanto
azioni riuscite. Pendenti dopo stop vengono scartati. Nessuna shell POSIX,
filesystem, processo esterno, ring3, syscall, SMP o test hardware fisico.

Coda e buffer fissi, nessun heap aggiunto. ASan host/accounting sessione non
dimostrano zero leak globali della RAM fisica. Valgrind e coverage linee/branch
N/D. Failure output testate host/mock, non iniettate nel device firmware.
Le guardie di dispatch/idle e il watchdog rimangono: sessione non illimitata.

Host: `kernel/drivers/input/ps2/scripts/test.ps1`.
Backend: `arch/x86/kernel/scheduler/scripts/test.ps1`.
Build: `kernel/scripts/build.ps1 -Ps2Input`.
Dieci boot: `arch/x86/kernel/drivers/input/ps2/scripts/firmware.ps1 -Boots 10`.
Stress: firmware.py con `--image <kernel.elf> --boots 1 --text --line-stress --shell`.
Nessun comando di questa shell modifica il PC host o spegne hardware reale.
