# Costruzione delle tabelle nel percorso UEFI

Il loader ora costruisce davvero le nuove tabelle prima del ritorno al
firmware. `paging/preparation.c` usa il piano del kernel e dello stack
ancora vivi e le capacita CPU rilevate. Le tabelle restano inattive:
CR3, CR4, EFER e RSP non vengono modificati, la fixture non viene eseguita.
Ora lo [stato attivo](active-state.md) viene anche letto e confrontato,
cosi il messaggio CR3 invariato segue una verifica dei registri.

## Nuova fotografia dopo le allocazioni

Una mappa precedente non descrive necessariamente le nuove allocazioni.
Dopo la costruzione si acquisisce una nuova mappa e si ricontrollano
kernel, stack e tutte le pagine delle tabelle. Il controllo richiede:

- mappa completa valida e risorse kernel/stack coerenti;
- proprietario valido con 1..128 pagine, root uguale alla prima pagina;
- indirizzi non nulli, allineati e nella larghezza fisica rilevata;
- indirizzi delle tabelle distinti e coperti come EfiLoaderData;
- nessun buco o descrittore runtime nella copertura.

Il costruttore esclude gia la sovrapposizione delle nuove pagine con
kernel e stack prima di scrivere nelle tabelle. Il proprietario resta
immutabile tra costruzione e verifica; la mappa da sola non dimostra
chi possieda la memoria.

La fotografia viene rilasciata dopo il controllo. La sua chiave non
e conservata per ExitBootServices: per quello servira una mappa finale
acquisita nel futuro percorso di transizione.

## Proprieta e pulizia

BOOT_PAGE_TABLES e BOOT_MEMORY_MAP restano proprietari nell'ingresso UEFI.
La funzione di preparazione li riceve per riferimento, inizialmente vuoti,
senza copiarli. Su errore le pagine gia allocate restano nel proprietario;
un errore della mappa invalida Root/Valid prima della pulizia.

Se FreePool della mappa fallisce, il buffer resta nel proprietario del
chiamante e la pulizia comune tenta ancora il rilascio. Lo status del
primo errore rimane preservato anche se questo tentativo riesce.
Il percorso boot include ora la [pagina contratto R/NX](../handoff/start-page.md),
allocata prima delle tabelle e verificata nella stessa fotografia UEFI.
La fotografia viene anche [aggiornata senza nuove allocazioni](../memory/final-map.md)
prima dei controlli di copertura e contenuto.
La pulizia tenta buffer mappa, tabelle, pagina contratto, stack e kernel, anche se una
risorsa precedente non viene rilasciata. L'OK finale delle pagine kernel
richiede anche il rilascio dello stack e delle tabelle.

Un errore persistente del firmware puo lasciare risorse allocate:
il loader ritorna errore, non promette il recupero dopo il ritorno.
La preparazione non trasferisce queste pagine a un kernel in esecuzione.
Non usare il rilascio delle tabelle dopo averle rese attive.

## Memoria e prove

Massimo 128 pagine di tabelle, cioe 512 KiB. La fixture attuale usa
8 pagine di tabelle: 32 KiB, oltre a 8 KiB di segmenti e 64 KiB di stack.
Il piano e locale alla funzione di preparazione; i proprietari restano
nell'ingresso senza allocazioni aggiuntive per i metadati.

I test host simulano capacita fisiche di 52 bit per gli indirizzi virtuali
del processo Windows. `test/cpu/processor_mock.c` viene collegato soltanto
alla DLL dei test; l'immagine EFI usa `cpu/processor_query.c` come
adattatore della [libreria x86](../libraries/x86-processor.md), con CPUID reale.
La validazione del profilo rimane la stessa funzione Rust in entrambi i casi.

Nuovi test numerici verificano proprietario, root, duplicati, allineamenti,
limiti fisici, buchi, memoria convenzionale e runtime esclusi. I test di
ingresso simulano fallimento a meta costruzione, CPU senza NX, copertura
errata delle tabelle, seconda mappa fallita, FreePages fallito e FreePool
della seconda mappa fallito una volta. Controllano che la pulizia continui.
I test Rust delle tabelle e le 512 sequenze rimangono nella build.

QEMU verifica nove scenari, incluso il nuovo messaggio di costruzione
e copertura nel caso valido. Non attivando le tabelle, non verifica
eccezioni MMU o protezioni NX effettive della CPU.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

Fonte: [UEFI, allocazioni e GetMemoryMap](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html).
