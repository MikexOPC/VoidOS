# Revisione della task allocatore fisico

Data: 2 ottobre 2026. Base: 0be622c489bc423423e0597154176aa224732c8b.

## Risultato e confini

Sono implementate inizializzazione, assegnazione contigua e rilascio in
kernel/mm/physical/, con validazione delle invarianti prima delle mutazioni.
Rust decide la compatibilita degli intervalli con CPU, tipo e attributi;
x86 legge la larghezza fisica CPUID preservando RBX. Nessuna modifica al
bootloader, nessuna reclamazione di LoaderData/LoaderCode o boot services.

L'assegnazione fisica non rende la RAM accessibile nel grafo virtuale
attivo: il kernel non dereferenzia, azzera o espone le pagine assegnate.
Il passo successivo e mapping e azzeramento prima dell'uso.

L'owner occupa 6192 byte in BSS, entro l'assert di 8 KiB; non usa heap,
bitmap proporzionali alla RAM o grandi bozze sullo stack. Supporta 128
descrittori candidati, 256 intervalli liberi e 64 assegnazioni vive.
L'ELF finale e di 20480 byte; le sezioni attuali occupano sei pagine,
24 KiB, escluse stack e risorse del loader. Dati/BSS prima di arrotondare:
6352 byte. Queste sono dimensioni dell'immagine, non picco totale di RAM.

## Invarianti e revisione

- Solo ConventionalMemory con capacita WB, senza attributi speciali,
  con base non nulla e interamente entro il limite CPU e gestita.
- Gli intervalli fuori limite sono esclusi interamente. Oltre 128 regioni
  candidate l'inizializzazione fallisce senza selezione silenziosa.
- Free list ordinata e coalescente, nessuna sovrapposizione libera/viva,
  somma esatta delle pagine e ID vivi distinti sono verificati.
- Output vuoto e distinto dall'owner; rifiuti non modificano i metadati.
- Rilascio esatto, handle obsoleti e doppi rilasci respinti; gli ID non
  vengono riusati, e il limite viene respinto prima del wrap.
- Owner non spostabile e non reinizializzabile. Le copie di un handle vivo
  non sono capability sicure: la prima liberazione rende obsolete le altre.
- La revisione ha aggiunto verifica finale e rollback dei metadati
  parziali nell'inizializzazione, prima di restituire successo.

Gli oggetti C devono essere leggibili/scrivibili e posseduti dal chiamante;
le API non sono una barriera contro corruzione arbitraria o chiamanti ostili.
La mappa resta immutabile e l'owner gia riservato durante la costruzione.
Questa fase e per un solo chiamante e processore, senza SMP o rientro.

## Prove completate

Build /W4 /WX e Rust -D warnings riuscite, senza CRT o import.
Test host: coalescenza, handle alterati, double free, riuso con ID diverso,
owner sbagliato, alias, output occupato, corruzione dei conteggi, limite ID,
esaurimento dei 64 record e di tutte le pagine, frammentazione contigua,
CPU limit, runtime e 129 regioni respinte. Anche 1024 sequenze riproducibili
di assegnazioni variabili e rilasci, con modello indipendente di indirizzi
e contabilita, sono passate insieme alle regressioni precedenti.

Quattro test Rust includono la nuova politica CPU/WB e tutti i bit speciali.
Il convertitore passa l'immagine valida e respinge sette mutazioni invalide.
QEMU verifica tre casi sull'ELF installato: ready con 128/256 MiB e panic
intenzionale. Ogni kernel assegna due blocchi, libera e riusa il primo,
respinge il suo handle obsoleto, libera tutti i blocchi e verifica il
ripristino del budget prima di KERNEL_PHYSICAL_OK.

## Evidenze

Gli artefatti ignorati restano in kernel/build/. Directory QEMU finale:
firmware-6318b92b2695416fb8eea832fd000aff. Gli SHA-256 identificano queste
esecuzioni, non una promessa di build riproducibili.

| Artefatto | SHA-256 |
| --- | --- |
| kernel.elf installato e provato | 4057DE1B96FAD5D4BC3876FE073ECE61A2262738A9E3F94A967A332AB93009D3 |
| physical-build.log | 86C635F282DC5691F6AA2650ED26F9B61935D4F8734879220FA691289ABE0A02 |
| physical-host.log | 0F13895ECE3024C5AD434D87FEC89B8426B89AD2E20D626B4E03159DDCD38E7B |
| physical-image.log | EF14CA12161259FF99B0A632C721ABE715023BA5EEB10051ED339DFA467F5CBD |
| physical-firmware.log | 8220B9CABA13506B157F1C19CD188913051CAA7B89A473B63747DFE91CF90B10 |

Limiti: QEMU/EDK2 q35 CPU max TCG; hardware fisico e profili cifrati/key-ID
non verificati. Niente mapping, zeroing, heap, reclamazione firmware, SMP
o benchmark. I controlli completi hanno costo quadratico sui budget fissati.
La task di ownership fisica e completata; la maturita del kernel ibrido
non si misura contando questi test.

[API, comandi e limiti](../kernel/memory/physical-allocator.md),
[UEFI 2.11](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel CPUID](https://cdrdv2-public.intel.com/774475/252046-sdm-change-document.pdf).
