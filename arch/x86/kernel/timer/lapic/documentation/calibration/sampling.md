# Calibrazione LAPIC delimitata e tentativi finiti

Aggiornamento R-001, 2026-10-07. Ambito: modulo timer LAPIC,
interfacce/build e test necessari. Nessuna nuova route HPET o modifica al
contratto HLT. R-001 resta aperto fino al watchdog indipendente operativo.

## Problema e correzione

La versione precedente confrontava una sola coppia di campioni e leggeva
il clock PM prima del primo contatore LAPIC, ma senza una lettura PM dopo
il contatore finale. Il tempo fra osservazioni non era quantificato.
Un fallimento sotto VM concorrenti era stato registrato nel blocco HPET;
non disponiamo dei campioni falliti per attribuirne con certezza la causa.

Ora ciascuna lettura del contatore LAPIC e delimitata da letture PM:

```text
write masked initial count
PM before -> LAPIC start -> PM after
poll PM within finite iteration ceiling
PM before -> LAPIC end -> PM after
validate ordering/wrap/bracket uncertainty in Rust
stop source and verify both count registers zero
if two valid samples agree: publish samples and reload
otherwise retry within three pairs; fail without publishing
```

La policy numerica mantiene l'intervallo 35795..143180 tick PM e l'accordo
25% fra reload; aggiunge incertezza totale dei bracket <= 1/16 dell'intervallo
totale. La stima usa i punti medi, con arrotondamento intero. Questi limiti
sono scelte del profilo v0, non prescrizioni hardware o frequenze CPU fisse.
Rust verifica maschere 24/32 bit, ordine modulo del contatore, intervalli e
aritmetica prima della conversione. Dati grezzi rimangono privati fino al
consenso della coppia. Nessuna allocazione nuova o modifica all'ABI 144 byte.

Tre coppie al massimo, fino a sei campioni. Un campione numericamente
inutilizzabile puo essere ripetuto solo dopo stop e readback riusciti.
Errore I/O, timer/reference fermo con ceiling raggiunto o stop incompleto
terminano immediatamente: nessuna acquisizione o errore hardware nascosto.
Il ceiling limita istruzioni/iterazioni, non garantisce tempo wall-clock se
il processo QEMU viene sospeso dall'host. Nessuna promessa di stabilita su
tutti i carichi o su hardware fisico.

## Fonti hardware verificate prima dell'implementazione

- [Intel SDM Vol.3A, dicembre 2024, §12.5.4 e Figure 12-10/12-11](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf):
  initial count R/W a 0x380, current count RO a 0x390; scrivere zero ferma il
  timer. Il profilo possiede gia il timer mascherato one-shot e divide 16.
- [ACPI 6.5, §4.8.3.3](https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html):
  PM_TMR RO, accesso dword, clock 3.579545MHz, larghezza dalla FADT validata.

Nessuna porta nuova, bit riservato o Assembly nuovo. Il gruppo runtime C/header
e stato spostato in `runtime/`, aggiornando soltanto include e build/test
necessari: root del modulo con quattro file, runtime con due.

## Prove e review

- Host C/DLL e 9/9 test Rust: rumore transitorio, rumore persistente,
  coppia discordante, fallimento di stop senza retry, wrap, ordine invalido,
  soglia di incertezza e consenso ai limiti u32.
  Evidenza: `kernel/build/lapic-calibration-2abb4b77f07d47e5ba56204730f3a12a/`.
- ASan C host: 10 ripetizioni, 10000 cicli di eventi, parsing e cleanup.
  `kernel/build/lapic-asan-d4205d5ed81a4c48896f32e37bfbb8f2/` contiene
  tools.log, source-manifest.json e cppcheck.xml. Nessun error/warning/
  performance cppcheck; style/information conservati. C strumentato,
  Rust/Assembly non strumentati; nessuna misura di leak RAM fisica o coverage.
- Kernel finale: build warning-as-error, 10 boot consecutivi + HPET assente:
  `kernel/build/hpet-kernel-d40d9eeddfda44619aa3304a5ca884fe/`.
- Due suite QEMU concorrenti: 20 boot positivi e due HPET assenti passati:
  `kernel/build/hpet-kernel-e539991fb5cd4a6cbc5a05eafa2c7495/` e
  `kernel/build/hpet-kernel-4e55ec307f6e4f3ba82d0b12c25506c1/`.
  Un retry osservato nella prima suite, seguito da boot valido.
- `kernel/test/timer/lapic/scripts/calibration-firmware.ps1` verifica
  campioni raw/reload, owner, consegne IRQ 0x41, opcodes STI/HLT/CLI,
  contatori arrestati, token rimosso e LVT/divide ripristinati. Rifiuta 14
  alterazioni della fotografia reale. Evidenza finale aggiunta sotto.

  `kernel/build/lapic-calibration-firmware-70d92ae1709048a28c74a9e66a266340/`:
  10/10 boot consecutivi verificati dal checker LAPIC, oltre alle serie
  precedenti. Totale di questa task: 40 boot positivi, tre profili HPET
  assente, 14 alterazioni monitor rifiutate. Nessun crash inatteso nelle
  prove finali; la concorrenza host non e una prova SMP del kernel.

La review conserva il precedente fallimento sotto carico e distingue
robustezza aggiunta dalla diagnosi definitiva di quel singolo episodio.
La calibrazione non rende terminabile HLT senza IRQ: il prossimo blocco
R-001 riguarda mapping/ownership HPET, route IOAPIC, handler e timeout.
