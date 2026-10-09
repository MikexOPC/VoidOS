# Controller PS/2 v0: preparazione e stop

Perimetro unico: driver input PS/2. Parser, discovery e protocollo sono
in kernel/drivers/input/ps2; il backend specifico x86 e in arch/x86/kernel/drivers/input/ps2.
Build, hook condizionale e test sono integrazioni necessarie del componente.
Nessuna modifica a reuse/ o ai driver esistenti.

## Profilo e fonti

Profilo scelto: QEMU q35/OVMF/BSP; il binario installato dichiara
11.1.0 (v11.1.0-12130-ge470268ff4). Il sorgente primario studiato e
[QEMU v10.1.0 pckbd.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c),
definizioni KBD_STAT/KBD_MODE/KBD_CCMD, i8042_realizefn e funzioni
kbd_write_command/kbd_write_data/kbd_read_data. Le differenze di versione
non sono nascoste: si verifica il contratto tramite round trip nel binario
installato. Non e una certificazione di implementazioni 8042 fisiche.

Discovery: [ACPI6.6 §5.2.9, Tabella5.9 e §5.2.9.3 Tabella5.11](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html#fixed-acpi-description-table-fadt):
IAPC_BOOT_ARCH u16 a offset109, bit1 per controller a porte60/64,
bit6..15 riservati zero. Prefix necessario almeno116 byte; table limit
4096 e una scelta v0. Checksum completa, span fisico e directory validati;
header/prefix riletti devono corrispondere. Più FADT sono rifiutate.
Revisioni3..6 interpretano il flag; revisioni vecchie/sconosciute o flag
assente non autorizzano port I/O. Questa validazione del prefix non
verifica ogni campo ACPI della FADT. Il clock esistente valida i propri
campi prima di accedere al PM timer.

| Porta/comando | Semantica usata | Motivo |
| --- | --- | --- |
| 0x64 read / 0x60 read | status / data, 8 bit | OBF bit0 prima di consumare |
| status bit1 | IBF | attendere zero prima di scrivere |
| status bit5 / bit6..7 | aux / errore timeout-parita | risposta aux o corrotta non e ACK |
| 0xAD / 0xA7 / 0xAE | disable keyboard / disable aux / enable keyboard | comandi solo con IBF=0 |
| 0x20 / 0x60 | read/write mode | confermare via readback |
| mode bit0..1 / bit4..5 / bit6 | IRQ / disable interfaces / translation | IRQ off, entrambe ferme, traduzione off |
| 0xAB -> 0x00 | keyboard interface test | verificare il canale |

Si conserva solo system bit2; nessun bit riservato impostato. Nessun
comando su output port, A20, CPU reset o self-test controller che possa
alterare implicitamente lo stato. Porte fisse autorizzate dal flag ACPI
e dal profilo emulato, nessun BAR/MMIO o DMA in questo componente.

Protocollo tastiera: [QEMU v10.1.0 ps2.c, ps2_write_keyboard](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c).
F5 reset/disabilita scanning; F0 02 seleziona set2; F0 00 lo interroga.
Ogni byte richiede FA; FE consente al massimo tre invii totali dello stesso
byte. Query richiede anche il valore02. Nessuna risposta di comando va
inoltrata come scancode al decoder.

## Ownership e failure

Owner C inizialmente zero. Callback e Context sono prestiti esclusivi,
non rientranti, validi fino alla chiusura; nessun heap o nuova pagina.
Backend nativo verifica il contesto CPU gia previsto dalla piattaforma
e usa la finestra ACPI esistente. Questo blocco viene chiamato soltanto
nel bootstrap con IF=0, prima di avere handler/task PS/2; non e SMP.

Stati: EMPTY -> DIRTY prima della prima operazione -> PREPARED soltanto
dopo query e readback. PREPARED e sicuro per il futuro binding, ma scanning,
interfacce e IRQ restano disabilitati. Close: DIRTY -> disable entrambe,
drain limitato, mode verificato -> CLOSED. Su failure si tenta close;
se fallisce resta DIRTY con CLEANUP_FAILED. Nessun falso successo o
ripristino degli IRQ firmware senza proprietario kernel. Close puo essere
ritentato su DIRTY; prepare su owner gia usato viene rifiutato. Per una
nuova sessione serve owner azzerato solo dopo chiusura confermata.

Deadline per attesa: 357955 tick PM, circa100ms a 3.579545MHz
([ACPI6.5 §4.8.3.3](https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html#power-management-timer-pm-tmr)).
Mask24/32bit validata, sottrazione modulare, budget sotto mezzo giro.
Limite supplementare di un milione di poll se il clock si arresta.
Drain massimo32byte. Sono limiti operativi v0, non delay universali del
dispositivo; percorsi con piu attese hanno piu deadline, non100ms totali.

Minacce: firmware/coda inattesi possono causare timeout o protocol error,
mai accessi fuori buffer. Callback port/clock sono codice kernel fidato,
non un confine userspace. Source continua/stale oltre32byte viene
rifiutata; nessun loop illimitato. Nessuna promessa di DMA isolation.

## Integrazione e lavoro restante

Il profilo isolato `-TestPs2Controller` include le prove decoder e chiama
il probe dopo ACPI discovery. Logga successo solo dopo prepare/close,
finestra idle e budget pagine invariato. Build ordinaria esclude i probe.
Prove di negoziazione non equivalgono a tasti ricevuti. Il successivo
profilo `-Ps2Input` aggiunge start/stop (F4/F5), binding IRQ1 tramite MADT,
coda e consumer; [contratto runtime](../../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md).

Test riproducibili:

```powershell
& kernel/drivers/input/ps2/scripts/test.ps1
& kernel/drivers/input/ps2/scripts/firmware.ps1 -Controller
& kernel/drivers/input/ps2/scripts/firmware.ps1 -Ordinary
```
