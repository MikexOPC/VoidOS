# Diagnostica del fail-stop video — 11 ottobre 2026

Checkpoint precedente alla riproduzione controllata e alla correzione del
fail-stop temporale. Per lo stato attuale vale il
[report della correzione](latency-fix.md); le prove qui sotto restano
quelle dell'immagine e del contratto precedenti al fix.

## Perimetro e stato

L'utente ha autorizzato la diagnostica e l'eventuale fix console necessario,
oltre agli adattatori PS/2, scheduler, IRQ e watchdog del blocco mouse.
Questa modifica aggiunge osservabilita; non dimostra una correzione della
causa dell'episodio precedente. Nessun limite temporale alzato, retry dopo
errore o controllo di ownership rimosso. `reuse/` non modificato.

L'episodio originale resta in
`kernel/build/ps2-input-firmware-b8394dd442e64bc19898674c2b1ff027/boot-6/`:
marker CPU `5343484641494c01`, worker a IF0, ISR vuoto, IRQ65 pendente.
L'immagine aveva SHA256
`6E12DE6DD465DF279BFFC3D56590ADDB2F755DDDB7188282FC9EFDA0E011AAEF`.
Il dump non contiene lo stato del misuratore; carico host e deadline sono
ipotesi, non cause dimostrate. I successi successivi non cancellano il guasto.

## Diagnostica aggiunta

`PS2_WORKER_FAILURE_STAGE` identifica il percorso terminale:

| Valore | Controllo fallito |
| --- | --- |
| 1 | Stato pending della console |
| 2 | Flush della console |
| 3 | Polling IRQ / verifica del contesto Assembly |
| 4 | Errore registrato dalla sessione PS/2 |
| 5 | Validazione del contesto della task |

Prima dello stop stampa anche Error e Losses. Il flush riporta
`CONSOLE_FAILURE_STAGE`: 1 task, 2 apertura del servizio, 3 chiusura del
servizio, 4 stato finale. La chiusura restituisce zero anche quando il
misuratore ha rifiutato l'operazione: leggere insieme le altre evidenze.

`CONSOLE_FAILURE_PM` contiene LastTicks del misuratore; puo essere il valore
precedente se l'operazione e respinta prima della misura. Non dedurre una
deadline superata da questo campo isolato. `CONSOLE_FAILURE_FLAGS` raccoglie
Measured.Failed nei bit0..7, Batch.Failed nei bit8..15, Render.Busy nei
bit16..23, Service.Failed nei bit24..31, Service.Busy nei bit32..39.
I contatori Cells e Rendered distinguono contabilita completata e pixel
gia confermati prima di un errore temporale. `PS2_CONSOLE_FAILED_PM` misura
il flush nel chiamante. L'errore resta terminale: nessun rollback pixel
fittizio. Il runner riconosce `PS2_INPUT_FAILED_WORKER` come errore.

Nessuna nuova istruzione privilegiata: il chiamante riusa il PM clock
validato della sessione, come nel [contratto misurato](measured-design.md).
Nessuna allocazione nuova; i log si eseguono nella task a IF0, mai nell'IRQ.

## Prove host

Suite: `kernel/console/video/scripts/test.ps1`, evidenze
`kernel/build/console-worker-host-final/` e
`kernel/build/console-worker-host-final.log`.
Compilazione MSVC warning come errori, ASan, Rust debug/release e Clippy,
cppcheck passano. Il test worker esegue 8.833 controlli: drain positivo e
cinque guasti iniettati, tutti distinguibili e terminali, WorkerDone resta
zero. Le prove del misuratore verificano deadline, clock fermo, wrap,
fallimento draw, ownership e rientro. Nessuna coverage linee/branch misurata.
Valgrind non eseguito; ASan e accounting non certificano leak su PC fisico.

## Riproduzione concorrente

Immagine diagnostica SHA256
`288E45F683E0A870C66D3B0DD8804F60A32D90F920DF392FE84D89753750F67F`.
Dieci boot video passano insieme alle campagne mouse e ordinaria, ciascuna
di dieci boot. Le durate delle tre campagne differiscono: non tutte le VM
restano contemporaneamente attive fino all'ultimo boot video.

- Video: `kernel/build/ps2-input-firmware-2c34c551c828483888c2b277ac3e4215/`.
- Mouse: `kernel/build/ps2-input-firmware-78bf9177b8644f6d8789660f9421b44c/`.
- Ordinario: `kernel/build/scheduler-firmware-fd01a8a159b146d1b50fc5f0857ddb05/`.

Un ulteriore boot video lungo, sovrapposto alla coda della campagna video,
passa: `kernel/build/ps2-input-firmware-d83005300ec54c06be258ac8aa57aede/`.
812 tasti, 2.460 byte, 12 comandi, un overflow di riga gestito,
59.151 celle / 7.774 gruppi, MaxPm216008 (circa60,35ms), budget20.419
prima/dopo; confronto pixel fisici e screenshot. Il guasto non si riproduce.

## Dieci boot finali isolati

Build finale senza warning del compilatore o Clippy, profilo `-ConsoleShell`:
`kernel/build/console-worker-final/kernel.elf`, SHA256
`D46ED41BB82F846E77CBCAF8D94E8D686CFF9DEBA648C590185ECB8073A0A1F3`.
Dieci boot consecutivi senza altre VM di test passano:
`kernel/build/ps2-input-firmware-9181011cc09e4ef7ac43ade5b45af323/`.
98 tasti per boot, testo italiano, shell, caret, pixel fisici/screenshot,
cleanup e budget20.419 prima/dopo; massimo205152 tick PM (circa57,31ms).
Il runner verifica ESP, hash immagine/loader/OVMF/QEMU e marker KERNEL_READY;
termina solo la propria VM. Le campagne precedenti restano separate per hash.
Review struttura console: 101 file esaminati, massimo sorgenti398 righe,
nessun errore; PS/2:64 sorgenti, layout/LF/include/link puliti.
`git diff --check` passa. Nessuna modifica dei sorgenti dopo questa build.

## Gate aperto alla fine di questo checkpoint

Confronto aggiuntivo, avviando insieme immagine originale, immagine finale
strumentata e mouse con200 tasti di stress per boot: entrambe le campagne
video completano dieci boot mentre il carico mouse e ancora in corso.
L'immagine originale e verificata contro lo stesso hash del guasto:

- Originale: `kernel/build/ps2-input-firmware-c9b9d8339f57470c8744222badce5286/`,
  MaxPm228481 (circa63,83ms).
- Finale: `kernel/build/ps2-input-firmware-d81d152db17e4c3c97a1d469ec475fc3/`,
  MaxPm307984 (circa86,04ms).
- Carico mouse: `kernel/build/ps2-input-firmware-7b45d0e30ced4d2cba6717132f584989/`,
  dieci boot passano, ciascuno con200 tasti di stress e35 eventi mouse;
  contatori, IRQ, cleanup e budget verificati dal runner.

Quindi venti boot dell'immagine finale passano, dieci isolati e dieci sotto
questo carico, senza dedurre che la sola diagnostica abbia corretto un bug.

Verifica della semantica del clock: nel sorgente ufficiale QEMU v11.1.0,
`acpi_pm_tmr_get_clock` deriva PM_TMR da QEMU_CLOCK_VIRTUAL;
`cpu_get_clock_locked` usa offset e get_clock mentre il clock e abilitato.
Fonti: [ACPI core](https://github.com/qemu/qemu/blob/v11.1.0/hw/acpi/core.c#L467),
[CPU timers](https://github.com/qemu/qemu/blob/v11.1.0/system/cpu-timers.c#L70),
[selezione virtual clock](https://github.com/qemu/qemu/blob/v11.1.0/util/qemu-timer.c).
Il binario installato include commit successivi al tag: questa e una
verifica del modello del riferimento, non una certificazione byte per byte
del binario. Il carico host puo quindi incidere sul delta osservato senza
dimostrare corruzione del guest: resta un'inferenza, non la diagnosi del
guasto originale. Non cambiare icount per rendere verde la stessa prova:
[QEMU documenta](https://www.qemu.org/docs/master/system/invocation.html)
che modifica il tempo virtuale e non fornisce emulazione cycle-accurate.

Non dichiarare il fail-stop risolto o il video privo di flaky. Prima di
procedere al puntatore GUI occorre una riproduzione con questi marker e
un fix della causa verificato. Se si ripresenta, conservare immagine,
seriale, trace e monitor; distinguere errore temporale da bounds/ownership,
perdita input e contesto IRQ. La prova riguarda QEMU/TCG BSP, non SMP,
preemption, hardware fisico o garanzie realtime.
