# R-001 — progetto dell'attesa terminabile

Stato: opzione B scelta dall'utente, watchdog indipendente e HLT. Primo blocco implementato separatamente in `arch/x86/kernel/timer/hpet/description/`: decoder della tabella HPET con validazione numerica Rust. Il wait LAPIC non e ancora modificato. Ambito: watchdog del timer e sue interfacce/test; non scheduler o SMP.

## Problema verificato nel sorgente

runtime.c, funzione step, e driver.c, funzione deadlines, attendono Count con x86_irq_sleep. Il budget otto conta risvegli, non tempo. Senza IRQ, il chiamante non riottiene controllo per controllare clock, stop o errore. La lettura PM in record_deadline avviene dopo l'attesa, quindi non la rende terminabile.

Il modulo dispone gia di reference ACPI validata, maschera 24/32 bit, lettura System IO, calibrazione e calcolo Rust del delta. Un register readback LAPIC non dimostra che un IRQ futuro verra consegnato.

## Opzioni

| Opzione | Perimetro | Vantaggio | Costo/limite |
|---|---|---|---|
| A: v0, attesa attiva bounded | Timer LAPIC con reference gia presente | Controllo della deadline anche senza IRQ; nessun nuovo device | Consuma CPU durante attesa; nel ciclo continuo puo occupare un core |
| B: watchdog indipendente con HLT | Seconda sorgente timer/IRQ, routing, lifecycle e integrazione | CPU puo dormire e risvegliarsi sulla deadline indipendente | Nuovo blocco hardware e piu failure/cleanup da verificare |

A e una mitigazione temporanea per il profilo iniziale, non un watchdog hardware o una soluzione efficiente definitiva. B e preferibile per un servizio persistente, ma non va realizzata fingendo che due eventi dello stesso LAPIC siano sorgenti indipendenti. L'assenza di HPET nel firmware deve produrre un profilo non supportato o un fallback scelto esplicitamente.

## Design A proposto

1. Entrata BSP/ring 0 con IF=0 e owner/subscription validi; nessuna allocazione nel percorso wait.
2. Snapshot PM, arm del LAPIC e limite iniziale proposto 100 ms, da verificare rispetto al periodo calibrato e al profilo QEMU; non e un tempo universale per altri device.
3. Finestra Assembly breve per consegna IRQ, con PAUSE e rientro IF=0. Nessuna chiamata C a IF=1 e nessun HLT.
4. Leggere risultato IRQ, Error, StopRequest e delta PM; decisione numerica in Rust. Gestire wrap 24/32 bit ed intervalli ambigui; eventi tardivi non diventano successi senza verifica.
5. Ceiling finito di iterazioni come escape se anche il reference clock si arresta. Non e una misura di millisecondi e non offre un limite wall-clock indipendente dalla CPU.
6. Su timeout/stop: mascherare e fermare source, verificare quiescenza, rimuovere subscription e ripristinare configurazione posseduta. Una IRR/ISR non vuota impedisce di dichiarare cleanup completo o rilasciare l'owner.
7. Conservare errori/device fault distinti da stop richiesto e da evento valido. Se cleanup fallisce: diagnostica e arresto controllato, senza falso OK o ritorno a caller con handler dangling.

Pseudocodice di intenti, non codice eseguibile:

```text
validate owner and entry state
sample reference; arm source
repeat within finite attempt ceiling:
    permit IRQ in bounded Assembly window; return with IF=0
    inspect delivered event, error, request and reference delta
    classify result using checked numeric policy
    if event valid: consume once and return EVENT
    if timeout/stop/fault: quiesce owned source and return distinct result
return REFERENCE_FAULT after controlled quiescence
```

L'ABI corrente che chiama le iterazioni Sleeps o SleepWakes non deve presentare polling come sleep. Aggiornare diagnostica/checker compatibili con questa scelta, conservando layout solo se motivato. Nessuna garanzia sulle API sleep generiche fuori dal timer: rimangono utilizzabili soltanto con un wake contract dichiarato.

## Design B scelto: blocchi di implementazione

HPET one-shot come candidato, scoperto dal firmware e verificato contro capabilities. Serve ownership di mapping e comparator, vettore/routing ammessi dal registro IRQ, acknowledgement, deadline armata abbastanza avanti e verifica del cleanup di entrambe le sorgenti. Non reset globale del counter senza possederlo; non disabilitare device di altri owner. Verificare prima disponibilita delle interfacce: non sono state lette/modificate implementazioni di altri moduli in questa task.

Prima di implementare B, produrre tabella register/bit/sequenza dal datasheet e suddividere il nuovo blocco in task testabili. Un watchdog IRQ non promette progresso se anche il controller/routing o entrambe le sorgenti falliscono; dichiarare il modello di failure supportato.

1. Descrizione HPET: decodifica della tabella copiata in memoria posseduta, checksum e profilo numerico; nessun accesso MMIO. Implementata e testata nel modulo HPET description.
2. Discovery e mapping: lookup nella directory ACPI, ownership della regione MMIO e mapping supervisor con cache appropriata; confrontare l'ID dichiarato con quello letto dal device.
3. Comparator e IRQ: scegliere una route supportata dalle capabilities effettive, possedere comparator/token, armare deadline e verificare stop/acknowledgement.
4. Integrazione HLT: timeout distinto dall'evento LAPIC e da stop; injection della perdita IRQ LAPIC e dieci boot del kernel finale. Solo questo blocco puo chiudere R-001.

## Criteri di accettazione

- Unit Rust: delta/wrap, deadline esatta, errore, saturazione, eventi/stop simultanei e budget invalido.
- Host C: IRQ assente, reference fermo, evento tardivo, stop durante wait, failure di stop/unbind/restore; output disgiunti e risorse ancora possedute su cleanup incompleto.
- QEMU positivo: dieci boot consecutivi sul profilo finale, piu ciclo continuo con stop e stato CPU/IRQ verificato. Quattro CPU non significano SMP attivo.
- Injection QEMU: sorgente ferma o consegna mancante; timeout osservabile senza dipendere da un altro IRQ accidentale. Verificare mask/count, token e IRR/ISR, nessun triplo fault inatteso.
- ASan host e accounting di risorse; nessuna dichiarazione di leak fisico misurato o coverage 100%.
- Distinguere fault terminale controllato da hang silenzioso; prima e dopo la deadline nessun marker di successo anticipato.

Il modulo attuale ha sei file diretti (access, calibration, driver, runtime, runtime header, timer header). Prima di proporre nuovi sorgenti, separare il gruppo runtime in una sottocartella funzionale e aggiornare soltanto i riferimenti necessari nelle interfacce/build/test. Nessun refactor globale.

## Fonti consultate

- [ACPI 6.5, §4.8.3.3 Power Management Timer](https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html): reference free-running 3.579545 MHz, 24/32 bit; leggere il contatore non arma una deadline IRQ arbitraria.
- [Intel SDM Vol. 3A, §§10.10.6.1 e 10.10.6.6](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf): PAUSE nei loop e clock reference distinto da un contatore di iterazioni. Prima di codice Assembly servono anche i riferimenti delle istruzioni STI/CLI/HLT nel Vol. 2.
- [Intel HPET 1.0a, §2.3.9.2.1](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf): comparator non-periodic e rischio di deadline gia superata durante arm.

Le fonti giustificano le alternative; non dimostrano supporto hardware fisico del progetto. R-001 resta aperto fino a implementazione e test del percorso scelto.
