# Profilo di boot x86

`arch/x86/configs/boot_profile.h` raccoglie i parametri statici del boot
x86 attuale. E un header condiviso da C e C++, senza dipendenze UEFI,
allocazioni o stato modificabile.

| Parametro | Valore attuale |
| --- | --- |
| Pagina | 4 KiB |
| Intervallo virtuale utilizzato | Meta inferiore canonica a 48 bit |
| Contratto di ingresso | `0x0000600000000000` |
| Pagina di guardia dello stack | `0x0000700000000000`, non mappata |
| Inizio stack | `0x0000700000001000` |
| Stack | 16 pagine, 64 KiB |
| Budget delle tabelle inattive | 128 pagine, massimo 512 KiB |

I budget indicano limiti; le tabelle vengono allocate solo quando servono.
Gli indirizzi sono virtuali e non impongono allocazioni fisiche a quelle
posizioni. I nomi `BOOT_*` del loader rimangono alias del profilo.
Anche il probe diagnostico usa gli stessi indirizzi e dimensioni.

I controlli in compilazione respingono indirizzi non allineati, intervalli
fuori dalla meta inferiore canonica, sovrapposizioni del contratto con
guardia o stack e budget fuori dai limiti attuali. La geometria resta
fissa a pagine di 4 KiB e quattro livelli; cambiarla richiede modificare
e verificare anche l'implementazione del paging.

Questo primo profilo descrive il boot attuale, non tutta l'architettura
x86 e non un formato di configurazione letto dal disco. Modificare un
parametro richiede una revisione dei consumatori e dei test indipendenti;
non ogni valore accettato dagli assert e un profilo gia supportato.

CPUID continua a rilevare NX, long mode e larghezza degli indirizzi fisici.
La lettura dei registri continua a controllare il paging attivo del
firmware. Il profilo non attiva NXE, WP, CR3 o altre funzioni hardware.

La build compila l'header nei consumatori C e C++ e verifica il piano,
le tabelle, il contratto e il probe. I test firmware restano in
`arch/x86/boot/uefi/test/`.
`test/configs/profile_test.ps1` verifica in entrambi i linguaggi il
profilo valido e otto varianti invalide: geometria, allineamento,
indirizzo non canonico, sovrapposizione, fine stack fuori intervallo,
stack vuoto e superamento dei budget di stack e tabelle.
Le copie modificate e le diagnostiche restano nella cartella `build/`;
il file di configurazione del progetto non viene modificato dai test.

Riferimento per geometria e indirizzi canonici:
[Intel SDM, volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
