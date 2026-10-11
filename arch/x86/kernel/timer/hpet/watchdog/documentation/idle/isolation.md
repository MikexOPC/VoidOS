# Isolamento del fallimento idle — 11 ottobre 2026

Stato del checkpoint: **CAUSA SOFTWARE ISOLATA; CORREZIONE NON IMPLEMENTATA**.
Il fix e le prove successive sono nel [report rollover](rollover.md).
Le richieste di correzione sotto descrivono il gate di questo checkpoint,
non lo stato corrente. Lo stress
riproduce lo status del timer posseduto con IRQ disabilitato vicino al wrap
one-shot32, rifiutato dal riarmo. Resta da correggere e verificare il ciclo
completo di rollover, compreso l'IRQ anticipato. Non si modifica la politica di
cleanup per far passare le ripetizioni.

## Evidenza iniziale e perimetro

Il caso conservato e
`kernel/build/ps2-input-firmware-d7e69479bbba496eb752d65d7e2770fd/boot-6/serial.log`:
durante `help`, dopo il carattere `h`, il dispatcher emette failure 2 e panic.
Il codice 2 identifica il ramo idle, non il limite di step (codice 3).
L'immagine storica non aveva il dettaglio del provider: attribuire il panic
al watchdog o a un preciso registro non sarebbe dimostrato.

Perimetro: diagnosi dell'attesa watchdog e sole interfacce di osservazione
nella sessione PS/2 e nel suo collaudo. Nessun nuovo registro, sequenza
Assembly, IRQ abilitato in C, soglia aumentata o cleanup anticipato.

## Diagnostica aggiunta

`PS2_IDLE_FAILURE_STAGE` distingue stato/retention (1), watchdog (2) e
scarico delle notifiche IRQ (3). Il nuovo `PS2_WATCHDOG_WAKE_DETAIL` distingue
il primo controllo fallito nella verifica successiva a `x86_irq_sleep`:

| Valore | Controllo fallito |
| --- | --- |
| 0 | Nessuno dei controlli di risveglio, oppure errore precedente al risveglio |
| 1 | Numero di risvegli nullo o superiore al limite esistente |
| 2 | Count del watchdog diverso dall'obiettivo |
| 3 | Error del watchdog |
| 4 | Comparatore ancora Armed |
| 5 | Page window non valida/inattiva |
| 6 | Controllo LAPIC idle fallito |

Il getter riguarda l'ultimo tentativo di wait, e viene azzerato all'ingresso;
non e una storia persistente come `x86_watchdog_failure`. Va letto insieme
alla fase principale. Ordine e short-circuit dei controlli restano uguali;
nessuna lettura MMIO aggiuntiva o modifica dell'ABI owner.

`PS2_WATCHDOG_ARM_DETAIL` distingue inoltre il punto di rifiuto nell'operazione
device2: gate owner/prestito prima della callback (1), lettura config/status
(2), Armed (3), global config (4), stop/readback (5), Start (6),
comparatore (7), enable IRQ/readback (8), counter fermo/margine (9), status
prima del riavvio (10), riavvio/readback (11), gate di revoca del prestito (12),
pending (13), config diverso (14).
Zero indica callback riuscita; il dettaglio riguarda l'ultima operazione2.
`PS2_WATCHDOG_ARM_REGISTERS` conserva config nei32bit alti e status nei32bit
bassi, dalle letture gia eseguite, senza rilettura. Start/Target/Ticks/Count
sono metadata dell'owner, non nuove letture del contatore hardware.
I punti sono metadata: accessi hardware, ordine e rollback sono conservati.
Gli injection31/32/33 e14 verificano dettagli5/11/7 e9, con suite ASan.

Il runner, in caso di eccezione, richiede stop, registri CPU, LAPIC e IRQ
nel monitor della propria VM, poi conserva l'errore originale e termina
soltanto quella VM. La raccolta e best effort: un monitor interrotto non
sostituisce l'errore originale con un falso PASS.

## Ipotesi e test deterministico

`wait.c` richiede LAPIC completamente idle dopo l'acknowledgement del watchdog.
Il test host modo 34 fa completare l'handler watchdog, Count=1, Armed=0,
Error=0 e ISR mock non in servizio; solo il controllo finale LAPIC fallisce.
L'attesa viene rifiutata con fase WAKE e dettaglio 6, trattenendo le risorse.
Dopo la rimozione della condizione mock il cleanup ripristina il budget;
una nuova attesa valida riporta dettaglio 0. Il caso gira 100 volte con ASan C.

Questo riproduce deterministicamente il percorso di rifiuto, **non** il
panic storico: il mock non legge IRR, non emula l'intera accettazione APIC
e non distingue un IRQ pendente da un mapping invalido. Il dettaglio 6
in QEMU richiedera ancora la cattura LAPIC per attribuire la causa.

L'ipotesi e un IRQ input accettato vicino alla fine dell'attesa, con IF=0
al ritorno, ma non ancora servito. Secondo
[Intel SDM dicembre 2024, Vol.3A §12.8.4 e figura 12-20](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf),
IRR contiene richieste accettate ancora da consegnare, ISR interrupt in
servizio. Table 12-1 e figura 12-20 collocano IRR a offset 0x200..0x270,
ISR a 0x100..0x170, con stride 0x10. Un IRR non vuoto non dimostra da solo
un guasto; la compatibilita con il contratto software va verificata.
Nessuno di questi accessi viene aggiunto o cambiato in questo blocco.

## Primo riscontro reale: ARM, non WAKE

Lo stress200 ha riprodotto un panic dopo192 chiamate idle e2094 step:
`kernel/build/ps2-input-firmware-85fce2062fdb4ef5a490c3100cd6e7e3/boot-1/`.
Immagine BC6CDDF..., log runner `kernel/build/idle-isolation-race.log`.
Stage idle2, watchdog failure12/ARM, wake detail0, resultFAULT.
Il fallimento precede `x86_irq_sleep`: la lettura LAPIC finale non spiega
questo caso. Monitor catturato: IF=0, ISR vuoto, IRR65/0x41 pendente, CPU
ferma nel panic. L'IRR fotografato dopo il panic non dimostra causalita.
Nessun PASS assegnato alla campagna fallita. Il caso storico puo essere
compatibile con questo percorso, ma non ne conserva la prova retroattiva.

La diagnosi prosegue sulla singola condizione ARM, usando una nuova immagine
con arm_detail. La specifica pertinente resta
[Intel HPET1.0a §§2.3.5/2.3.6/2.3.7/2.3.8/2.3.9.2.1](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf):
global config0x10 bit0, status0x20, counter0xf0, timer config0x100+0x20*n
bit2, comparatore0x108+0x20*n. Non si aggiunge alcuna operazione a tali
registri: i marcatori precedono i controlli gia presenti.

## Condizione isolata e rapporto con il rollover

Due ulteriori catture hanno ristretto il controllo: prima dettaglio3
del gruppo Armed/pending/config, poi dettaglio13/pending e registro
`0x0000213200000001`. La config0x2132 e quella posseduta: route16,
one-shot32 bit8=1, level bit1=1, interrupt enable bit2=0; status bit0=1.
Campagne fallite:

- `kernel/build/ps2-input-firmware-1d6c149a82c947f188150759ba1b9435/boot-1/`
  (immagine86BD133..., dettaglio3, idle193).
- `kernel/build/ps2-input-firmware-0b510fe6d4f948e9adbe4195a8f02f9d/boot-1/`
  (dettaglio13, config/status catturati).
- `kernel/build/ps2-input-firmware-16e0c900a3344407b8872ef700740e0e/boot-1/`
  (immagine finaleFFD4DF..., dettaglio13, owner catturato dal monitor).

Nell'ultima: Start0xff62ad04, Target0xfffb4384, Ticks0x989680=10000000,
Count196, Armed0, Error0, self originale confermato nel dump224byte.
L'ultima deadline conclusa precede il wrap0x100000000 di circa3.1ms al
periodo10ns del device. Il riarmo legge quindi status proprio pendente con
interrupt disabilitato. Non e una scadenza non riconosciuta di quella wait:
il Count era avanzato e Armed era stato rimosso.

La regola software errata e trattare ogni status pendente a IRQ disabilitato
come guasto. HPET1.0a §2.3.8 bit2 permette ancora la generazione dello status;
§2.4.3.1 richiede un evento aggiuntivo al wrap per one-shot32. Il percorso
non gestisce questa combinazione. L'istante esatto del wrap non e tracciato
nel log, ma configurazione, status e ultima deadline vicina al wrap sono
catturati; il comportamento e previsto dalla specifica.

Il test host modo35 riproduce status proprio dopo una deadline riconosciuta
con IRQ disabilitato: la successiva idle viene rifiutata con ARM13. La suite
PASS verifica questo comportamento attuale e la retention, **non** il fix.
La rimozione dello status nel mock per il cleanup e un'operazione della fixture,
non una sequenza aggiunta al driver.

Una quarta variante della diagnosi ha invece terminato sul fail-stop IRQ:
`kernel/build/ps2-input-firmware-64cf23a7c12d4587ab6225fc293e8159/boot-1/`.
Timeout del test host, IF0, HLT1, ISR64level, RAX0x4b495251554e0004.
Questa firma appartiene al fail-stop del registry; non e un normale idle
silenzioso e non e PASS. Il mock modo36 simula wrap prima della deadline,
e il callback attuale rifiuta l'IRQ come troppo presto, trattenendo handler
e mapping. La cattura reale non contiene l'owner per questa variante:
la sua attribuzione precisa al wrap resta da completare durante il fix.

Sono cinque campagne di stress fallite: quattro ARM e un fail-stop IRQ.
L'errore storico puo essere dello stesso tipo; non viene riclassificato
retroattivamente senza la diagnostica che quella immagine non aveva.

## Prove host e immagine

- Watchdog: `kernel/build/idle-isolation-watchdog-host.log`, artefatti
  `kernel/build/watchdog-host-a5be41cd6acf4ca1bd8fe186b5434597/`.
  Suite completa passata, incluso modo34, warning come errori, Rust e ASan C.
- Console/worker: `kernel/build/idle-isolation-console-host.log`, artefatti
  `kernel/build/idle-isolation-console-host/`; suite passata e cppcheck.
- Analisi wait: `kernel/build/idle-isolation-cppcheck.xml`, nessun
  error/warning/performance/portability; style unusedFunction in analisi
  isolata e information per header di sistema/checker.
- Immagine diagnostica: `kernel/build/idle-isolation-image/kernel.elf`, SHA256
  `BC6CDDF67CF99B514121AF60A7ABFD4675BB617D5EC1FB0516429FCE659D4AC3`;
  build `-ConsoleShell` riuscita, log `kernel/build/idle-isolation-build.log`.
- Riferimento precedente: 20/20 boot serial fallback senza riproduzione,
  `kernel/build/ps2-input-firmware-4bf7d3be7c7b4b8b8fe73340263bc221/results.json`.
  Questa campagna usa DEEAB100..., non la nuova immagine diagnostica.
- Immagine finale di isolamento: `kernel/build/idle-isolation-final-image/kernel.elf`,
  SHA256 `FFD4DF544E4BB5FE4D92E3740CAD780741C87E579968DA0C803E6F254B4B4D7B`.
  Build `kernel/build/idle-isolation-final-build.log`, host finale
  `kernel/build/idle-isolation-final-host.log`, artefatti
  `kernel/build/watchdog-host-753b8da4bc0147b68ea7ac3010a4e8e3/`.
  I modi34/35/36 girano100 volte insieme alle suite precedenti.
- Console finale: `kernel/build/idle-isolation-final-console-host.log`, suite
  passata con ASan C, Rust e cppcheck; analisi aggiuntiva device/wait in
  `kernel/build/idle-isolation-final-cppcheck.xml` senza diagnostica bloccante.

ASan riguarda C host, non Rust/Assembly/kernel reale. Valgrind non eseguito;
copertura linee/branch N/D. La diagnosi non alloca memoria, non libera risorse
e non cambia i requisiti di ownership.

## Riproduzione mirata

Runner: `arch/x86/kernel/drivers/input/ps2/scripts/firmware.py`.
Opzioni `--text --shell --console-shell --video-absent --stress 200 --idle-race`.
Gli intervalli fra input alternano 83/97/101/109/123 ms, attorno alla deadline
di profilo 100 ms; sono un esperimento host, non una garanzia di allineamento
temporale nella VM TCG. Hash, opzioni, contatori, budget e log sono conservati.

Per chiudere il bug servono causa confermata, un caso che fallisca prima
e passi dopo la correzione pertinente, test negativi che conservino i veri
errori e dieci boot consecutivi sulla modifica finale. Le ripetizioni verdi
non cancellano il fallimento precedente.

## Correzione richiesta e criteri di chiusura

Il prossimo blocco deve gestire sia status proprio a timer disabilitato sia
IRQ da rollover prima del match, senza consumare un IRQ come deadline riuscita
troppo presto. Deve restare terminale su status estraneo, config alterata,
duplicati inattesi, MMIO fallita e revoca incompleta. Ownership, budget,
deadline100ms e ripristino completo restano vincoli; non basta togliere
`pending` dai controlli o aumentare i limiti del dispatcher.

Gate affidabilita **REQUEST CHANGES**. Nessuna campagna QEMU della modifica
finale dichiarata positiva; dieci boot consecutivi restano obbligatori dopo
la correzione. Nessun commit/push o avanzamento verso la GUI in questo blocco.
