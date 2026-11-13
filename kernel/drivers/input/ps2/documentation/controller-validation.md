# Verifica controller PS/2 — 9 ottobre 2026

Snapshot del blocco prepare/close. Il successivo collegamento IRQ e
documentato nel [report runtime](../../../../../arch/x86/kernel/drivers/input/ps2/documentation/validation.md).

Implementati discovery FADT, protocollo con deadline, prepare/close e
backend nativo x86. Il profilo diagnostico esercita il controller emulato
con gli IRQ disabilitati; non riceve tasti e non e il driver operativo.
Contratto e fonti: [controller.md](controller.md).

## Prove host finali

Log (`kernel/build/ps2-controller-host-final.log`),
evidenze (`kernel/build/ps2-decoder-9f014de0775a460eb70a47d3cb812860/tools.log`).

- Decoder Rust: 11/11 casi in debug e 11/11 in release, un milione di
  byte pseudocasuali e tutte le coppie di byte per modalita. Clippy pulito.
- ABI C/Rust senza puntatori: sequenze e input fuori range passano.
- Controller C di produzione: 2.815 CHECK, inclusi guardie del mock,
  rollback dopo failure in ogni callback della traccia positiva, RESEND
  fino al limite, ACK errato, risposta set1, mode readback errato, aux,
  parity/timeout status, IBF bloccato, assenza ACK, clock fermo e wrap.
- Discovery C di produzione: 7.989 CHECK; ogni lunghezza116..4096,
  checksum, flag/assenza/revisione1, tabella duplicata, mutazione durante
  lettura e failure delle quattro letture della tabella minima. Output
  preservato su errore, alias con directory rifiutato.

Toolchain MSVC x64 /W4 /WX e ASan; dipendenze C/Rust ACPI reali collegate.
Le operazioni kernel non usate dal fixture host sono trap che abortiscono
se chiamate. IO/clock/FADT sono modelli: non dimostrano il dispositivo.
ASan strumenta C, non i validatori Rust e Assembly. Nessuna allocazione
nel controller/parser; leak detector Valgrind non eseguito, coverage N/D.

Cppcheck su sette file del componente/backend: zero diagnostici
error/warning/performance/portability. Restano consigli style e
informazioni, inclusi include di sistema e funzioni usate dal kernel.
I fallimenti iniziali di link e il caso di fault su una quinta lettura
inesistente sono stati corretti; non sono riportati come PASS.

## Prova QEMU dell'immagine finale

Log controller (`kernel/build/ps2-controller-firmware-final.log`),
risultati (`kernel/build/ps2-decoder-firmware-86509b5e7fb74db6a445e975dc056b95/results.json`).
Dieci boot consecutivi q35/TCG/OVMF con `-TestPs2Controller`, ciascuno con
`KERNEL_PS2_DECODER_OK`, `KERNEL_PS2_CONTROLLER_SET2_OK`, `KERNEL_READY`
e verifiche CPU/memoria del harness scheduler. SHA256:
`1D07C1845DB5B7CCC376A0626A5804BD7B83E38DF0EC8BA77E2157D42A637609`.

Il marker controller richiede presenza ACPI, clock validato, query02,
readback del mode senza IRQ/traduzione e entrambe le interfacce ferme,
close confermato, finestra ACPI idle e numero di pagine libere invariato.
Una tastiera assente non puo produrre questo marker. Il marker non prova
IRQ1, tasti, task consumer o compatibilita su PC fisico.

La review finale ha aggiunto il controllo finestra/budget anche sul
percorso assente. La precedente sequenza di dieci boot e il primo smoke
restano evidenze intermedie; il risultato sopra e della modifica finale.

Profilo ordinario: altri dieci boot consecutivi, probe decoder/controller
esclusi dall'immagine e marker decoder assente dai log.
Log ordinario (`kernel/build/ps2-controller-ordinary-final.log`),
risultati (`kernel/build/ps2-decoder-firmware-b18b7aa95aa547cb9906e314ae9e047b/results.json`).
SHA256: `6640480DD7CEE066E1DDAE2D67C135F5CB602544C78C3E9A8EEB964B867CBBEA`.

## Review e limiti

Tutti i loop hanno un bound: table4096, directory256, drain32, tre invii
per RESEND, deadline PM con un milione di poll supplementare. Checksum e
letture sono bounds checked; nessun bit di reset/A20 o riservato scritto.
Ownership BSP/IF=0 esclusiva, callback fidate, nessuna condivisione SMP.
Cleanup non confermato conserva DIRTY e segnala errore, senza finto close.

Massimo sorgente del componente194 righe, massimo quattro file diretti
nel controller; main209 e build303 dopo i soli hook del profilo isolato.
Zero trailing whitespace/tab di indentazione/CR isolati; nessun TODO.
Hash sorgenti finali (`kernel/build/ps2-controller-source-final.json`).

Restano IRQ1/route MADT, source binding, attivazione scanning, payload,
mapping dei tasti e task consumer permanente. I negativi del controller
sono test host; fault fisici, hotplug, clock assente sulla macchina e
controller disabilitato nella configurazione QEMU non sono ancora provati.

## Riproduzione delle prove

Da PowerShell 7 nella radice, con il loader compilato in modalità
`-BootKernel`, usare `./kernel/drivers/input/ps2/scripts/test.ps1` per
la suite host e `./kernel/drivers/input/ps2/scripts/firmware.ps1 -Controller -Boots 10`
per il diagnostico. Per la regressione usare
`./kernel/drivers/input/ps2/scripts/firmware.ps1 -Ordinary -Boots 10`.
