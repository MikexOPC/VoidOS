# Lettura e classificazione delle regioni UEFI

## Scopo e politica iniziale

`memory/regions.c` legge una regione dalla fotografia verificata e
costruisce un riepilogo numerico. Non alloca o libera pagine e non
trasforma la mappa in un allocatore del kernel.

Una regione e candidata convenzionale solo se il suo tipo e
EfiConventionalMemory (7) e gli attributi contengono esclusivamente
i bit di cacheabilita UC, WC, WT, WB, UCE e l'eventuale bit XP.
Gli altri attributi, inclusi RUNTIME, protezioni di lettura/scrittura,
memoria persistente e special purpose, escludono la regione dal conteggio.
Tipi sconosciuti sono conservati ma non diventano candidati.
Loader, Boot Services, Runtime Services, ACPI, MMIO, memoria non accettata
e gli altri tipi restano esclusi, anche se alcuni potranno essere
recuperati piu avanti con una politica apposita.

Questa e una scelta conservativa del progetto, non una prescrizione
UEFI di scartare tutte queste regioni. Gli attributi descrivono anche
capacita della memoria, non necessariamente la configurazione attuale:
il conteggio non dimostra accessibilita, permessi MMU o impostazioni cache.
XP non viene interpretato come permesso di eseguire codice.

## Durata, limiti e controlli

BOOT_MEMORY_REGION contiene copie numeriche di base fisica, numero di
pagine, attributi, tipo e indicazione di candidatura. Non contiene un
puntatore a un descrittore firmware. Il riepilogo conta regioni, pagine
candidate e pagine escluse; il secondo gruppo puo includere MMIO e non
va interpretato come RAM occupata. Ogni pagina UEFI ha 4096 byte.

La lettura richiede una mappa ancora valida, buffer posseduto e metadati
compatibili con la capacita. Usa DescriptorSize e controlla l'indice
prima di accedere. Decodifica i byte senza cast a struct. Il riepilogo
rivalida l'intera mappa, comprese le sovrapposizioni, prima del conteggio.
Non pubblica risultati parziali e controlla le somme contro overflow.

Il chiamante non deve copiare il proprietario della mappa, mutare il
buffer durante la lettura o sovrapporre output, mappa e buffer. Una
lettura individuale non rivalida tutte le altre regioni: richiede la
fotografia immutabile gia verificata. Gli output vengono azzerati su errore.

Dopo boot_release_memory_map la lettura viene rifiutata, anche quando
FreePool fallisce: la fotografia e comunque invalidata. Le copie numeriche
gia ottenute non diventano puntatori pendenti, ma possono essere obsolete.
Non devono essere usate per decidere nuove allocazioni dal firmware.

Finche i Boot Services sono attivi si usa AllocatePages/AllocatePool.
Prima di un futuro allocatore del kernel serviranno mappa finale,
ExitBootServices, riserve delle risorse ancora vive, esclusione delle
pagine speciali come la pagina zero e una politica dei permessi/cache.
Nessuno di questi passaggi viene dedotto dalla sola candidatura.

La [verifica delle risorse](../handoff/resources.md) ora controlla
separatamente la copertura delle pagine del kernel e dello stack
come EfiLoaderData: non le considera memoria convenzionale candidata.

## Test e integrazione

`test/memory/regions_test.c` copre i tipi da 0 a 16, un tipo sconosciuto,
ciascuno dei 64 bit degli attributi, un buffer disallineato con passo
esteso a 48 byte, sovrapposizioni, overflow, indice fuori limite, mappa
invalidata e indipendenza delle copie numeriche dal buffer originale.
Il percorso di ingresso costruisce il riepilogo prima del rilascio e
stampa OK solo dopo classificazione e rilascio riusciti.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./test/arch/x86/boot/uefi/firmware_test.ps1
```

[Specifica UEFI: tipi e attributi di memoria](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
