# Inventario video e tabelle kernel — 10 ottobre 2026

Scelta concordata: un framebuffer e LAPIC, BSP e lease pinned.
Modulo in sola lettura; non alloca tabelle, non modifica PTE, PAT o TLB
e non scrive pixel. Le API precedenti restano disponibili e non autorizzano
implicitamente video o tabelle kernel.

## Contratto e ownership

`capture_video` e `verify_video` ricevono un owner LAPIC live, un'autorita
video esplicita e un callback di riscontro hardware. Nessun callback o
puntatore da dereferenziare viene recuperato dallo snapshot.
Gli input devono essere prestiti kernel leggibili e stabili, con IF=0
sul BSP; non sono puntatori utente e IF=0 non e un lock SMP.

Il callback del futuro mapper deve verificare prima e dopo la visita:
root corrente, paging a quattro livelli, prerequisiti PAT/NX e NXE/WP,
profilo cache CPU e PAT corrente uguale al valore dichiarato. Deve essere
in sola lettura, senza reentrancy o mutazioni concorrenti del grafo/allocator.
Il callback host e un mock, non dimostra questi prerequisiti hardware.

Il framebuffer deve coincidere con Base/Capacity/Used dell'handoff validato.
Si coprono le pagine di Used; l'intera Capacity arrotondata viene esclusa
dagli alias RAM/MMIO. Sovrapposizioni con loader, RAM libera, lease live
o LAPIC sono respinte. Non si adotta e non si libera memoria video.

Ramo dedicato PML4[225], prima foglia a `(225 << 39) + 4096`:
prima pagina assente, pagine video contigue, guardia finale assente.
Ogni foglia presente del ramo deve appartenere esattamente al video.
Le guardie proteggono pagine complete, non gli estremi parziali:
il futuro writer deve comunque controllare ogni offset pixel.

Ogni tabella nuova e una pagina di un lease pinned, riscontrato contro
l'allocatore. Massimo undici tabelle e 4321 foglie video; le altre tabelle
rimangono EfiLoaderData WB. Tutte le tabelle del ramo video devono essere
kernel-owned; nessuna tabella owned puo apparire in un altro ramo.
Tabelle mancanti, inutilizzate, duplicate o mappate come dati sono respinte.

Lo snapshot passa da3144 a3296 byte, senza modificare l'ABI boot v4.
Mantiene il prefisso e aggiunge record video e undici ID dei lease.
Il campo storico `Tables[].Reserved` vale zero per tabelle loader e
indice lease+1 per le tabelle video. Gli ID conservati rifiutano uno
snapshot vecchio anche dopo rilascio/riallocazione allo stesso indirizzo.
Output pubblicato soltanto dopo visita e riscontri finali riusciti;
un errore lascia l'output precedente intatto. Nessuna revoca e compiuta
dall'inventario: revoca, invalidazione e rilascio restano del mapper.

## Formato CPU e fonti

[Intel SDM 253668-090, febbraio2026](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):
tabella5-20 e §5.6 per le PTE4KiB; Pbit0, RWbit1, U/Sbit2 disattivato,
XDbit63 e prerequisito NXE. A/Dbit5/6 possono cambiare; gli altri flag
fuori dal profilo sono respinti. §14.12.2 e §14.12.3/tabella14-11:
PATbit7, PCDbit4, PWTbit3 selezionano uno degli otto slot PAT; UC=0,
WB=6, encoding riservati respinti. Richiesti PAT[0]=WB e slot scelto=UC,
non soltanto PCD impostato. Nessuna scrittura globale di PAT/MTRR.

[Linux6.6, device memory mapping modes](https://www.kernel.org/doc/html/v6.6/driver-api/device-io.html#device-memory-mapping-modes):
riferimento per distinguere device memory, cache e ownership delle mapping.
Non sono state copiate API o implementazioni. WC, SMP e DMA sono esclusi.

## Verifiche e limiti

Comando host: `arch/x86/mm/paging/scripts/test.ps1`.
Sei casi Rust debug/release, warning come errori e Clippy; controllo
di ogni foglia fino al limite4321 e alias dell'apertura fino a8193 pagine.
Otto gruppi C ripetuti100 volte con MSVC /W4 /WX e ASan:
errori di authority, output conservato, permessi alterati, guardie,
alias, foglie mancanti, metadati corrotti, lease duplicati/unpinned e ID
stantii. Il test di riuso conferma la stessa base fisica con un nuovo ID.
Accounting mock:125 pagine libere con tre tabelle pinned,128 dopo cleanup.
ASan strumenta C host; non Rust, firmware o RAM fisica. Valgrind e
coverage linee/branch non misurati. Nessuna allocazione runtime aggiunta.

[Log host finale](../../../../../kernel/build/paging-owned-final.log),
[cppcheck](../../../../../kernel/build/paging-owned-final/cppcheck.xml),
[manifest sorgenti](../../../../../kernel/build/paging-owned-review.json).
Cppcheck: nessun error/warning/performance/portability; le informazioni
sugli include di sistema e lo scope di una variabile sono distinte dai
warning del compilatore. Non attestano una review hardware live.

Il comando `scripts/firmware.ps1` ricompila se non riceve una directory
e prova dieci boot ordinari con video metadata, poi dieci senza display.
Campagna finale:10/10 con video e10/10 senza, nessun fault inatteso.
Kernel SHA256:2D5C645F11D7FD8F543B287331E0DC6BDA839F1678804312AEBE31FA8F696FA9.
Manifest delle due campagne:
[video](../../../../../kernel/build/framebuffer-firmware-2cf78f29c1314636adade266a3b9cd7c/results.json),
[assente](../../../../../kernel/build/framebuffer-firmware-2896194a96cf49a99e6b57a09c306c4e/results.json).
La suite host kernel completa passa anche sullo stato finale:
[log](../../../../../kernel/build/paging-owned-kernel-host.log).
OVMF/ESP e immagine identificata, KERNEL_READY, CPU capture e assenza di
fault inattesi sono verificati dal provider posseduto esistente.
Questa e regressione del percorso RAM/LAPIC sulla nuova struttura:
non e una prova del mapping video live o del suo callback hardware.
Risultati della campagna nel log `kernel/build/paging-owned-firmware.log`.

Resta aperta l'integrazione con il mapper video reale: preparazione delle
tabelle, pin, pubblicazione, invalidazione, revoca/rollback e riscontro
hardware delle nuove API. Fino a quella prova non si dichiara completato
il gate del mapping framebuffer. Hardware fisico non testato.
