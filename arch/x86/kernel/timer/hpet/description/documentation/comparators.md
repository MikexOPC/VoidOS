# R-001: osservazione delle capacita dei comparatori HPET

Questa pagina conserva il blocco preparatorio; il successivo
[watchdog implementato](../../watchdog/documentation/validation.md) chiude R-001
nel profilo BSP dichiarato. Il probe resta una funzione in sola lettura.

Aggiornamento 2026-10-07. Il probe legge i registri dei soli timer annunciati
dal dispositivo, valida le combinazioni dei bit in Rust e segnala il primo
candidato inattivo con una capacita di routing. Nessuna scrittura MMIO,
acquisizione, route programmata o interruzione watchdog e ancora presente.

## Contratto e fonti

[Intel HPET 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf),
sezioni 2.3.5, 2.3.6 e 2.3.8: letture dword di configurazione a 0x10,
stato a 0x20, configurazione/capacita timer a 0x100 + 0x20*n e +4.
Il conteggio deriva da NUM_TIM_CAP; massimo 32 timer, ultima lettura 0x4e4
entro la pagina 4KiB verificata dal preflight.

Bit rilevanti: global legacy 1; timer interrupt enable 2, periodic 3,
periodic capability 4, size capability 5, value-set 6, 32-mode 8,
FSB enable 14, FSB capability 15; route capability nel dword superiore.
Rust rifiuta bit riservati e dipendenze impossibili. Si escludono timer
abilitati, periodici, con stato pendente o FSB abilitato; timer 0/1 sono
esclusi se legacy e impostato. Una maschera IOAPIC non dimostra GSI liberi.

## Stati e ownership

```text
borrow RO/UC/NX -> snapshot registri -> validate -> choose candidate
  -> re-read stable fields -> revoke mapping -> verify paging -> log
```

Il candidato non e una lease. Prima dell'acquisizione il futuro driver deve
rileggere lo stato, verificare l'ownership del blocco/route e acquisire le
risorse. BSP con IF=0 non protegge da firmware/SMM o produttori su altri core.
Una doppia lettura rileva differenze, non dimostra atomicita o assenza di ABA.
Se nessun candidato esiste, il probe segnala WARN senza falsificare successo
del watchdog. Identita/profilo inconsistenti provocano fallimento del probe.

Non si scrive lo stato W1C, non si resetta il contatore globale e non si
accede ai registri comparator/FSB route. Stack temporaneo senza heap; array
di 32 elementi, accessi delimitati, nessun output pubblicato prima della
revoca. O(n), massimo due letture di ciascun registro di configurazione.

## Test e limiti

`scripts/test.ps1`: sei test Rust; suite C con ASan ripetute 100 volte;
fixture UEFI in dieci VM. Il mock MMIO confronta l'intero buffer prima/dopo
per rilevare scritture. Casi: dipendenze dei bit, timer legacy/occupati/pending,
assenza route, FSB, timer 31 e bit route 31, rollback e accounting invariato.
ASan strumenta il C host, non il dispositivo o tutti gli accessi Rust.
Nessuna allocazione nuova; nessuna misura di leak RAM fisica o coverage.

`scripts/kernel.ps1` richiede candidato entro conteggio e route IOAPIC oppure
capacita FSB sul profilo positivo; conserva il caso HPET assente.

Una prima esecuzione sovrapposta alla suite QEMU delle fixture ha avuto un
fallimento LAPIC `TIMER_CALIBRATION_FAILED` al secondo boot, dopo HPET valido.
Evidenza: `kernel/build/hpet-kernel-3396b04c005740d4b37f574fe636dde8/`.
La concorrenza delle VM e un'ipotesi, non una causa dimostrata. Il risultato
non e scartato e la validazione della calibrazione sotto carico resta aperta.
Le prove seriali finali sono registrate separatamente sotto.

Evidenze sul sorgente finale:
- Host/fixture: `kernel/build/hpet-description-130b4e22f628419bb7ccfec1586b6dd2/`:
  6/6 Rust, entrambe le suite C/ASan per 100 ripetizioni, 10/10 fixture UEFI.
- Kernel seriale: `kernel/build/hpet-kernel-3feaec0f5712459da236d6c529cdea2c/`:
  build C/Rust/ASM con warning come errori, 10/10 boot positivi consecutivi
  e 1/1 HPET assente, marker KERNEL_READY e nessun fault inatteso.
- cppcheck: `kernel/build/hpet-kernel-3396b04c005740d4b37f574fe636dde8/cppcheck.xml`:
  hardware, policy e mock, nessun error/warning/performance; rilievi style
  dei mock e information sugli include standard/analizzatori conservati.
- Tutti i sorgenti del modulo <= 400 righe; massimo sorgente C 230 righe,
  cartelle <= cinque file diretti; diff whitespace verificato.

Osservato in QEMU q35/TCG/CPU max: tre comparatori, candidato 0,
route mask 0x00ff0104, FSB capability 0. Nessun valore e hardcoded nel driver.
La route richiede quindi verifica dell'IOAPIC e delle risorse effettivamente
libere; questi dati non dimostrano un interrupt HPET gia operativo.

## Prossima integrazione

R-001 resta aperto. Occorrono ownership e mapping operativo, route IOAPIC
verificata, handler, deadline one-shot, attesa HLT, stop/ack/EOI e revoca.
La chiusura richiede perdita IRQ LAPIC iniettata e risveglio HPET dimostrato.
Il probe attuale non rende terminabile un HLT privo di IRQ.
