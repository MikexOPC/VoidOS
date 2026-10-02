# Controllo Rust dei segmenti ELF nel boot

`arch/x86/boot/uefi/elf/rust/validation/segment_bounds.rs` e il primo modulo Rust
collegato all'immagine EFI. Il nome descrive la funzione del file;
non viene creata una cartella di produzione chiamata rust.

Il parser C continua a leggere l'header dopo aver verificato il buffer
e la tabella programmi. Passa al modulo Rust sette valori numerici:
permessi, offset, indirizzo virtuale, dimensioni file/memoria, allineamento
e dimensione del file. Rust non riceve puntatori, non accede alla memoria
fisica e non possiede o alloca buffer.

Il modulo verifica intervalli, sottrazioni controllate, dimensione non
nulla in memoria, file_bytes <= memory_bytes, indirizzi del profilo basso
a 47 bit, permessi senza W+X e congruenza degli allineamenti. Restituisce
gli stessi codici numerici del validatore C. Un controllo di compilazione
C protegge la corrispondenza dei tre codici usati dal modulo.

Rust usa no_std in produzione. La build emette un oggetto COFF x64,
con panic=abort e ottimizzazione, collegato dal linker EFI senza runtime.
Per questa funzione numerica viene usato il backend x64 MSVC installato;
non e un'applicazione Windows e non usa API Windows. Il collegamento
/nodefaultlib e la verifica dell'immagine senza import restano obbligatori.
Se future modifiche richiedessero funzioni runtime, la build deve fallire
e il progetto dovra definire esplicitamente come gestirle.

I test dedicati sono in `test/elf/rust/validation/segment_bounds_test.rs`, separati dal
codice di produzione. Coprono overflow, troncamenti, allineamenti,
permessi e limiti esclusivi. I test C del parser, comprese 5000 mutazioni,
esercitano il percorso C -> Rust reale. I test QEMU verificano l'integrazione
nel firmware con la fixture ELF e i casi di errore gia presenti.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

Questo passo sposta in Rust i controlli numerici dei segmenti, non l'intero
parser ELF. Header, accessi al buffer, ordinamento, sovrapposizioni e controllo
dell'entry restano nel C e mantengono i test esistenti. Non introduce
autenticazione del kernel o attivazione delle tabelle di pagina.

Fonti: [ELF, program header](https://gabi.xinuos.com/elf/07-pheader.html),
[Rust, ABI ed emissione di oggetti](https://doc.rust-lang.org/reference/abi.html),
[Rust, opzioni di compilazione](https://doc.rust-lang.org/rustc/codegen-options/index.html).
