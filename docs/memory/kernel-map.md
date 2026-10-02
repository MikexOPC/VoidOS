# Mappa UEFI nel futuro spazio del kernel

`memory/final/mapping.*` collega il buffer persistente alle tabelle
inattive e completa la bozza del contratto di ingresso. Riceve risorse
gia possedute e vive; non trasferisce la proprieta e non le libera.
Non attiva CR3 e non chiama ExitBootServices.

## Mappatura e verifiche

La finestra virtuale parte da `0x0000500000000000`, con un limite di
1 MiB. Vengono mappate solo le pagine effettivamente allocate al buffer,
in sola lettura e non eseguibili. Le pagine oltre la capacita restano
assenti. Il kernel deve mantenere questa memoria riservata finche usa
la fotografia; il puntatore virtuale non possiede un secondo buffer.

Il piano ammette fino a 19 mappature: 16 segmenti ELF, contratto, mappa
e stack. La mappa ha un budget separato dal kernel. Gli altri intervalli
non possono attraversare la finestra riservata alla mappa.

`boot_extend_page_tables` verifica l'intero grafo esistente prima di
seguirne i puntatori. Controlla il piano esteso, limiti fisici, alias e
permessi, poi aggiunge le foglie usando lo stesso proprietario delle
tabelle. Non duplica la root. Nella disposizione attuale aggiunge tre
pagine di tabelle, cioe 12 KiB, per il percorso della nuova finestra.

Prima delle modifiche il contratto viene invalidato. Dopo le allocazioni
viene acquisita una nuova fotografia nello stesso buffer e ricontrollata
la copertura di kernel, stack, contratto, buffer e tutte le tabelle.
Il grafo completo viene verificato nuovamente. La firma e `Ready` vengono
pubblicati solo dopo questi controlli.

## Contratto versione 2

Il contratto passa da 72 a 112 byte. Il probe respinge la versione 1.
I primi campi conservano significato e posizione; i nuovi campi sono:

| Campo | Significato |
| --- | --- |
| `MemoryMapVirtual` | Indirizzo virtuale del buffer nelle nuove tabelle |
| `MemoryMapBytes` | Byte occupati dai descrittori della fotografia |
| `MemoryMapCapacity` | Capacita riservata e mappata, in byte |
| `MemoryDescriptorBytes` | Passo restituito dal firmware, non sizeof di una struttura locale |
| `MemoryDescriptorVersion` | Versione dei descrittori, attualmente accettata solo 1 |
| `MemoryReserved` | Zero |

MapKey resta interno al loader. `EnabledFeatures` resta zero perche
le tabelle sono inattive. Il probe controlla i metadati numerici senza
dereferenziare il buffer: la lettura dei descrittori nel kernel e un
passo successivo.

Questa e ancora una fotografia preparatoria nel percorso di prova. Attivita firmware successive
possono cambiarla; il futuro percorso ExitBootServices dovra aggiornare
la mappa e i campi del contratto usando la fotografia del tentativo
riuscito. Il [modulo terminale](../handoff/firmware/terminal-handoff.md)
implementa ora questo collegamento, verificato con firmware simulato.
Non basta conservare i valori stampati durante questo test.

## Errori e test

Se l'estensione fallisce a meta, root e validita delle tabelle vengono
invalidati, mentre le pagine gia allocate restano nel proprietario per
il cleanup. Un errore successivo invalida fotografia e contratto senza
perdere i proprietari. Argomenti o grafo iniziale non validi vengono
rifiutati prima della modifica. Non si puo usare una preparazione fallita
come transizione al kernel.

Nove scenari host verificano foglie R/NX, guardia assente, root invariata,
chiave aggiornata, fallimento di ciascuna delle tre allocazioni, errori
di refresh, buffer insufficiente, copertura errata, puntatore di tabella
estraneo e allocazione duplicata. Due ulteriori scenari respingono pagine allocate
su alias dei metadati del piano e della nuova mappatura senza scritture
o adozione di un secondo proprietario. Quattro verifiche respingono
finestra errata, permessi RW, alias fisico con una tabella e buffer oltre
1 MiB, conservando piano e tabelle esistenti. Il probe verifica dieci nuovi
contratti incompatibili. QEMU verifica il percorso completo nei due
casi positivi e il ritorno al firmware nei dieci scenari.

[UEFI: GetMemoryMap e ExitBootServices](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html).
