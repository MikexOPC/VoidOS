# Checkpoint: LAPIC dedicato durante la vita del driver

Il driver COM1 mantiene una pagina LAPIC dedicata durante i tre IRQ,
invece di rimappare la finestra RAM per ogni EOI. Setup e chiusura della
rotta I/O APIC rimangono con IF=0; alla chiusura vengono revocati PTE e
owner LAPIC. E un mapping persistente per la vita della prova, non un
servizio ioremap generale o un mapping trattenuto a KERNEL_READY.

## Implementazione e revisione

- Slot PT 288, virtuale 0x0000700000120000, guardie 287/289, fuori da
  finestra RAM, stack e arena. Nessuna nuova tabella o pagina RAM.
- Owner statico non trasferibile di 40 byte. Bind vergine, preflight
  APIC e disgiunzione dai metadati prima di qualsiasi pubblicazione.
- Encoding Rust: limiti fisici CPUID, allineamento e flag cache; UC
  scelto dal PAT validato, supervisor RW NX senza user/global/huge.
- IF=0, radice e genitori verificati; INVLPG e readback dopo bind/revoca.
  Nessuna pagina fisica del dispositivo viene azzerata o liberata.
- Rollback dello slot appena acquisito, rifiuto di mapping estranei,
  output alias e offset fuori ISR/IRR. Revoca solo con ISR/IRR vuoti
  dopo che il chiamante ha chiuso la sorgente.
- Handler indipendente da Window.Active e dalle sue PTE: prova host
  con finestra RAM simulata occupata, completamento ACK/EOI riuscito.
- Nella prima integrazione dello script host, un argomento oggetto era
  stato aggiunto anche alla compilazione Rust; corretto prima dei test finali.

## Evidenza

- Suite host kernel passata, con bind/unbind/doppio bind, slot e guardie
  occupati, rollback, owner/PTE corrotti, limiti fisici, alias e offset;
  regressione delle cause UART e guasti nelle letture ISR/EOI.
- 45 test Rust complessivi, inclusi due nuovi per bounds e flag PTE.
- Immagine valida e sette mutazioni invalide verificate.
- 23/23 scenari QEMU: kernel/build/firmware-be93f1a9d3984d228eaf15afb37584dd.
  Tre IRQ hardware 0x40 in ogni scenario; monitor indipendente di owner
  azzerato, PTE revocata e due guardie vuote. Regressioni NMI, double
  fault, NX e stack mantenute. Contatore errato e PTE user vengono
  rifiutati anche dal verificatore della campagna su copie dei log.
- SHA256 ELF: 0A6A9CF18C7666CCA53BDB013073CCF240B0C71462E079222B0320D8FB03904C.
  ELF 102400 byte, segmenti residenti arrotondati a pagina 114688 byte:
  nessuna crescita di pagine residenti rispetto al checkpoint COM1.

Le modifiche utente a diagnostici, header ACPI e spaziatura di main.c
sono preservate fuori dal commit. L'hash e della working tree testata.
Hardware fisico non testato; BSP operativo, niente nesting o shootdown SMP.

## Prossimo blocco

Prima di trattenere il mapping dopo il ritorno del driver, aggiornare
l'inventario per distinguere foglie RAM e MMIO con ownership e cache
verificate. Durante questa prova lo snapshot precedente non viene usato
come inventario corrente; torna valido dopo la revoca. Poi si potra
estendere COM1 a TX/RX con buffer bounded e trasferimenti reali.

[Contratto e fonte Intel](../../mm/devices/lapic.md).
