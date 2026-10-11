# Correzione rollover HPET — 11 ottobre 2026

Stato: **FIX VERIFICATO NEL PROFILO QEMU; REVIEW APPROVE**.
32 boot finali passati, nessun crash inatteso nelle campagne completate.
Questo report aggiorna il checkpoint [isolamento](isolation.md), senza
trasformare le campagne storiche fallite in prove positive.

## Causa e modifica

Il timer one-shot a 32 bit genera anche un evento al rollover. Il vecchio
percorso rifiutava lo status proprio a comparatore disabilitato durante il
riarmo e trattava il rollover anticipato come IRQ invalido. Il contatore
continuava inoltre a funzionare fra le attese.

La correzione mantiene il profilo esclusivo del blocco HPET, la deadline
100 ms, il limite di otto risvegli e il token del registry:

- La prova iniziale di progresso termina fermando il contatore.
- Il riarmo verifica configurazione esatta, stato globale posseduto e assenza
  di status estraneo. Ferma il contatore prima di riconoscere eventuale status
  proprio del timer inattivo; poi programma e verifica il nuovo target.
- Durante una deadline che attraversa il rollover, un solo IRQ anticipato
  e accettato se il contatore ha effettivamente attraversato lo zero.
  Lo status viene riconosciuto, ma Armed resta attivo e Count non aumenta.
- Dopo W1C si leggono status e poi contatore: un match maturato durante il
  riconoscimento completa la deadline, evitando di cancellare l'unico
  risveglio utile o rifiutarlo usando un campione temporale precedente.
- La scadenza completata ferma il contatore, disabilita il comparatore,
  riconosce lo status e infine completa EOI; solo allora Count aumenta.

Status estraneo, configurazione alterata, IRQ anticipato senza attraversamento,
rollover duplicato, errore MMIO/EOI e durata fuori dal semintervallo valido
restano errori. Non si rilasciano risorse possedute dopo una verifica fallita.
Nessuna nuova allocazione, modifica dell'ABI dell'owner o protezione rimossa.
Il flag di rollover e privato al singleton BSP e viene azzerato ad ogni arm;
non costituisce un contratto SMP.

## Fonti hardware

[Intel HPET 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf):
§2.3.5, globale 0x10 bit0 per abilitazione; §2.3.6 e §2.4.6, status 0x20
W1C per il solo timer posseduto; §2.3.8, configurazione 0x100+0x20*n,
bit2 maschera la consegna senza impedire la generazione di status;
§2.4.3.1, interrupt aggiuntivo al rollover nella modalita one-shot32.
Il contatore e a 0xf0, il comparatore a 0x108+0x20*n.

## Prove host e review

Il nuovo caso 38 fallisce prima della correzione:
`kernel/build/hpet-rollover-red.log`.
Il caso 243 dimostra la race fra lettura del tempo e dello status:
`kernel/build/hpet-rollover-read-order-red.log`; passa dopo aver invertito
le due letture successive a W1C.
Il modello ora consegna rollover e match come IRQ separati, verificando
che la prima consegna non consumi la deadline. Sono coperti anche match
durante W1C con status gia cancellato o nuovamente pendente, duplicato,
IRQ anticipato senza wrap, status estraneo, configurazione alterata,
W1C fallita e arresto fallito durante la scadenza.

Suite finale host: `kernel/build/hpet-rollover-host.log`, artefatti
`kernel/build/watchdog-host-c261c47426d542a0aeaedb576054afb2/`.
Otto test Rust e suite C ripetute 100 volte con /W4 /WX e ASan;
10.000 attese nominali piu regressioni, selezione, diagnostica, recovery
e adattatore MMIO. Accounting del mock ripristinato: 42 pagine libere.
ASan strumenta il C host; non prova assenza di leak fisici, ne strumenta
il kernel reale, Assembly o i componenti Rust compilati separatamente.
Coverage linee/branch N/D; Valgrind non disponibile nel profilo Windows.

## Gate QEMU

Immagine shell finale `kernel/build/hpet-rollover-fixed-image/kernel.elf`,
SHA256 `2E8EC6D1F457D26268A704B5C3F28970216B5FB87635274E1953A979182C6658`.
Dieci boot consecutivi del riproduttore stress200/idle-race/fallback passano:
`kernel/build/ps2-input-firmware-88a4a950a6094dbca455886d7f153137/results.json`.
Ogni boot verifica input, IRQ, UTF-8, shell, cleanup e budget fisico.

Sessione aggiuntiva stress200/idle-race/line-stress passata con 812 eventi
tastiera, 778 caratteri, 12 righe, rifiuto della riga troncata e budget 20464
prima/dopo: `kernel/build/ps2-input-firmware-5d64d3b2c1cb49f9810f3adf3ebd99dd/results.json`.
Ha completato 215 scadenze, quindi da sola non dimostra il rollover reale:
il contatore resta fermo fra le attese. Il numero totale degli IRQ vector64
include anche il timer LAPIC e non identifica da solo gli IRQ HPET.

Il nuovo collaudo `--idle-rollover` attende 80 secondi controllando che la VM
posseduta sia viva e non segnali panic. Alla chiusura richiede un avanzamento
minimo `WatchdogDeadlines * Ticks > 2^32`, senza dichiarare successo solo
in base al tempo host trascorso. Passa con 837 scadenze, avanzamento minimo
8370000000 tick e budget 20464 ripristinato:
`kernel/build/ps2-input-firmware-a2b5b8658d5246038052edb68a2a384e/results.json`.
Questo dimostra almeno un attraversamento reale del contatore a 32 bit
nella VM; non attribuisce ogni IRQ vector64 al solo HPET.

Dieci boot consecutivi con video passano sulla stessa immagine shell:
`kernel/build/ps2-input-firmware-3b321d3ddd48478b9b269fba12d0e3af/results.json`.
Per boot, 640 pixel fisici e 786432 visibili verificati, dieci comandi,
input UTF-8 e budget 20419 ripristinato. Nessun store video in IRQ.

Dieci boot ordinari consecutivi passano sulla build
`kernel/build/hpet-rollover-fixed-ordinary/kernel.elf`, SHA256
`1014E8CA2E1956EEC1F8A0014AE004005727AD7D77E779C89B94E0DEA65850CF`.
Report: `kernel/build/framebuffer-live-7a605f9a2b0b43529b7b605738e30cfa/console-grid-results.json`.
KERNEL_READY raggiunto, nessun probe console inatteso, loader invariato.

Totale finale: 10 stress/fallback, 10 video, 10 ordinari, una sessione lunga
e una idle-rollover: 32 boot. Le campagne sono sequenziali, ogni VM ha ESP e
variabili private, immagine verificata e deadline host. Lo stop riguarda
soltanto la VM posseduta. Nessun crash viene riclassificato come PASS.

Review finale: cppcheck 2.22.0 warning/performance/portability senza
diagnostiche (`kernel/build/hpet-rollover-cppcheck.xml`), diff senza errori
di whitespace, 24 sorgenti watchdog/test con massimo 293 righe, nessuna
cartella funzionale oltre cinque sorgenti; provider Python 341 righe.
Manifest: `kernel/build/hpet-rollover-source-check.json`.
Regressione console host:
`kernel/build/hpet-rollover-console-host.log` e `kernel/build/hpet-rollover-console-host/`.
Nessuna modifica a `reuse/`, LICENSE, Assembly o ABI dell'owner watchdog;
nessun commit/push eseguito in questo blocco.

La prima campagna sulla versione intermedia e stata interrotta deliberatamente
dopo la review della finestra W1C; non vale come gate finale:
`kernel/build/ps2-input-firmware-8732dad7475b41c4a12ffd612c06bb1d/interrupted.txt`.
Anche la seconda campagna intermedia e esclusa dopo la regressione 243:
`kernel/build/ps2-input-firmware-7ad26c2fe8e04790b13366debd6b6d50/interrupted.txt`.

Restano i limiti QEMU q35/TCG/OVMF, BSP, task kernel fidati, nessuna prova
su hardware fisico. Il watchdog non copre uno stallo C permanente a IF=0.

## Riproduzione

Verifica pre-commit successiva: le12 suite host passano tramite runner
centrale; regressione del runner su PowerShell5.1 e7 passata. Ripristinato
il loader BootKernel dopo la suite boot (che produce il diagnostico).
Loader rigenerato SHA256
`2D87ABD8C7CAD8D0C2214259C0FA9479ED75D8A259AFFD28A9AF1738765C2A02`:
altri10 boot consecutivi video passano con lo stesso kernel2E8EC6D1...:
`kernel/build/ps2-input-firmware-d6ec35b7215b4e639c5303e27a0541a4/results.json`.
Sono prove aggiuntive, distinte dai32 boot del fix iniziale.

Dalla radice del progetto con PowerShell 7 e toolchain supportata:

```powershell
./arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1
./kernel/scripts/build.ps1 -ConsoleShell -OutputDirectory ./kernel/build/hpet-rollover-fixed-image
python -B arch/x86/kernel/drivers/input/ps2/scripts/firmware.py --image kernel/build/hpet-rollover-fixed-image/kernel.elf --boots 10 --stress 200 --idle-race --text --shell --console-shell --video-absent
python -B arch/x86/kernel/drivers/input/ps2/scripts/firmware.py --image kernel/build/hpet-rollover-fixed-image/kernel.elf --boots 1 --idle-rollover --text --shell --console-shell --video-absent
python -B arch/x86/kernel/drivers/input/ps2/scripts/firmware.py --image kernel/build/hpet-rollover-fixed-image/kernel.elf --boots 10 --text --shell --console-shell
```

Il loader deve gia essere quello BootKernel verificato. I percorsi univoci
delle campagne cambiano a ogni esecuzione; gli hash sopra identificano le
immagini effettivamente usate in questo report.
