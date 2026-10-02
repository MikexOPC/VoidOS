# Revisione del codice persistente della transizione

Checkpoint del 2 ottobre 2026, successivo a 1926c17. Il boot ordinario
prepara e verifica codice copiato indipendente dagli indirizzi del PE EFI,
lo mappa R/X e prepara i gate IDT verso i nuovi ingressi. Il contratto
kernel v2 resta invariato; descrittori e nuovo paging restano inattivi.

## Implementazione e revisione

- Template assembly di 580 byte: tratto finale del trampoline, 32 ingressi
  terminali e stop comune. Salti relativi interni; nessuna relocation nel
  relativo oggetto COFF, verificato con dumpbin /relocations.
- Rust no_std in produzione: oracolo dei byte e verifica numerica di
  dimensioni, indirizzi, allineamento, overflow e alias. Nessun blocco unsafe,
  heap o puntatore nel confine FFI; C mantiene la proprieta dei buffer.
- Dieci pagine contigue possedute, 40 KiB, con codice nell'ultima pagina.
  Foglia R/X in identita, separata dai tre mapping RW/NX. Fino a sei nuove
  pagine di tabelle, 24 KiB, secondo i percorsi condivisi gia presenti.
- CapsuleBase nel piano richiede esattamente una pagina R/X corrispondente,
  esclude il codice dal budget ELF di 128 MiB e impedisce che quel mapping
  soddisfi il controllo dell'ingresso del kernel. L'owner viene verificato
  anche contro alias del piano con le sue pagine prima della pubblicazione.
- Il preflight e la copertura firmware comprendono tutte le dieci pagine;
  cleanup e retry del rilascio conservano le regole del checkpoint precedente.

La revisione ha corretto la dipendenza iniziale del controllo Rust dalla
dimensione fissa dello stack: riceve ora la dimensione effettiva, entro
il budget di 64 KiB. Il probe serializza la copia con CPUID prima
dell'attivazione e rilascia la pagina sui percorsi di errore precedenti.
Una build e stata interrotta da un header temporaneo bloccato da un altro
processo; la nuova esecuzione completa e passata senza modificare i test.

## Evidenza sul codice finale

- Build con warning trattati come errori, controlli C/C++ e Rust verdi.
- Tre nuovi test Rust: salti, input estremi e 1024 basi riproducibili;
  32768 mutazioni C della pagina, incluso padding, tutte respinte.
- Regressioni di mapping: duplicati, permessi, guardie, capsule mancante,
  ingresso nella capsule, corruzione del codice e budget ELF pieno.
- Tredici scenari host dell'ingresso EFI e test Rust di allocazione
  esistenti, inclusi sette test con 512 sequenze, superati.
- Dieci scenari firmware QEMU superati sulla stessa immagine ordinaria.
- Tre eccezioni reali nel codice copiato, con LTR e IST1, superate.
- Un probe del trampoline copiato verifica destinazione raggiunta,
  stack ABI, argomento in RCX, ritorno nullo, IF=0 e DF=0.

BOOTX64.EFI e di 71.168 byte. Artefatti sotto build/, SHA-256:

| Artefatto | SHA-256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | 6CFEDBB7A29CE2F0E57199C786598E718458409197F6268F419D94F7823167E3 |
| capsule-build.log | 4E4A01FEE4A3C8B63C51D8C7C3512C60B07416A081D50033943E0478489D212D |
| capsule-firmware.log | 18F4E6551DBCBDDEA7BE378CE52EE6020C34CD815FED7F75D8DBBC28F1A59F08 |
| capsule-probe.log | BA70B8223F611549B6F53382E94B348B84A8D767DB875A2E58A6E02D1757E7D6 |

Log per scenario in `firmware-test-7f411991dae743628954f8e49eb34de0`
e `exception-test-ed35b344257e44fe95a7ff778432441b`.
Gli hash dei log possono variare nelle esecuzioni successive.

## Limiti e progresso indicativo

Il salto del probe raggiunge un arresto di test EFI usando il paging
firmware; non esegue il kernel ELF. La pagina ordinaria e EfiLoaderData
e puo essere NX nel paging firmware: non deve essere eseguita prima
dell'attivazione delle proprie tabelle. Il profilo IST1 comune non
garantisce consegna affidabile di eccezioni annidate o NMI.

Resta il tratto iniziale persistente per l'attivazione di descrittori,
NXE/WP e CR3, con verifica delle risorse raggiungibili durante ogni
passaggio. Restano ExitBootServices reale, salto al kernel e prove
su hardware. Non sono state effettuate prove su macchine fisiche.

Stima soggettiva del lavoro restante: **30–35%** per il primo ingresso
minimo nel kernel in QEMU e **60–70%** per un bootloader iniziale affidabile
su hardware x86_64 UEFI selezionato. Sono stime di pianificazione,
non misure di completezza o garanzie di supporto.

Dettagli e riferimenti ufficiali nel [contratto del codice persistente](../handoff/transition/capsule.md).
