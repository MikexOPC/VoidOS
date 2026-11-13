# GOP v0: selezione della modalita — 9 ottobre 2026

Snapshot della prima fase isolata. Stato corrente nel
[report handoff v4](handoff-validation.md); le prove sotto riguardano
il probe EFI separato di questa fase.

Scelta utente: enumerare e impostare un modo video supportato. Prima fase
del modulo framebuffer, con Rust per validazione e punteggio, C per
ownership dei buffer firmware e sequenza QueryMode/SetMode/readback.
La console e il nuovo handoff non sono implementati da questa prova.

## Implementazione e contratti

`framebuffer.h` espone solo metadati numerici. Il nucleo Rust non usa
unsafe o heap; il gateway ABI esporta solo interi. Geometria RGB/BGR32,
stride distinto dalla larghezza e limiti operativi descritti nel
[progetto](design.md). Enumerazione massima256, punteggio distanza da
1024x768, parita risolta dal primo indice. Costo O(numero modi), spazio O(1).

L'adattatore in `arch/x86/boot/uefi/graphics/` controlla prefissi e callback,
query complete, FreePool prima di SetMode e readback dopo il cambio.
Un buffer che aliasa i metadati vivi non viene adottato o liberato.
FreePool fallita conserva Pending, disponibile per retry; nessun avanzamento.
SetMode fallita/readback incoerente tentano il ripristino originale;
Dirty/RestoreFailed restano espliciti quando il ripristino non riesce.
I callback firmware sono fidati e non rientranti; una chiamata firmware
che non ritorna non puo essere interrotta dal solo limite di enumerazione.
Il test host termina soltanto la propria VM entro deadline.

Il probe EFI e separato dal loader corrente. Non chiama ExitBootServices,
non esegue il kernel e non scrive direttamente nella memoria video.
Il parametro52 del probe e soltanto il tetto del formato indirizzi x64;
non e una misura CPUID dei bit fisici. Prima del mapping servono i bit
CPU reali, cache policy, esclusione dall'allocatore e verifica degli alias.

## Prove eseguite

- Host (`kernel/build/framebuffer-gop-host.log`): 4 test Rust
  sia debug sia release con overflow checks, warning come errori e Clippy.
  C MSVC /W4 /WX e ASan:190.186 controlli,10.000 sessioni,30.000
  allocazioni bilanciate nella fase ripetuta. Callback hardware simulate.
  Errori: GOP assente/null, QueryMode fallita con output non adottato,
  buffer null/alias/corto, nessun formato supportato, FreePool fallita
  con retry, SetMode/readback/capacita/base errati, rollback fallito,
  limite modi e parita. Ogni caso termina con accounting esplicito.
- Cppcheck (`kernel/build/framebuffer-gop-final/cppcheck.xml`):
  3 file C; zero error/warning/performance/portability. Style/information
  restano: callback const suggeriti incompatibili con la firma ABI,
  entry EFI vista come unused nell'analisi parziale, header standard
  non espansi. Le include locali sono risolte con le directory di build.
- QEMU positivo 10/10 (`kernel/build/framebuffer-firmware-e09fbdf3b69a48e9946ac8ed1902da1e/results.json`):
  q35/TCG/max,128MiB,BSP,OVMF. Modalita1024x768,BGR32,stride1024,
  capacita/usato3.145.728 byte,30 query e30 rilasci in ogni boot.
  Hash immagine/ESP, firmware e binario QEMU registrati. Screenshot PPM
  completo e geometria coerente; marker GOP_SELECTION_READY, cattura
  CPU, nessun triple fault. Non e una prova del rendering del kernel.
- QEMU senza video (`kernel/build/framebuffer-firmware-f1da6ac7ffaa4b0180a92060dad455c4/results.json`):
  `-vga none`, marker GOP_UNAVAILABLE sulla console seriale,1/1.
- Manifest sorgenti corrente (`kernel/build/framebuffer-handoff-final/source-review.json`):
  limiti righe/cartelle, UTF-8/LF e whitespace della fase corrente.
  Non certifica la corrispondenza dei sorgenti attuali con l'immagine
  storica del probe descritta in questo snapshot.

Immagine EFI verificata:
`E5BCF74E0A5BC9E2F8A79F70135E741A0668C86A547100F28E7314B385835CAB`.
Loader ordinario prima/dopo:
`9F4F5C93A46200D387D493E3F7EB81656BBB504DEC8BF98C892CBAF7C1D9E989`.
I report QEMU confermano ProductionUnchanged=true. Non e stata necessaria
una nuova regressione ordinaria: il codice/immagine del percorso ordinario
non e stato modificato o collegato al probe.

ASan strumenta solo il C host, non Rust e firmware. L'accounting host
misura malloc/free del mock; quello QEMU conta i buffer QueryMode.
Non dimostrano zero leak globale del firmware o RAM del kernel.
Valgrind e coverage linee/branch N/D; hardware fisico non testato.

## Debug e revisione finale

I gate hanno rilevato un attributo no_std collocato nel modulo anziche
nel gateway e una directory include mancante per il probe; corretti.
Lo smoke QEMU ha rilevato il separatore assente nei log numerici e
il monitor bloccato sul buffer stdout della pipe Windows. Log riallineati;
stdout/stderr del monitor ora vanno direttamente su file, con capture
CPU e screenshot verificati prima del successo. Le VM fallite sono
state terminate dal loro harness, senza intervenire su processi esterni.
I dieci boot finali usano l'immagine sopra, successiva alle correzioni C.

## Riproduzione

```powershell
& kernel/drivers/video/framebuffer/scripts/test.ps1 -OutputDirectory D:/OS/kernel/build/framebuffer-gop-final
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" kernel/drivers/video/framebuffer/scripts/firmware.py --image D:/OS/kernel/build/framebuffer-gop-final/BOOTX64.EFI --boots 10
& "$env:USERPROFILE/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe" kernel/drivers/video/framebuffer/scripts/firmware.py --image D:/OS/kernel/build/framebuffer-gop-final/BOOTX64.EFI --boots 1 --absent
```

La prima fase GOP passa i gate del suo profilo. Prossimo blocco dello
stesso percorso: handoff versionato e mapping video dedicato RW/NX con
cache verificata; solo dopo, disegno e console testuale/shell.
