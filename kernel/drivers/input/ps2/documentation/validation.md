# Verifica decoder PS/2 — 9 ottobre 2026

Perimetro: parser scan set 2 e ABI C/Rust. Nessun accesso al controller
PS/2, IRQ1 o input proveniente da una tastiera. La fase driver hardware
non e completata; il gate di input operativo resta da eseguire.

## Test host

[Log finale](../../../../build/ps2-decoder-host-final.log), evidenze
[cartella host](../../../../build/ps2-decoder-1e53b635f8974c1186f32310d51db0c9/tools.log).
Undici casi Rust passano sia debug sia release, con overflow checks e
warning come errori. Ogni modalita include tutte le 65.536 coppie di byte,
un milione di byte pseudocasuali, mutazioni di ogni posizione della coda
Pause e reset di ogni stato parziale. Queste prove di bounds e grammatica
non sono una misura di coverage: linee/branch N/D.

Clippy pulito per parser/test e gateway. Gateway compilato no_std, panic
abort, target x64 MSVC; ABI verificata da C con /W4 /WX e ASan, compreso
rifiuto di stati e byte u32 fuori range. ASan strumenta il C, non il Rust
o Assembly. Non ci sono allocazioni/risorse possedute nel decoder; leak
RAM fisica non misurati e Valgrind non eseguito.

Cppcheck sul test C: nessun error/warning/performance/portability.
Restano informazioni sugli header di sistema, report dei checker e un
consiglio staticFunction per la funzione di prova, che deve restare
esterna per essere chiamata dal profilo kernel.

## Test nel kernel

Profilo isolato `-TestPs2Decoder`: stesso test ABI C chiama il gateway
Rust dentro il kernel dopo il ritiro dello stack loader. Produce
`KERNEL_PS2_DECODER_OK` soltanto dopo tutti i controlli; termina poi sul
normale `KERNEL_READY`. Il profilo non puo essere installato nella ESP
ordinaria o combinato con altri fault/loop diagnostici.

[Log diagnostico](../../../../build/ps2-decoder-firmware-final.log),
[risultato](../../../../build/ps2-decoder-firmware-5fbe62cdbcc84a47a2b2e1412311f53c/results.json):
dieci boot consecutivi, QEMU q35/TCG/OVMF, verifiche CPU/memoria del
harness scheduler e marker del parser in ogni boot. SHA256 immagine:
`B15F67FE53E31369FD11DA488D832F635B101DB94021AA9143A1E4EA68D4D54D`.
Byte sintetici: questa prova non dimostra che i8042 sia configurato
correttamente o che una tastiera reale funzioni.

Profilo ordinario: altri dieci boot consecutivi, stesso harness, marker
e stringhe delle prove decoder assenti dall'immagine e dai log.
[Log ordinario](../../../../build/ps2-decoder-ordinary-final.log),
[risultato](../../../../build/ps2-decoder-firmware-902011b99fb74cbe9565652c9e3bef84/results.json).
SHA256: `9B3B97DE9CDF5167310A3A09578F9E101B18B2500E3305E419D51B1FA5AF85CC`.
Le due immagini sono intenzionalmente distinte: nessuna prova sul parser
ordinario viene dedotta dal profilo diagnostico.

## Review

Il decoder non contiene unsafe, heap, globali, callback o puntatori C.
Lo stato resta nel consumer; il gateway converte soltanto valori interi
validati. Gli enum Rust non sono esposti come layout ABI. Errori consumano
il byte e cancellano il prefisso; il reset non recupera dati persi.
Identificatori opachi non equivalgono a tasti assegnati o Unicode.

Massimo sorgente del componente: 167 righe; massimo due sorgenti diretti
per cartella. Interfaccia header 19 righe, parser 115, gateway 11.
I soli hook esterni sono condizionali in main/build; nessuna modifica a
reuse/. Collegamento del controller e della task consumer rinviato al
successivo blocco dello stesso driver, con fonti/profilo e deadline.
Main resta a 201 righe e build a 293; stile, limite cartelle e link
verificati. [Hash sorgenti finali](../../../../build/ps2-decoder-source-final.json).

Riproduzione dalla root del progetto:

```powershell
& kernel/drivers/input/ps2/scripts/test.ps1
& kernel/drivers/input/ps2/scripts/firmware.ps1
& kernel/drivers/input/ps2/scripts/firmware.ps1 -Ordinary
```
