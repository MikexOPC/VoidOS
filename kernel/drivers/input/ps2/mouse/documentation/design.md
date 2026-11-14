# Mouse PS/2 — design

I documenti completi precedenti sono in `kernel/drivers/input/ps2/mouse/documentation/` al commit `49f8cef`.

Il canale AUX PS/2 condivide il controller con la tastiera.
Il runtime smista byte tramite AUX nello status e il dispatcher
segnala il consumer mouse. Il decoder di pacchetti controlla
header, segno e overflow prima di produrre movimento o pulsanti.
LOSS e pacchetto invalido fanno risincronizzare il flusso.
Lo stop drena IRQ e code prima di liberare le lease. Il renderer
riceve eventi dal consumer, mai dalla routine IRQ.

Il percorso con mouse è attivo in `ordinary` quando il controller è disponibile.
QEMU usa il dispositivo standard emulato. Le verifiche di
trasporto in host modellano I/O, IRQ e parti dello scheduler.

[Validazione del modulo](validation.md).
