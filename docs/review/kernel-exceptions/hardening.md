# Review dettagliata delle eccezioni: correzioni e validazione

Data: 3 ottobre 2026. Stato iniziale: `abad469`.
Ambito: IDT, ingressi terminali, validazione Rust, diagnostica e relativi test.
La revisione non costituisce un audit completo di bootloader, allocator o OS.

## Problemi trovati e corretti

| Priorita | Problema nello stato iniziale | Correzione |
| --- | --- | --- |
| P1 | Un fault durante la stampa poteva richiamare il C sullo stesso IST, sovrascrivendo frame e stack precedenti. | Guardia Assembly atomica prima del C; secondo ingresso arrestato senza UART, con marcatore distinto. |
| P2 | La vecchia IDT era controllata solo nella base: un intervallo di 512 byte poteva superare il limite canonico. | Validatore Rust dell'intero operando IDTR, riusato per IDT nuova e precedente. |
| P2 | Il costruttore assumeva 32 indirizzi senza ricevere una lunghezza e verificava incompletamente gli intervalli di operando e target. | Parametro count obbligatorio, esattamente 32; controlli dell'intero buffer prima degli accessi e della scrittura. |
| P2 | Il ripristino dopo readback errato non veniva verificato. I mock non percorrevano il ramo di attivazione. | Rilettura anche dopo rollback; esito distinto se il ripristino fallisce e arresto diretto in produzione; test di tutti questi rami. |

La guardia evita il rientro nella diagnostica, ma non impedisce alla CPU di
sovrascrivere il primo frame su IST condiviso. E un arresto terminale, non
un recupero. Se l'IST stesso non e utilizzabile, la guardia non puo intervenire.

## Parti risultate coerenti

- Gate da 16 byte, CS 8, IST 1, tipo `0x8e`, riservati zero; IDTR da 10 byte,
  limite 511 e tabella allineata a 16 byte, ora con assert esplicito.
- Frame normalizzato da 56 byte; segnaposto solo per vettori senza error code
  CPU. Per double fault e richiesto zero. Non si simulano error-code faults
  con INT software.
- Prima della chiamata C: CLD, allineamento dello stack a 16 byte e 32 byte
  di shadow space. Nessun ritorno IRETQ, allocazione o dipendenza UEFI.
- Ownership della IDT statica; pagine del loader e IST mantenute riservate.
  CR3 e stato ExitBootServices verificati nelle prove di integrazione.
- Successo stampato dopo readback; colore limitato alle etichette.

Riferimenti verificati: [Intel SDM 3A](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
sezioni 2.4.3, 7.14 e 11.1.2.1 (XCHG su memoria atomico);
[ABI Microsoft x64](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170).

## Evidenze dello stato finale

Test host C: 4.176 alterazioni singole di bit (512 gate-byte e 10 IDTR-byte),
8.192 indirizzi generati con decodifica indipendente, count errato, alias,
null, disallineamento e intervalli fuori dal limite. Verificata l'assenza di
scritture parziali su input rifiutati, operando senza requisito di allineamento,
attivazione positiva, preflight negativo, readback errato e rollback fallito.
Il costruttore riceve buffer validi dal chiamante: i bounds numerici non provano
che un indirizzo arbitrario sia mappato o che il chiamante dica la verita sulla
dimensione del proprio buffer.

Regressione C del kernel e 15 test Rust complessivi superati. Build C/Assembly
con warning come errori, Rust con `-D warnings`, superata.

QEMU: 12/12 scenari superati. Ready 128/256 MiB, panic, NX, unmap, due guardie,
UD2, GP da selettore invalido, divisione per zero, guardia stack e fault annidato.
Nell'ultimo scenario la sequenza CPU e UD2, poi PF nel terminale C: un solo
messaggio `KERNEL_EXCEPTION`, SS salvato zero nel secondo frame e RAX
`0x4b4e455354450001`. Nessun nuovo ingresso nella seriale C e nessun triple fault.
La lettura del monitor e di sette qword anche nel caso annidato, per non
attraversare il confine superiore dell'IST.

Directory: `kernel/build/firmware-f9c32b1c09234a3fbbc5569e91e523c8`.
Log: `kernel/build/exceptions-review-host.log`, `exceptions-review-build.log`,
`exceptions-review-firmware.log`. Gli artefatti restano ignorati in build.

ELF: 36.864 byte su disco, 40.960 byte residenti; memoria residente invariata
rispetto al checkpoint precedente. Guardia aggiuntiva: un byte statico,
senza nuove allocazioni o tabelle paging. I due moduli C principali contano
201 e 204 righe; header e validatori rimangono compatti come concordato.

SHA-256 kernel costruito, installato e provato nella variante ready:
`1E235EF41234B438654C10184C066E674DA4E29CC46A2CEBD61CE2BC900A4E20`.

## Limiti e avanzamento

GDT, TSS e IST sono ancora ereditati. IF resta zero; manca gestione IRQ/APIC,
recupero dei fault, SMP e ring tre. NMI, machine check, guasto dello stack IST
e double fault reale non sono stati provati. La guardia globale deve diventare
per CPU prima di qualsiasi supporto SMP. Nessuna prova su hardware fisico.

Per questo profilo di bootstrap, le correzioni hanno test mirati e regressione
verde; non e una dimostrazione di perfezione o sicurezza assoluta.
Il prossimo modulo sara la proprieta kernel di GDT/TSS e stack d'emergenza
separati e protetti. Prima dei relativi LGDT/LTR vanno definite le nuove aree
virtuali, le guardie e i lifetime; non basta copiare descrittori del loader.
