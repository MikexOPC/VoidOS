# Preflight del mapping — risultati del 9 ottobre 2026

Snapshot del blocco preflight. Il panic ARM e stato successivamente
corretto e verificato: [report watchdog](../../../../../../arch/x86/kernel/timer/hpet/watchdog/documentation/arm-race.md).
Lo stato di installazione/gate descritto sotto e quello precedente al fix.

## Risultato concreto

Geometria del mapping implementata in Rust senza unsafe e collegata al
kernel tramite C. Presenza video validata con MAXPHYADDR effettivo del
pool, arrotondamento a4KiB, offset interno e budget di tabelle per due
guardie. Output transazionale, assenza interamente zero, zero allocazioni.
Nessuna PTE video pubblicata e nessuna scrittura di pixel eseguita.

La scelta UC con ownership e rollback e stata approvata dall'utente.
[Design e prerequisiti](design.md). Questo blocco e il preflight;
ownership delle tabelle, pin, cache e rollback hardware sono lavoro
successivo, non proprieta gia dimostrate dal calcolo numerico.

## Host eseguiti

- Cinque nuovi casi Rust in debug e release, overflow checks attivi,
  warning come errori e Clippy pulito. Riferimento u128 indipendente per
  il rounding e conteggio iterativo delle PT:6144 combinazioni di offset
  e dimensione. Confini fisici32..52 e saturazioni esplicite.
- C con MSVC /W4 /WX e ASan:41398 controlli,10000 piani ripetuti,
  output invariato su errore, canary, alias input/output, assenza parziale
  respinta, massimo4321 pagine e11 pagine di tabelle.
- Regressioni del modulo:230214 controlli GOP,10000 sessioni e30000
  allocazioni host bilanciate nella fase ripetuta;30079 controlli
  handoff e10000 pubblicazioni. Otto casi Rust precedenti distinti.
  Esecuzioni mode duplicate dentro pixel non contate come nuovi casi.
- Cppcheck sugli otto file C del modulo/adattatore/test: nessuna
  diagnosi error/warning/performance/portability; informazioni sulle
  espansioni standard restano distinte dai risultati compilati.
- Suite host kernel completa passa; il preflight C e coperto dalla sua
  suite dedicata, non attribuito ai test di moduli diversi.
- Build kernel ELF riuscita. C /W4 /WX e Rust -D warnings; il log
  contiene avvisi ambientali "could not canonicalize path D:\\OS/D:\\",
  non diagnosticati come warning di sorgente. Non dichiarare un log
  integralmente privo di avvisi.

Evidenze:

- [Host del modulo](../../../../../build/framebuffer-mapping-host.log)
- [Host kernel](../../../../../build/framebuffer-mapping-kernel-host.log)
- [Build kernel](../../../../../build/framebuffer-mapping-build.log)
- [Analisi statica](../../../../../build/framebuffer-mapping-host/cppcheck.xml)
- [Manifest sorgenti finale](../../../../../build/framebuffer-mapping-host/final-source-review.json)

Nessuna allocazione nel preflight; ASan strumenta il C host, non Rust,
firmware o RAM fisica. Valgrind e coverage linee/branch N/D. Non si
dichiara zero leak dell'intero OS in base a questi test.

## QEMU sull'immagine corrente

q35, TCG, CPUmax,128MiB, BSP. VM possedute dal test, deadline e cleanup
limitati al loro processo. Il test ricontrolla il piano da log con un
calcolo Python indipendente, verifica v4/208, hash, marker KERNEL_READY,
registri e assenza di triple fault. Lo screenshot verifica geometria,
non rendering del kernel.

Kernel ELF SHA256:
6AF64576829E1111FD524B05856FB0F175EE0BC485AE2C747E22A643C15F391C.
Loader EFI invariato SHA256:
EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7.

Campagne finali in serie con il test aggiornato:

- [Video10/10](../../../../../build/framebuffer-firmware-fdcfc3adfc254fff8f29547e27fa888b/results.json):
  1024x768 BGR,768 pagine video e4 pagine di tabelle previste.
- [Assenza10/10](../../../../../build/framebuffer-firmware-0f0b06e3f77d4acfaff703a356bc8aaf/results.json):
  metadati e piano zero, fallback seriale fino a KERNEL_READY.

## Fallimento osservato e gate ancora aperto

Una prima coppia di campagne eseguite contemporaneamente ha dato:
video10/10, assenza3 boot passati e quarto boot in panic del timer.
[Log del fallimento](../../../../../build/framebuffer-firmware-f8c0c66a16e0419e90cb4ae264d0bd5b/boot-4/serial.log).
Il preflight framebuffer aveva gia raggiunto KERNEL_FRAMEBUFFER_PLAN_OK.
Successivamente: TIMER_DEADLINES_FAILED, failure0x0c, fase ARM,
cleanup riuscito, poi LAPIC_TIMER_INVALID. Nessun triple fault osservato.

Concorrenza delle VM e un fatto del test, non la causa radice dimostrata.
Le campagne in serie passate non correggono questo panic e non provano
stabilita sotto carico concorrente. Non alterati limiti o controlli del
watchdog per nascondere il problema.

Il test ora interrompe subito su [ FAIL ] e registra tipo, messaggio e
numero del boot fallito nel manifest; prima attendeva il timeout anche
in presenza del panic seriale. Il vecchio manifest conserva tre risultati
e failed.txt del quarto, senza riscriverlo come una prova riuscita.

Gate complessivo NON chiuso: direttive-locali §6 vieta di dichiarare pronto un
modulo con test falliti/flaky. Prima di procedere al backend live serve
chiarire/riprodurre il problema timer e poi estendere l'inventario per
tabelle possedute dal kernel e intervalli MMIO. Questi prerequisiti non
sono aggirati dal preflight. L'ELF nuovo rimane immagine di prova in
kernel/build/framebuffer-mapping-kernel/, non installato sulla ESP ordinaria.

## Review finale

Sorgenti del modulo: massimo207 righe; cinque file diretti nella radice.
Interfacce di integrazione main/build: massimo326 righe. Controlli di
dimensione, spazi, sintassi Python, link locali e git diff --check passano.
La segnalazione CRLF preesistente di kernel/test/firmware.ps1 e distinta
da un errore di diff. Nuovo codice con soli separatori; reuse/ intatto.
Il repository include lavoro precedente non committato: i totali del
diff Git non rappresentano esclusivamente questa implementazione.
