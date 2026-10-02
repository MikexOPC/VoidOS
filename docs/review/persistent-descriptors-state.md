# Revisione delle risorse persistenti della transizione

Checkpoint del 2 ottobre 2026: allocazione dei descrittori e dei due
stack ausiliari, mappature RW/NX con guardie non presenti, ricostruzione
del grafo inattivo, fotografia aggiornata e cleanup nel boot ordinario.
Il contratto kernel resta v2. Nessun salto al kernel e nessun cambio CR3
vengono eseguiti nell'immagine ordinaria.

## Risultati verificati sul codice finale

- Build C/C++ e controlli host superati, inclusi i test Rust esistenti:
  sette test e 512 sequenze di allocazione.
- Tredici scenari host della nuova fase: successo, esaurimento memoria,
  output nullo, alias kernel, disallineamento, errore GetMemoryMap,
  fallimento della nuova tabella, copertura di tipo errato, errore
  FreePages, limite canonico, overflow, alias servizi e alias immagine EFI.
- Dieci scenari firmware QEMU superati. I percorsi validi richiedono
  la nuova fase e il suo successo prima del cleanup; CR3 resta invariato.
- Tre probe CPU separati superati: #DE, #UD e #GP con LTR e IST1,
  paging firmware. Non verificano il nuovo grafo della fase ordinaria.
- `git diff --check` superato prima del checkpoint.

La revisione dei mock ha corretto il conteggio delle pagine ancora vive
durante la ricostruzione, i tentativi di rilascio e la capacita della
fotografia con una regione EFI aggiuntiva. I controlli di produzione non
sono stati indeboliti per rendere verdi le prove. L'oracolo di liveness
verifica separatamente che il caso FreePages fallito trattenga l'owner.

## Artefatti locali riproducibili

BOOTX64.EFI: 68.608 byte; gli artefatti sono in build/ e non sono sorgenti.
SHA-256 dello stato verificato:

| Artefatto | SHA-256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | 24E358EA4AB602F5E5DF9564EE60743CC755A4CE1E47D94D9EA2C8C2B212C0B5 |
| persistent-build.log | DC7467C887C2B3973987B2D7DA98F639FC5DE88410D3FF586B26D4098350084C |
| persistent-firmware.log | 6F8C5E8F38FF30A62A9FDFFF9611B9E867AA85B5F0D34A07F0120335CAEFC809 |
| persistent-probe.log | 6C1244F4DC2E7D508A6394601263162289F060E2A0DD4A5AF746EBBBD00F4225 |

Directory delle prove: `firmware-test-57f4bf1a3c8840f0b4db9c2ce6a24d0d`
e `exception-test-cce77c0edc03408f9e53bc751c45e003`, sotto build/.
Una nuova esecuzione puo cambiare hash dei log e indirizzi firmware.

## Valutazione e lavoro restante

La nuova risorsa ha proprieta esplicita, controlli di overflow e alias,
pubblicazione dopo verifica e cleanup ritentabile. Costo: 36 KiB piu
fino a 12 KiB di nuove tabelle; le guardie non allocano RAM.
La [documentazione della durata](../handoff/descriptors/storage.md)
descrive anche i limiti del recupero da firmware non conforme.

Stima soggettiva del lavoro restante: **35–40%** per il primo ingresso
minimo nel kernel in QEMU; **60–70%** per un bootloader iniziale affidabile
su hardware x86_64 UEFI selezionato. Non sono percentuali misurate ne
una promessa di supporto universale. Nessun hardware fisico e stato provato.

Il prossimo blocco riguarda codice e trampoline persistenti: gli handler
restano nell'immagine EFI e non sono mappati nel nuovo grafo. Poi occorre
provare la sequenza reale di uscita UEFI, attivazione e salto, compresi
i percorsi terminali di errore. Restano prove su dispositivi reali,
profili firmware e un futuro profilo di eccezioni piu completo.
