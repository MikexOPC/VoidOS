# Test Rust delle allocazioni delle tabelle

## Che cosa viene verificato

Rust ora verifica il costruttore e il rilascio delle tabelle C reali.
Non e una riscrittura del loader: `paging/plan.c` e `paging/tables.c`
sono compilati nuovamente e collegati a un eseguibile host Rust.
Il piccolo bridge C costruisce la fixture numerica e collega i callback.

Il modello Rust mantiene un insieme indipendente delle pagine vive.
Dopo costruzione e rilascio lo confronta con le pagine nel proprietario C.
Ogni rilascio deve riguardare una pagina ancora posseduta: un doppio
rilascio o un indirizzo estraneo viene registrato come errore del test.

Sette test verificano:

- dimensione, allineamento e offset della struttura condivisa con C;
- indirizzi e permessi delle foglie e dei livelli intermedi;
- assenza di pagina zero, guardia e alias identity non richiesti;
- fallimento di ciascuna allocazione, anche con output numerico alterato;
- fallimento persistente di ciascun rilascio e recupero successivo;
- capacita non supportate e output duplicato del firmware;
- 512 sequenze deterministiche con combinazioni di errori e tentativi.

La lettura delle tabelle cerca prima l'indirizzo fra le pagine vive
del modello. Un riferimento non posseduto fallisce senza dereferenziare
un indirizzo arbitrario. Le quattro prove attraversano anche confini
di 2 MiB, 1 GiB e 512 GiB.

## Memoria e confine FFI

Ogni pagina simulata ha allineamento 4096 e due sentinelle da 4096 byte,
prima e dopo l'area accessibile al C. Tutti i byte iniziano a 0xA5;
il C deve azzerare le proprie tabelle senza cambiare le sentinelle.
L'allocazione reale del test occupa quindi 12 KiB per pagina, non 4 KiB.

Il rilascio simulato rimuove subito la pagina dall'insieme vivo ma conserva
la memoria fino alla fine della sequenza, per ricontrollare le sentinelle.
Il proprietario Rust la dealloca con lo stesso Layout dell'allocazione.
Questa conservazione non costituisce un rilevatore generale di use-after-free.
Le sequenze azzerano lo stato tra i casi, limitando la memoria trattenuta.

L'ABI usa `repr(C)` e `extern "C"` su Windows x64 MSVC. Le parti unsafe
sono limitate al confine, alle allocazioni allineate e alla lettura delle
pagine possedute. I callback sono sincroni e non conservano l'output C.
Le verifiche con assert vengono eseguite fuori dai callback; errori del
modello nei callback tornano come status EFI. Lo stato e locale al thread,
cosi i test paralleli non condividono proprieta o errori simulati.

## Esecuzione

Richiede Rust per `x86_64-pc-windows-msvc` e la toolchain MSVC gia usata
dal progetto. Non usa crate esterne, Cargo o download durante i test.
La build normale esegue automaticamente anche questi test:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
```

Per eseguire soltanto i test Rust, ricompilando comunque il C necessario:

```powershell
./arch/x86/boot/uefi/scripts/test-rust.ps1
```

Gli oggetti e l'eseguibile sono in `build/rust-tests/`. Rust usa std nel
processo di test Windows; questo eseguibile di test non viene collegato
all'immagine BOOTX64.EFI. Il modulo di produzione per i
[limiti dei segmenti ELF](../elf/segment-bounds.md) usa invece no_std nel boot.
La larghezza fisica di 52 bit e un parametro della simulazione host;
non rappresenta il rilevamento delle capacita della CPU.

Queste prove completano i test C; non dimostrano l'assenza di ogni bug
nel C, non attivano le tabelle e non sostituiscono QEMU o prove hardware.

## Riferimenti ufficiali

- [Rust: FFI e confini di unwinding](https://doc.rust-lang.org/nomicon/ffi.html)
- [Rust: rappresentazione C](https://doc.rust-lang.org/nomicon/other-reprs.html)
- [Rust: allocazione e Layout](https://doc.rust-lang.org/std/alloc/index.html)
