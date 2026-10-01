# Kernel C diagnostico e contratto di ingresso

`test/kernel/entry.c` e un probe eseguibile, non il kernel dell'OS.
La build genera `build/kernel_probe.elf` e lo copia nell'ESP come
`EFI/BOOT/kernel.elf`. Le fixture sintetiche restano nei test negativi.

## Contratto preparatorio

`handoff/protocol/start_info.h` definisce una struttura di 72 byte, con
firma, versione, dimensione, funzionalita richieste e abilitate, root
fisica delle tabelle, intervallo virtuale dello stack e ingresso kernel.
Il bit 0 significa NXE; il bit 1 significa CR0.WP. Entrambi sono richiesti.
Il campo riservato deve essere zero. I tipi hanno ampiezza fissa nel
profilo x64; non sono presenti puntatori o servizi UEFI nella struttura.

Il [produttore preparatorio](start-preparation.md) costruisce e invalida
una bozza in una [pagina dedicata](start-page.md), mappata nelle tabelle
inattive con funzionalita abilitate a zero. La futura transizione
dovra mantenere questo oggetto
leggibile per tutta la chiamata e dichiarare le funzionalita abilitate
soltanto dopo la verifica dei registri. Il bootloader non lo passa ancora
al kernel. Non e ancora un protocollo completo: mancano la mappa memoria
finale e le altre informazioni necessarie al kernel dell'OS.

L'ingresso usa la convenzione Microsoft x64: puntatore in RCX, 32 byte
di shadow space del chiamante, RSP allineato a 16 prima di CALL e risultato
in RAX. Il probe controlla i campi numerici, scrive un marker BSS e ritorna.
Il chiamante deve fornire un puntatore realmente leggibile: i controlli
numerici non possono verificare la validita della memoria del puntatore.

## Generazione dell'ELF di prova

La toolchain disponibile produce COFF/PE. `scripts/build-kernel-probe.ps1`
compila C senza CRT e collega un PE fisso con base virtuale 4 GiB.
Un convertitore limitato ai tre segmenti del probe conserva indirizzi e
byte delle sezioni `.text`, `.rdata` e `.data` in un ELF64 ET_EXEC.
Non e un convertitore PE generale e non viene usato dal loader a runtime.

Accetta solo codice RX, dati R e BSS RW, senza import, relocation, TLS o
directory runtime. La directory debug e conservata come metadato in R,
senza essere interpretata dal loader ELF. Se la toolchain cambia il
profilo, la generazione fallisce invece di ignorare nuove sezioni.
Il file ELF e di 12 KiB; i tre segmenti occupano tre pagine, di cui la
pagina BSS contiene un marker di 8 byte. Stack e tabelle restano separati.

## Verifiche e limite attuale

La build esegue lo stesso oggetto C in una DLL host: prova ingresso
valido, puntatore nullo, 12 contratti incompatibili e assenza di scritture
al marker sugli errori prima del successo. Il parser C/Rust del loader
verifica anche l'ELF generato, permessi, BSS e 4096 prefissi troncati.
QEMU aggiunge lo scenario `kernel-probe` agli altri nove: legge, carica,
prepara le tabelle e libera le risorse con CR3 firmware invariato.

Il codice ELF non e ancora eseguito in QEMU. Prima del salto servono la
mappatura della transizione e del contratto, la gestione delle eccezioni,
ExitBootServices e il cambio controllato di stack e CR3.

## Riferimenti

- [ABI x64 Microsoft](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Formato PE e sezioni](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [ELF: segmenti e BSS](https://gabi.xinuos.com/elf/07-pheader.html)
