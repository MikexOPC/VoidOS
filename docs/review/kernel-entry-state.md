# Revisione del primo ingresso ELF nel kernel

Data: 2 ottobre 2026. Base precedente: bd2847837b10bf7c6bd23013419b97c90b948334.
Questa revisione accompagna il checkpoint con attivazione reale, non i log
dei checkpoint precedenti. Gli artefatti citati restano esclusi da Git.

## Risultato

Il percorso `build.ps1 -BootKernel` esegue ExitBootServices, entra nel codice
persistente, attiva NXE/WP, GDT/TSS/IDT e la nuova root CR3, quindi chiama
l'ingresso del kernel ELF diagnostico. Il marker restituito e verificato
dal monitor dopo l'arresto; non e dedotto dal messaggio prima dell'uscita.
Il futuro kernel ibrido non e ancora implementato.

Le risorse della transizione sono due allocazioni, 48 KiB complessivi:
undici pagine EfiLoaderData e una EfiLoaderCode. Nel nuovo grafo il codice
e R/X, dati e stack RW/NX; due pagine dati possedute restano non mappate.
Il rilascio prima dell'uscita conserva gli owner parziali su errori.
La copertura codice respinge XP, RP, RO e runtime prima della copia.
Rust no_std verifica anche il profilo CR4 e la root prima dell'uscita.

## Verifiche completate

| Gruppo | Evidenza |
| --- | --- |
| Build C/C++/Rust | Warning trattati come errori, profilo EFI e test host superati |
| Risorse descrittori | 19 scenari EFI host, alias delle guardie, cleanup separato codice/dati e retry |
| Profilo Rust transizione | 4 test, incluse root invalide e LA57/PCID/CET/PKS rifiutati |
| Regressioni Rust ownership | 7 test, comprese 512 sequenze riproducibili |
| QEMU diagnostico | 10/10 scenari, senza cambio CR3 |
| Probe CPU isolato | 3/3 eccezioni copiate e 1/1 ingresso ABI nel paging firmware |
| QEMU kernel | 4/4: marker ELF, scrittura contratto, guardia stack, esecuzione NX |
| Assembly persistente | Nessuna rilocazione COFF; profilo byte ricontrollato dalla build |

Le prove kernel controllano stato Exited, LastStatus zero, tentativi da
uno a tre, root CR3 esatta, NXE/WP, PGE disattivo, IF/DF zero e descrittori.
I fault producono #PF con CR2 e codici 3, 0 e 17 attesi, sullo stack IST1.
La ripetizione ha corretto un'aspettativa del test: SS deve essere zero
dopo cambio IST, mentre il frame conserva il precedente SS=16.
Il percorso normale verifica SS=16. Il manuale Intel conferma questa
distinzione; non e stata modificata la produzione per aggirare il test.

## Artefatti e identificazione

Tutti i percorsi seguenti sono relativi a `arch/x86/boot/uefi/build/`.
Gli SHA-256 identificano queste esecuzioni, non promettono build riproducibili.

| Artefatto | SHA-256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI, percorso kernel, 74752 byte | C44890E284312F6F9065106F38C22AF2A1E3CED8B9B4EF189AAFA588A34FAD83 |
| diagnostic-BOOTX64.EFI, copia diagnostica | 0D93B3936FE098D9C080C58AE8AFBFBF215E4878479AC3EE25D667957C2D4DA2 |
| kernel-build.log | 567DC7C2DAD3EA7E404EA36A20F5394BCA89C04010FDFBCF6E714203C28A4A5E |
| kernel-firmware.log | E2C5F10BB4AB14CDD3368C8DA00F8B2552554CE4723B14A772FDAEE03CBD3389 |
| activation-build.log | B74690D8BAEEC0384C8354FC60F7E069382EC2DC4E9CFE3D5BC1A08C69BF56FA |
| activation-firmware.log | A8A110ED35130925CDCE52A94DD8CD6CF6A52A9CDA0A3E7E759D15200A8EF8CC |
| activation-probe.log | CBFC7CF782525EC2E115B903322DBE22E7A6E1A0A723044034489B4EE9FB940F |

Directory delle prove:

- Kernel: kernel-test-fa575bc495734066955129544312a80e.
- Diagnostica: firmware-test-5b49feaf685c4aad98379eeb21b3241a.
- Probe isolato: exception-test-fc3e2a9487454095912f575167dc01fe.

I test diagnostici usano l'immagine salvata prima della build kernel;
il test kernel usa l'immagine finale. Ricostruire senza `-BootKernel`
ripristina il percorso diagnostico. Il test kernel non abilita da solo
l'attivazione nell'immagine EFI.

## Limiti e prossimo obiettivo

QEMU 11.1.0, EDK2, q35, CPU max, TCG e 128 MiB sono il solo ambiente
verificato. Non e stata avviata questa immagine su hardware fisico.
IST1 condiviso e arresto terminale non gestiscono eccezioni annidate;
il cambio GDTR/IDTR non e atomico rispetto a NMI. Interrupt esterni,
inizializzazione SIMD/FPU/TLS e diagnostica autonoma restano da sviluppare.
Un firmware non conforme o un FreePages permanentemente fallito puo
lasciare risorse non recuperabili: il cleanup non garantisce il contrario.

Il requisito per iniziare il kernel e raggiunto: esecuzione ELF dopo
uscita e attivazione. Il prossimo obiettivo e una base kernel distinta
dal probe, con gestione esplicita del contratto e diagnostica propria.
La maturita su hardware non e misurabile con la percentuale di test QEMU.

Riferimenti: [sequenza e comandi](../handoff/transition/activation.md),
[UEFI 2.11](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel SDM 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 4.10.4, 6.14.3-6.14.5, 9.1.3 e 9.3.
