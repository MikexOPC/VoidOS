# Handoff framebuffer v4 — 9 ottobre 2026

Modulo funzionale: trasferimento del descrittore video. Gli aggiornamenti
di loader, contratto, kernel e build sono le interfacce di questo blocco;
non introducono un secondo driver, un mapper o una console grafica.
Scelta delegata dall'utente: video opzionale, seriale disponibile.

## Risultato

BOOT_START_INFO v4:208 byte, framebuffer da40 byte all'offset168.
Il prefisso numerico precedente resta nella stessa posizione; v3 viene
rifiutata, non viene dichiarata una compatibilita non implementata.
Static assert compilati verificano dimensione e offset.

GOP viene selezionato prima di ExitBootServices. Vengono copiati soltanto
base fisica, capacita, byte utilizzati, geometria, stride e formato.
Zero completo significa assenza; descrittore presente invalido e un errore.
Non si trasferiscono puntatori GOP, callback o buffer QueryMode.
Il kernel ricontrolla geometria e risorse prima dell'allocatore e stampa
i dati ricevuti, poi prosegue fino a KERNEL_READY.

Le snapshot dei retry conservano ACPI e framebuffer, ricostruiscono il
contratto e verificano nuovamente il descrittore sulla nuova mappa.
Nessuna nuova allocazione, log o callback GOP nel percorso ristretto
dei retry. CR3/stato paging verificati nuovamente dopo la selezione GOP.

L'intervallo video e arrotondato alle pagine per il controllo dei vicini:
nessuna sovrapposizione con tipi RAM, map buffer o contratto vivo.
Riservato0 e MMIO11 sono gli unici tipi ammessi in caso di sovrapposizione;
un intervallo assente dalla mappa non viene inventato come RAM.
Fonte primaria dell'enumerazione: [EDK2 edk2-stable202508 UefiMultiPhase.h,
EFI_MEMORY_TYPE](https://raw.githubusercontent.com/tianocore/edk2/edk2-stable202508/MdePkg/Include/Uefi/UefiMultiPhase.h).
Questo controllo non dimostra identity PCI/BAR o cache policy: nessuno
store video e consentito dalla sola validazione del descrittore.

## Cleanup e sicurezza

FreePool fallita conserva il buffer per retry. Se anche il rilascio nel
cleanup fallisce, il loader segnala GOP_BUFFER_RELEASE_FAILED e si ferma,
senza perdere l'owner. Abbandonare il boot dopo un cambio valido ripristina
il modo originale; fallimento segnalato con GOP_MODE_RESTORE_FAILED,
fail-stop. I test verificano anche un ripristino fallito seguito da retry.
Il framebuffer non viene liberato con FreePages: non e RAM posseduta dal loader.

C possiede copia e sequenza; Rust valida campi numerici. Il parser della
mappa legge campi little-endian senza accessi disallineati. Limiti4096
descrittori, stride40..4096, nessun heap; costo O(numero descrittori), spazio O(1).
Il caller possiede un buffer mappa accessibile con la lunghezza dichiarata;
questa API interna non e un user-copy e non sostituisce fault recovery.
Nel loader i bit fisici sono quelli CPUID; il controllo numerico iniziale
kernel usa il tetto52 come quello ACPI, senza autorizzare un mapping.

## Prove finali

- Suite dedicata (`kernel/build/framebuffer-handoff-host-final.log`):
  8 casi Rust distinti debug/release, Clippy e warning come errori.
  C MSVC /W4 /WX e ASan:230.214 controlli GOP,10.000 sessioni,
  30.000 allocazioni mock bilanciate nella fase ripetuta;30.079 controlli handoff,
  10.000 pubblicazioni senza allocazioni. Buffer corti, assenza parziale,
  tipi0..15, confini di pagina, mappa corta, overflow, rollback e alias.
- Build/test loader (`kernel/build/framebuffer-handoff-boot-build-final.log`):
  suite C e Rust completa dello script supportato. Test ExitBootServices
  verificano il framebuffer attraverso uno, due e tre tentativi e i
  percorsi di errore esistenti. Questi retry sono mock host, non errori
  ExitBootServices indotti nel firmware QEMU.
- Suite host kernel (`kernel/build/framebuffer-handoff-kernel-host.log`):
  contratto, copie senza alias, ownership/rollback e regressioni memoria.
  Nuovi casi:40 alterazioni singole del descrittore assente rifiutate,
  v3 rifiutata e copia di un descrittore valido.
- Build kernel finale (`kernel/build/framebuffer-handoff-kernel-build-final.log`):
  ELF ordinario verificato dalla toolchain supportata.
- Cppcheck modulo (`kernel/build/framebuffer-handoff-final/cppcheck.xml`)
  e interfacce (`kernel/build/framebuffer-handoff-final/integration-cppcheck.xml`):
  rispettivamente6 e4 file C, zero error/warning/performance/portability.
  Analisi parziale: restano suggerimenti style/information e header standard
  non espansi; non equivale a una prova formale o a un audit globale.
- QEMU con video 10/10 (`kernel/build/framebuffer-firmware-316947b1070b4274aee28e86b7463c86/results.json`):
  loader e kernel reali, ExitBootServices e paging proprio; v4/208,
  framebuffer1024x768 BGR32, stride1024,3.145.728 byte. Dati letti dal
  kernel, presenza concorde al loader, KERNEL_READY, CPU e screenshot
  completi, nessun triple fault. Screenshot valida la geometria firmware,
  non un rendering del kernel.
- QEMU senza video 10/10 (`kernel/build/framebuffer-firmware-39bdd311e4f542ceaaf6b8ec56a9ae14/results.json`):
  stessa coppia di immagini, -vga none, tutti i campi video zero,
  KERNEL_READY raggiunto tramite seriale.
- Regressione PS/2 10/10 (`kernel/build/ps2-input-firmware-eee37fb51c80479283e67c01c6d4b0ce/results.json`):
  stesso loader finale, kernel input ricompilato; testo italiano/editor,
  task shell separata, IRQ/idle/cleanup verificati dal relativo harness.
  Hash ELF input:
  CE7B0058F19134822AA6CDF07AD2221CA18DACD4A05E3D078E9A0A470C99A99E.
- Manifest finale (`kernel/build/framebuffer-handoff-final/source-review.json`):
  29 sorgenti/script/interfacce, massimo373 righe, nessun errore di
  whitespace/LF o limite. Le cartelle funzionali nuove rispettano5 file.
  I file nuovi contengono soltanto commenti separatori; reuse/ intatto.

Hash EFI finale:
EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7.
Hash ELF ordinario finale:
6AC46965711A8995B91E44513CF63813852DBFEFA82DA78B893F6D5A17AD75B3.
Le campagne precedenti al fix di cleanup restano evidenza storica,
non vengono sommate ai dieci boot finali.

Coppia installata nell'ESP (`kernel/build/framebuffer-handoff-final/esp-install.json`):
copiato il kernel ordinario verificato, hash confrontati prima/dopo;
conservato il probe precedente negli artefatti. Nessun commit/push in questo blocco.

Riproduzione della coppia ordinaria:

```powershell
& arch/x86/boot/uefi/scripts/build.ps1 -BootKernel
& kernel/scripts/build.ps1 -Install -OutputDirectory D:/OS/kernel/build/framebuffer-handoff-kernel
& kernel/drivers/video/framebuffer/scripts/test.ps1 -OutputDirectory D:/OS/kernel/build/framebuffer-handoff-final
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" kernel/drivers/video/framebuffer/scripts/firmware.py --image D:/OS/arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI --kernel D:/OS/kernel/build/framebuffer-handoff-kernel/kernel.elf --boots 10
```

Ripetere l'ultima riga con --absent per il profilo senza video. Build del
loader rigenera inizialmente il probe nell'ESP: eseguire la build/install
kernel dopo quella del loader per avviare il kernel ordinario.

## Debug risolto e limiti

Due oggetti handoff.c si sovrascrivevano nella build host: ora nomi distinti.
Il linker ASan richiedeva /DEBUG con /WX: corretto. I nuovi header standard
richiedevano le include UCRT anche nella build EFI/probe: esplicitate.
Revisione del cleanup: ripristino del modo originale aggiunto e ritestato.

ASan riguarda C host; Rust e firmware non sono strumentati. Accounting
malloc/free misura il mock, non le allocazioni interne di SetMode o leak
globali del firmware/RAM fisica. Valgrind e coverage linee/branch N/D.
Profilo BSP QEMU/OVMF, hardware fisico non testato. SMEP/SMAP/KPTI non
vengono introdotti da questo blocco; non c'e una nuova transizione ring.
Limiti video v0 restano quelli del [progetto](design.md), incluso il
budget32MiB: non e una garanzia di compatibilita con ogni GPU reale.

## Prossimo blocco

Mapping video dedicato RW/NX, bit fisici reali e cache policy verificata
su PAT/MTRR, budget delle tabelle, esclusione degli alias e accounting.
Solo dopo: store limitati, pattern controllato tramite screenshot e
recupero delle primitive buone del vecchio OS per testo/console.
