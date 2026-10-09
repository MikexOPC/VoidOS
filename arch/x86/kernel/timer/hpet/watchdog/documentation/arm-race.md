# Panic ARM — correzione verificata il 9 ottobre 2026

## Problema e causa riprodotta

Il boot fallito del preflight video terminava con TIMER_DEADLINES_FAILED,
failure0x0c/ARM e LAPIC_TIMER_INVALID. Il piano video era gia riuscito.
La sequenza precedente leggeva Start mentre HPET correva, scriveva il
comparatore, abilitava l'IRQ e controllava che fosse trascorsa meno di
meta deadline. Una pausa durante la programmazione poteva provocare
un errore ARM anche con dispositivo funzionante; se il confronto veniva
superato prima della programmazione, poteva anche perdere il match.

Riproduzione deterministica: mock modo29 introduce il ritardo nella
scrittura del comparatore quando il contatore corre. Sul codice precedente
il test termina con WATCHDOG case29 failed; dopo il fix passa.
[Log precedente](../../../../../../../kernel/build/watchdog-arm-before.log).
Il log originale non contiene il delta o la singola readback fallita:
dimostra la fase ARM, non identifica da solo quale condizione sia fallita.
La race temporale e dimostrata dal test; la spiegazione del singolo
episodio originale e coerente con questa evidenza, non una misura retroattiva.

## Correzione

Il watchdog gia acquisisce esclusivamente un blocco HPET fermo, senza
altri comparatori attivi. Non viene aggiunto un HPET condiviso o usato
come clock globale del sistema. Arm ora verifica la configurazione globale,
ferma il contatore posseduto, legge Start, programma/verifica Target e
abilita il comparatore. Richiede che il counter resti uguale a Start e
che non ci siano pending. Solo allora riavvia il counter.

Se l'IRQ diventa pendente subito dopo il riavvio, viene consumato attraverso
il percorso level gia esistente, senza rifiutare un arm valido per il tempo
trascorso. Deadline100ms e semantica evento/timeout/stop conservate.
Non c'e retry illimitato, incremento della deadline hardware o counter reset.
Armed viene pubblicato prima dell'abilitazione, sul contratto BSP/IF=0.
Un rollback incompleto conserva l'owner e impedisce rilascio falso.

[Intel HPET1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf):
§2.3.5 offset0x10, ENABLE_CNFbit0 ferma/avvia il contatore;
§2.3.8 config0x100+0x20*n, interrupt enablebit2 e levelbit1;
§2.3.9.2.1 comparatore0x108+0x20*n e rischio del confronto superato;
§2.3.6/§2.4.6 status0x20, W1C del bit del timer per IRQ level.
Accessi dword, reserved bit e readback del backend esistente conservati.

## Test host e review

- MSVC /W4 /WX e ASan:100 ripetizioni della suite,10000 wait della
  sequenza ripetuta piu200 wait con ritardo di programmazione/IRQ gia
  pendente. Resta il test di counter che avanza nonostante lo stop.
- Tre nuovi failure injection: stop globale, scrittura comparatore e
  riavvio. ARM fallisce con rollback e cleanup riusciti;27 controlli
  diagnostici ripetuti100 volte. Test compatibility/recovery e adattatore
  MMIO reale in memoria host passano.
- Otto casi Rust deadline/route/recovery passano; Rust non modificato.
- Suite host kernel completa passa. Cppcheck su quattro sorgenti C
  modificati: nessun error/warning/performance/portability.
- Trenta sorgenti/script del watchdog, massimo292 righe, massimo cinque
  file diretti; diff/spazi e sintassi del nuovo test verificati.
  Avvisi ambientali di canonicalizzazione percorso D: nella build sono
  riportati distintamente dai warning di sorgente.

Evidenze:
[host](../../../../../../../kernel/build/watchdog-arm-after.log),
[host kernel](../../../../../../../kernel/build/watchdog-arm-kernel-host.log),
[cppcheck](../../../../../../../kernel/build/watchdog-arm-cppcheck.xml),
[manifest](../../../../../../../kernel/build/watchdog-arm-source-review.json).
Nessuna allocazione aggiunta; accounting dei mock e owner/risorse reali
verificati separatamente. ASan non strumenta firmware, Rust o RAM fisica.
Valgrind e coverage linee/branch N/D; hardware fisico non testato.

## QEMU finale

q35/TCG/CPUmax,128MiB, BSP, OVMF. Immagini identificate con hash.

- [Ordinario10/10](../../../../../../../kernel/build/watchdog-normal-49406f403d3440c89e637408614272c5/images.json):
  IRQ HPET/LAPIC, KERNEL_READY e owner chiuso verificato dal monitor;
  log della campagna nella stessa cartella e
  [riepilogo](../../../../../../../kernel/build/watchdog-arm-normal-firmware.log).
- [IRQ LAPIC perso10/10](../../../../../../../kernel/build/watchdog-lost-cb5496dabc0e4bc8b471b90d5ea0e777/images.json):
  timeout atteso, fase DEADLINE, wake HPET indipendente e cleanup reale,
  panic intenzionale distinto da un crash inatteso;
  [riepilogo](../../../../../../../kernel/build/watchdog-arm-lost-firmware.log).
- [Due worker contemporanei20/20](../../../../../../../kernel/build/watchdog-arm-stress-aa31b46a923f468db9fcc8db337ea5bd/results.json):
  dieci boot consecutivi con video e dieci senza; IRQ HPET consegnati3,
  risultato EVENT, failure/cleanup0 e KERNEL_READY in ogni boot.

Totale finale:30 boot positivi e10 negativi attesi. Nessun ARM panic,
triple fault o hang inatteso nelle campagne finali. Lo stress riusa il
provider di boot posseduto del framebuffer, senza modificare il driver;
snapshot/log delle singole VM e hash presenti nel manifest.

Una prima campagna stress, mentre giravano anche altre due VM di test,
ha raggiunto KERNEL_READY nel boot video4 ma QEMU non e uscito entro il
limite host5s. Non era un panic kernel. Il test condiviso ora accetta un
limite di shutdown esplicito1..30s, default5s; questo stress usa15s.
La deadline HPET resta100ms. Il manifest fallito e conservato:
[prima campagna](../../../../../../../kernel/build/watchdog-arm-stress-614ba8c00edc49fda181d1f8e817b507/results.json).
Nessun PASS attribuito a quella campagna incompleta.
Ogni VM viene terminata soltanto dal test che la possiede; bytecode
Python disabilitato per mantenere gli artefatti in build/.

## Immagine installata

Kernel ordinario verificato SHA256:
5DE9543E51EA2E9AAE802B04EBF86821E22C522668C176BEB03EF1F5BAE4D2F9.
Kernel fault injection separato SHA256:
DBE44CCBEBCCB56087FE06D5ED04E32FFE8CDA9DAD74DCA2FEF0DAC92F51F5B1.
Loader EFI invariato SHA256:
EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7.

Copiata nella ESP solo l'immagine ordinaria gia testata, hash ricontrollato.
[Installazione e backup](../../../../../../../kernel/build/watchdog-arm-install-4c42929ed04342a2aa3c4a3674e0262b/install.json).
Nessuna immagine con IRQ perso installata. Nessun commit/push in questa task.

Il blocco del preflight video dovuto al panic ARM e risolto nel profilo
verificato. Il mapping framebuffer RW/NX/UC e il disegno restano da
implementare; non vengono dichiarati implementati da questa correzione.
