# Capacita CPU richieste dal boot

`cpu/processor_query.c` usa la libreria `arch/x86/lib/processor/` per
leggere CPUID senza modificare CR3, CR4, IA32_EFER o lo stack.
`arch/x86/lib/processor/paging_capabilities.rs` controlla i valori
numerici in Rust no_std, senza puntatori, allocazioni o runtime.
I nomi descrivono la funzione dei file.

Il loader legge prima 0x80000000 e controlla il massimo leaf esteso.
Solo se esiste 0x80000008 legge 0x80000001 e 0x80000008. Il profilo
richiede long mode e NX, larghezza fisica da 32 a 52 bit e larghezza
lineare dichiarata di 48 oppure 57 bit. Restituisce EFI_UNSUPPORTED
per un profilo incompatibile; i dati di output restano invalidati.
Il risultato viene marcato valido soltanto dopo tutti i controlli.

Richiedere esplicitamente il leaf 0x80000008 e una scelta conservativa
del progetto: non implementiamo ancora il fallback per vecchie CPU.
Il codice controlla la CPU che sta eseguendo il boot. Non enumera altre
CPU e non verifica l'attendibilita di un hypervisor che fornisce CPUID.

Il bit NX segnala disponibilita, non attivazione: questo passo non
imposta IA32_EFER.NXE o CR0.WP. Non deduce il modo di paging attualmente
attivo dalla larghezza supportata. Prima del futuro cambio CR3 serviranno
controlli del modo attivo, mappature della transizione e gestione eccezioni.
Il costruttore usa ora queste capacita nel percorso di ingresso,
come descritto nella [preparazione UEFI](uefi-preparation.md).

## Proprieta e verifiche

Il controllo non alloca memoria. Nel percorso UEFI viene eseguito dopo
la verifica del piano; una CPU incompatibile porta alla pulizia dello
stack e dei segmenti, preservando lo status dell'errore iniziale. Quando
la pulizia riesce, il log ne conferma il completamento anche su errore.

Il callback di interrogazione usato nei test deve scrivere tutti e quattro
i registri, in modo sincrono. La versione di produzione usa l'intrinseco
MSVC __cpuid, compilato direttamente nell'istruzione x64.

I test C simulano leaf mancanti, NX assente, larghezza non supportata,
output precedente da invalidare e argomenti nulli. Verificano anche
l'ordine delle interrogazioni e che nessun leaf non disponibile venga letto.
Tre test Rust comprendono tutte le 65536 combinazioni dei campi a 8 bit
di larghezza fisica e lineare, piu i requisiti long mode/NX e leaf massimo.

La build normale esegue tutti questi test. Il test firmware contiene dieci
scenari, incluso `cpu-no-nx` con QEMU `qemu64,-nx`: richiede il messaggio
FAIL della CPU, la conferma della pulizia e il ritorno EFI_UNSUPPORTED.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

## Fonti ufficiali

- [Intel SDM Volume 3A, sezione 4.1.4](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf): CPUID, long mode, NX e larghezze degli indirizzi.
- [Microsoft, __cpuid e __cpuidex](https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170): dichiarazione e uso degli intrinseci.
