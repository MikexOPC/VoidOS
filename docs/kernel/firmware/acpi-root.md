# Lettura della directory XSDT/RSDT nel kernel

Il kernel usa la copia RSDP del contratto v3 per scegliere XSDT, oppure
RSDT nel formato legacy. Non effettua fallback quando una XSDT presente
e malformata. Le voci sono indirizzi fisici numerici; non sono puntatori
virtuali utilizzabili direttamente.

## Finestra fisica temporanea

`x86_page_window_read_acpi` presta la finestra gia disponibile senza
assegnare lease, allocare frame o azzerare la memoria firmware. Accetta
un buffer scrivibile di almeno `bytes` byte; il chiamante ne possiede
la durata di vita. Contesto, mappa, owner e allocatore restano prestiti.
Gli output sovrapposti ai metadati o alla finestra sono respinti.
La mappa e il contratto vengono verificati prima di modificare PTE.

Si richiede una riserva ACPI reclaim o NVS, WB e non read-protected.
L'intero intervallo arrotondato a pagina deve rientrare in un singolo
descrittore: non basta che siano coperti i soli byte richiesti.
Input nulli, MMIO, overflow, CPU limit, mappe sovrapposte e finestre
occupate falliscono prima della pubblicazione di una foglia.
Il limite di ogni richiesta e 64 KiB.

Una sola PTE viene usata: present bit 0, RW e US assenti, NX bit 63;
il bit accessed 5 puo cambiare durante la lettura. Gli altri slot e
le guardie restano vuoti. Dopo scrittura della PTE viene eseguito
INVLPG; dopo copia, la PTE viene azzerata e nuovamente invalidata.
Il buffer di output puo essere parziale se un errore avviene dopo
l'inizio della copia; il parser usa una bozza e non la pubblica.

Profilo corrente: CPU singola, IF=0, nessun utilizzatore concorrente
o handler NMI che usi la finestra. Il prestito non e un lock per SMP.
Le tabelle ACPI rimangono riservate dall'allocatore fisico corrente;
prima di reclamarle occorrera rivedere tutti i riferimenti conservati.
La gestione cache eredita il profilo di boot delle finestre WB.

## Parser C e validatori Rust

Il parser legge prima i 36 byte dell'header SDT. Rust controlla firma,
revisione 1, lunghezza minima, divisibilita del payload per 4/8 e
limite di 256 voci. La lunghezza massima XSDT e 2084 byte; RSDT 1060.
Formati o capacita maggiori vengono respinti dal profilo iniziale.

La seconda lettura copia tutta la tabella, confronta ogni byte
dell'header con la prima lettura e verifica il checksum complessivo.
Le voci sono decodificate byte per byte, senza allineamenti imposti
ai dati firmware. Indirizzi nulli, oltre MAXPHYADDR, duplicati o
sovrapposti alla root sono respinti. Per ogni indirizzo numerico deve
essere rappresentabile almeno l'intero header figlio da 36 byte.
Le tabelle figlie non vengono ancora lette, ne validate dalla directory.

La pubblicazione avviene solo dopo tutte le verifiche. Gli slot non
usati vengono azzerati. `kernel_acpi_directory_valid` ricontrolla
formato, conteggio, limiti e coda della fotografia; non autentica
indirizzi modificati in altri indirizzi strutturalmente validi.
Il checksum della tabella originale non e una firma crittografica.

Il lettore callback deve rispettare la dimensione e scrivere solo nel
buffer fornito; il suo contesto non deve dipendere dall'output finale.
Le API C richiedono oggetti leggibili/scrivibili e correttamente
allineati per il proprio tipo. Nessuna API recupera da un page fault
del chiamante o offre isolamento da firmware ostile/SMM.

## Stato persistente e costo

La directory occupa 2072 byte nell'arena esistente, a allineamento 8.
L'handle viene conservato dal kernel per il blocco successivo.
Gli oggetti persistenti passano da 3296 a 5368 byte; nessun nuovo
frame viene allocato per la directory o per le letture temporanee.
Il kernel verifica nuovamente il registro paging dopo la discovery.
La scansione completa della mappa privilegia la verifica: per ora
non e un percorso di lettura ad alta frequenza.

I frame locali della copia e della bozza sono separati, ciascuno sotto
4 KiB, mantenendo lo stack principale da 64 KiB e i controlli del
compilatore. Non si disabilitano gli stack probe per aggirare il link.

## Prossimo blocco

La discovery MADT e ora implementata con lo stesso lettore; il contratto
e descritto in [madt.md](madt.md). Il prossimo blocco deve verificare
identita del BSP, modalita APIC e pagine MMIO prima di configurare IRQ.
Controller IRQ e MMIO non sono ancora programmati; IF resta zero.

## Specifiche

- [ACPI 6.5, sezioni 5.2.7 e 5.2.8](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html?highlight=gsi).
- [Intel SDM, Volume 3A, capitolo 4 e invalidazione delle traduzioni](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
