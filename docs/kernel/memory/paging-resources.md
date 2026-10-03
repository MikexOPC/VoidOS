# Inventario delle tabelle paging attive

Il kernel legge il grafo PML4 -> PDPT -> PD -> PT tramite la vista
ricorsiva gia attiva. Il registro conserva indirizzo fisico, vista virtuale
e livello di ogni tabella distinta, insieme a CR3 e numero delle foglie.
La scansione comprende tutti i rami presenti del profilo iniziale, non
soltanto quello dei mapping kernel. Il riferimento ricorsivo del PML4
e verificato separatamente e non seguito come un nuovo ramo.

## Controlli prima degli accessi

Le pagine devono essere allineate, entro MAXPHYADDR e interamente
contenute in un solo descrittore EfiLoaderData ordinario con capacita WB.
Nessuna tabella puo sovrapporsi a un extent libero o a un lease vivo
dell'allocatore. La mappa e ricontrollata per overflow e sovrapposizioni;
il totale della RAM candidabile deve coincidere con quello conservato
dall'allocatore. Duplicati fisici, viste duplicate e cicli sono respinti
prima di leggere il figlio. Una foglia ordinaria non puo mappare una
pagina di paging come dati. Il backend non scrive PTE, registri o TLB.

Rust controlla esclusivamente valori scalari: formati delle voci e
matematica delle viste ricorsive. Non riceve puntatori e non usa unsafe.
I flag ammessi sono una politica piu restrittiva di Intel SDM: supervisor,
pagine da 4 KiB, nessun bit software/cache aggiuntivo, nessuna pagina
scrivibile ed eseguibile. A/D sono ignorati nei confronti; le voci assenti
devono essere completamente zero. Huge pages, LA57 e metaformati futuri
richiederanno un aggiornamento esplicito del profilo e dei test.

## Durata di vita e limiti

Il registro di 3096 byte occupa un oggetto nell'arena persistente gia
esistente. Non richiede nuovi frame. Viene creato dopo il passaggio allo
stack kernel e la revoca del vecchio mapping dello stack loader.
La scansione usa una bozza locale, pubblicata soltanto dopo il successo;
un fallimento lascia l'output precedente intatto.

Verify ricostruisce la topologia e il numero delle foglie. Non conserva
indirizzo e permessi di ogni singola foglia: modifiche valide che lasciano
invariati grafo e conteggio non vengono rilevate. Gli altri mapper
continuano a verificare le proprie PTE. Dopo un cambiamento del grafo
o del numero dei mapping occorre acquisire una nuova fotografia.

Massimo 128 tabelle, CPU singola, IF=0, nessun writer concorrente.
Gli input C devono essere buffer leggibili e prestiti stabili; i controlli
di alias non sostituiscono l'isolamento da codice kernel ostile.
Le tabelle ereditate rimangono riservate. Essere presenti nel registro
non autorizza rilascio, adozione nell'allocatore o reclamazione del loader.

## Riferimenti

- [Intel SDM 3A, tabelle 5-15, 5-17, 5-19, 5-20 e sezione 5.7](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf).
- [UEFI 2.11, tipi di memoria e AllocatePages](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html).

Il successivo lavoro verso un driver a interrupt comprende scoperta
della piattaforma, mapping MMIO con attributi corretti, controller IRQ
e handler capaci di ripristinare il contesto e tornare all'esecuzione.
La seriale in polling esistente e gia un backend hardware minimo;
non richiede scheduler o processi utente per trasmettere diagnostica.
