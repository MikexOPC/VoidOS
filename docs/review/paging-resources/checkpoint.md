# Checkpoint: inventario delle risorse paging attive

## Risultato e revisione

Il kernel censisce tutti i rami paging presenti del profilo iniziale e
conserva una fotografia nell'arena persistente. La scansione iterativa
verifica la riserva fisica prima di leggere ogni tabella e respinge
cicli, duplicati, alias ordinari verso pagine di paging, huge pages,
flags non supportati e indirizzi oltre MAXPHYADDR. Il riferimento
ricorsivo intenzionale del PML4 viene verificato ed escluso dalla visita.

Rust verifica i valori scalari e la costruzione delle viste, senza unsafe.
Il C mantiene espliciti i prestiti di contesto, mappa e allocatore.
Una bozza locale impedisce pubblicazioni parziali. Verify controlla
anche record inutilizzati, struttura delle viste, grafo e conteggio;
non fotografa contenuto o permessi di ogni singola foglia.

La revisione ha aggiunto due controlli: coerenza del totale fisico tra
mappa UEFI e allocatore, e rifiuto delle foglie che espongono tabelle
come normali dati. Sono stati corretti una copia di struttura che
generava memcpy e il percorso relativo di un test Rust. Nessun CRT,
import, TLS o rilocazione e richiesto dall'immagine finale.

## Validazione sullo stato finale

- Regressione host completa: `kernel/build/paging-host.log`.
- Nuova suite: errori senza modificare l'output, cicli, duplicati,
  alias, riserve fisiche, metadati corrotti e null input.
- Capacita: 128 tabelle accettate, la centoventinovesima respinta.
- 24768 alterazioni dei bit del registro, tutte respinte.
- 25 test Rust complessivi superati, inclusi due nuovi test su
  flag, limiti CPU e formule indipendenti delle viste ricorsive.
- C11 `/W4 /WX`, MASM `/W3 /WX`, Rust `-D warnings`.
- Convertitore ELF: immagine valida e sette mutazioni respinte.
- 22/22 scenari QEMU, nessun triple fault: RAM 128/256 MiB,
  panic, mapping, stack revocati, guardie, NX, NMI, double fault,
  fault annidato e ritorno imprevisto.
- Il monitor legge il registro dopo revoca dello stack loader:
  indirizzo dentro l'arena, allineamento, CR3, conteggio e Ready corretti.

Log firmware: `kernel/build/firmware-14ad25d403434e678b35b23b12b4c1b4/`.
Build: `kernel/build/paging-build.log`; conversione:
`kernel/build/paging-image.log`; firmware: `kernel/build/paging-firmware.log`.
ELF finale costruito, installato e copiato nei casi ready con SHA-256:
`6410D1A47D0140DBE23FE95AB42B618F7C46125A5BFFA58902851E226BA0B9A5`.
EFI invariato:
`07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3`.

## Costi e limiti

Registro 3096 byte; oggetti vivi nell'arena da 144 a 3240 byte.
Nessun nuovo frame allocato per il registro. Nel caso ready il grafo
contiene 15 tabelle (60 KiB riservati ereditati) e 106 foglie ordinarie.
ELF su disco 65536 byte; quattro segmenti residenti arrotondati a pagina
69632 byte, una pagina in piu rispetto al checkpoint precedente.
Modulo C principale 202 righe, test C 201, main 347.

CPU singola, IF=0, nessuna modifica concorrente delle tabelle.
La fotografia deve essere rinnovata dopo modifiche al grafo o al
numero dei mapping. Non e un'autorizzazione al rilascio del loader.
Hardware fisico ancora non validato; non si attribuisce una percentuale
di completamento all'intero OS o alla sicurezza del kernel ibrido.

## Prossimo blocco verso il primo driver a interrupt

La seriale COM1 in polling e gia un backend hardware minimo. Il kernel
esegue codice senza servizi UEFI, dispone di memoria e stack propri,
ma gli handler attuali delle eccezioni sono terminali: non tornano al
codice interrotto. Non ci sono controller IRQ, scheduler o user space.

Il prossimo blocco proposto e la discovery ACPI: individuare RSDP dalla
ConfigurationTable UEFI, controllare firme, lunghezze e checksum e
consegnare al kernel dati con durata di vita esplicita. Seguiranno parsing
MADT, mapping MMIO controllati, controller degli interrupt, ingresso e
ritorno IRQ con preservazione del contesto e ricezione seriale testata.
Stima orientativa: 4-6 task mirate per questo percorso su QEMU, da
rivedere dopo discovery e scelta precisa del dispositivo. Un driver
in polling su piattaforma nota puo richiedere meno prerequisiti.

Il primo driver puo inizialmente vivere nel kernel; isolamento in
processi e modello ibrido richiedono progettazione successiva, non sono
proprieta gia ottenute usando Rust o dividendo i moduli.

[Contratto e limiti dell'inventario](../../kernel/memory/paging-resources.md).
[UEFI: ConfigurationTable](https://uefi.org/specs/UEFI/2.11/04_EFI_System_Table.html).
[ACPI 6.6: RSDP e MADT](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html).
