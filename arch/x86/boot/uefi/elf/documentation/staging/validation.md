# R-014 — validazione degli indirizzi di staging ELF

Verifica del 5 ottobre 2026. Scope: modulo ELF e relativi test; nessuna modifica al kernel, timer o registri hardware.

## Correzione

Prima di azzerare/copiare una pagina restituita da AllocatePages, il C chiama il validatore numerico Rust `elf_check_staging_range`. Sono richiesti base >=4096, allineamento 4096, numero di pagine non nullo e intervallo interamente sotto il limite esclusivo 2^47. Moltiplicazione e somma sono checked, senza dereferenziare puntatori.

La base superiore canonica non e ammessa: questo loader usa il profilo identity nella meta inferiore, non un direct map della RAM a indirizzi virtuali alti. Il controllo non dimostra che una pagina sia mappata: tale contratto resta affidato al firmware conforme.

La modalita AllocateAnyPages e conservata per non cambiare il contratto degli altri moduli. Un'allocazione fuori profilo produce EFI_DEVICE_ERROR; non viene ritentata con AllocateMaxAddress. Un futuro supporto alla RAM oltre il profilo richiede un progetto distinto.

Le allocazioni distinte vengono registrate prima del controllo, per poterle rilasciare senza accedervi. Se FreePages fallisce, il record resta posseduto e ritentabile. Gli alias ambigui continuano a essere rifiutati senza azzerarli o liberarli: non si puo assumere che appartengano al chiamante.

## Verifiche

- Build UEFI con BootKernel e suite host completa: PASS, warning ordinari trattati come errori.
- Cinque test Rust ELF: PASS; confini esatti, zero, disallineamento, indirizzo non canonico, intervallo oltre limite, overflow di dimensione.
- Test C: sei indirizzi invalidi, memoria sentinella intatta, rollback parziale, errore FreePages e retry con accounting finale vuoto; casi di copia/BSS/alias preesistenti conservati.
- ASan host: 100 ripetizioni dei test di staging/ownership, piu validazione ELF; nessuna diagnostica nella campagna finale con script corretto.
- Dieci avvii QEMU consecutivi: KERNEL_READY in ogni VM, nessun FAIL/panic/triple fault inatteso; q35/OVMF/TCG, 128 MB, BSP.
- Handoff: quattro scenari verificati, inclusi fault intenzionali contract-write, stack-guard ed execute-data. Questi fault previsti non sono crash inattesi del fix.
- Riproduttore dell'audit: ELF_VALID seguito da EFI_DEVICE_ERROR (`0x8000000000000007`), exit 0 invece di access-violation.
- Cppcheck sul loader, warning/performance/portability: nessuna segnalazione nel profilo eseguito.
- File e cartelle del modulo sotto i limiti; diff senza trailing whitespace.

ASan non e un leak detector della RAM fisica UEFI. I test verificano accounting e rilascio delle allocazioni possedute nel mock; nessuna certificazione di zero leak su hardware reale, nessuna copertura percentuale misurata.

## Riproduzione ed evidenze

Dalla root: eseguire `arch/x86/boot/uefi/scripts/build.ps1 -BootKernel`, quindi `arch/x86/boot/uefi/test/elf/scripts/validate.ps1`. Lo script richiede MSVC/Windows SDK, QEMU/OVMF e un kernel ordinario gia compilato in kernel/build/kernel.elf, usato come fixture immutabile. Gli artefatti sono isolati in arch/x86/boot/uefi/build/elf-validation-<id>/.

Log riepilogativi: build/r014-build.log, build/r014-validation.log e build/r014-handoff.log. Prima campagna ASan/QEMU: elf-validation-1beb54afb0b943008c550a1dda6002a2; campagna con script senza warning: elf-validation-90bba785a3b7492399beed27cf4dc64e. Riproduttore original-probe.log nella prima campagna. Handoff: kernel-test-673c0771f3544f2088112efeb9f10203.

Il primo tentativo AllocateMaxAddress e stato scartato dopo un fallimento di compatibilita dei mock integrati. Un errore nell'offset della fixture di confine e un warning negli argomenti del nuovo harness sono stati corretti prima della validazione finale. La pulizia finale di validate.c/.h riguarda soltanto commenti e whitespace; la build/suite host sono state rieseguite dopo tale pulizia.

La full review in review_results rimane il documento storico dello snapshot pre-fix. R-014 e corretto e verificato nel profilo descritto; gli altri rilievi restano aperti.

## Fonti

- [UEFI 2.10, §7.2.1 AllocatePages](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html?highlight=exitbootservice): pagine da 4096 byte, AllocateAnyPages/AllocateMaxAddress e contratto di Memory.
- [Intel SDM Vol. 3A, §4.5 Canonicality](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf): nel paging a quattro livelli i bit 63:47 devono coincidere; il profilo inferiore termina a 2^47 escluso.
