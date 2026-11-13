# Verifica IRQ1 → task PS/2 — 9 ottobre2026

Snapshot del collegamento precedente al layout italiano. L'immagine e i
conteggi qui sotto restano quelli di quel blocco; per il testo e la nuova
immagine vale il [report aggiornato](../../../../../../../kernel/input/keyboard/documentation/text.md).

Blocco completato nel profilo esplicito `-Ps2Input`; nessuna modifica a
`reuse/`. Contratto: [runtime.md](runtime.md). Non e certificazione hardware.

## Immagini ed evidenze

Input ELF SHA256:
`6E834F53EB8535DBA6AA7F6CD8B32DABFECC0D3B80358C9BD3F7A9AF974BFAAD`.

- Dieci boot input finali (`kernel/build/ps2-input-firmware-5b5c0a98c0d74f99a532ab5aa24489dd/results.json`):
 10/10 consecutivi q35/max/TCG,128MiB,BSP1,OVMF. Per ogni VM sei press
 (A,B,Up,Print Screen,Pause,Esc), cinque release,32byte; IRQ tastiera
 durante finestra task IF=1, idle HPET senza input, Esc e cleanup.
 TaskIrqs3..5; budget20469 pagine prima/dopo. Code/token/route/stack
 vengono verificati dal kernel prima dei marker di raccolta/cleanup.
 Trace QEMU conferma IRQ41 e nessun triple fault; nessun fault kernel inatteso.
- Stress100 tasti aggiuntivi (`kernel/build/ps2-input-firmware-cbf8b97bf15f4374a18f38840d97166b/results.json`):
 stessa immagine,1/1;106 press,104 release,330byte,297IRQ in task,
8 idle e budget20469→20469. Workload finito; non endurance o benchmark.
- Regressione ordinaria10/10 (`kernel/build/ps2-decoder-firmware-fb1851ce1e90407e8668885b7b45d4ba/results.json`):
 ELF `9C9B09AE74CDD6E2534B4895C5ADCEDF0DF643C43CA70ECC90A80AFDB60E6EE9`,
 niente probe decoder/controller o sessione input nel percorso ordinario;
 harness scheduler verifica IRQ, stack/PTE, accounting e marker finali.
- Timeout timer isolato1/1 (`kernel/build/ps2-input-irq-timeout-firmware.log`):
 conserva il significato negativo del vecchio ingresso Assembly dopo
 l'introduzione del risultato esplicito per poll vuoto. Arresto previsto
 con stack/owner trattenuti; non un crash positivo.

Il manifest delle VM registra hash ELF, bootloader, QEMU e OVMF;
serial.log, qemu.log e monitor.log sono separati per ogni processo.
Test non eseguiti su immagini precedenti spacciate per quella finale.
Manifest locale dei sorgenti e dell'ELF finale:
`kernel/build/ps2-input-source-final.json`.

## Host

Log PS/2 (`kernel/build/ps2-input-host-final.log`),
strumenti e ASan (`kernel/build/ps2-decoder-653d401abdee464e8138bd943904316b/tools.log`).

- Rust decoder11/11 debug+11/11 release, milione byte pseudocasuali
 per modalita e65.536 coppie. Clippy e ABI integer C/Rust passano.
- Controller6722 CHECK: prepare/start/stop/close, failure in ogni callback
 delle tracce, owner DIRTY e chiusura, deadline/clock fermo, ACK/RESEND.
- Discovery7989 CHECK con dipendenze ACPI reali, output rollback e bounds.
- Runtime4559 CHECK: validator MADT e encoding capacita Rust reali;
 IOAPIC/port/queue boundary MOCK. ISO reserved/alias/NMI/capacita incoerenti,
 source non posseduta, mask/unmask/restore, errori di ogni accesso acquisizione,
 selector, ISR0..33byte, status AUX/parity/timeout, LOSS, deny, quiet ed EOI.
- MSVC/W4/WX, ASan C: nessun errore osservato. Rust non strumentato ASan.
 Cppcheck12 file PS/2: zero error/warning/performance/portability;
 style/information sono nel XML, non dichiarati inesistenti.

Owner/coda Rust e gateway (`kernel/build/ps2-input-rust-scheduler-host.log`):
36 test debug+36 release;523081 check payload FFI,110016 binding FFI,
545700 ABI e matrice engine con Owner reale. Policy CPU/hardware MOCK.
Backend scheduler (`kernel/build/ps2-input-scheduler-host.log`):
suite switch reale host, guardie e dispatcher/accounting passa;
non sostituisce gli IRQ delle VM.
Watchdog (`kernel/build/ps2-input-watchdog-host.log`):
suite ASan100 ripetizioni;10000 attese evento piu10000 heartbeat vuoti
e transizione a evento, source error, fault/rollback/recovery.8 test Rust.

## Review e limiti

Nuovo runtime cinque file diretti, sorgenti sotto400 righe; integrazione
scheduler resta entro400 e cinque file. No heap nel driver/parser/ISR;
solo due lease stack16 pagine ciascuno, contabilizzati e restituiti nel
percorso QEMU positivo. ASan e accounting sono prove distinte: nessuna
affermazione globale di zero leak RAM fisica. Valgrind non eseguito;
coverage linee/branch N/D; SMP, utenti, DMA e hardware fisico non provati.

Corretti durante debug: precondizione quiet del bind prima di Active;
layout count/error a offset0/4; poll vuoto distinto da corruzione GPR/SIMD;
warning locali e guardie capacity/physical_bits. Review delle sorgenti
hardware e del diff finale eseguita. Il profilo attende con IF=1 soltanto
nella finestra Assembly e nell'idle dispatcher, come specificato nel contratto.

Restano layout/modificatori, key-state, API input per altri consumer,
recovery/restart dopo fault, sessione senza budget diagnostici, altri device
e prova su macchina dedicata. Non sono presentati come implementati.

## Riproduzione delle prove

Da PowerShell 7 nella radice, la suite host si avvia con
`./kernel/drivers/input/ps2/scripts/test.ps1`. Le suite backend e watchdog
si avviano con `./arch/x86/kernel/scheduler/scripts/test.ps1` e
`./arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1`.

Per il percorso input usare `./arch/x86/kernel/drivers/input/ps2/scripts/firmware.ps1 -Boots 10`
dopo la build del loader con `-BootKernel`. Per lo stress, il runner
`arch/x86/kernel/drivers/input/ps2/scripts/firmware.py` accetta
`--image <kernel.elf> --boots 1 --stress 100` con l'ELF PS/2.
Per la regressione del decoder usare
`./kernel/drivers/input/ps2/scripts/firmware.ps1 -Ordinary -Boots 10`.
I manifest con ID e hash sopra appartengono alle immagini del checkpoint.
