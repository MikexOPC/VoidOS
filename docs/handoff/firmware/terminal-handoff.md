# Uscita UEFI collegata al contratto del kernel

`handoff/firmware/terminal/handoff.*` collega la sequenza ExitBootServices
alla mappa persistente R/NX e al contratto versione 2. `BOOT_EXIT_HANDOFF`
contiene prestiti di kernel, stack, capacita CPU, tabelle, pagina del
contratto e proprietario della mappa, oltre al prestito opzionale Descriptors
delle undici pagine EfiLoaderData e della pagina distinta EfiLoaderCode.
Non e un nuovo proprietario.

Gli oggetti devono essere distinti, validi, vivi e non modificati da altri
durante la chiamata. Il codice viene eseguito con il paging del firmware;
le tabelle del kernel restano inattive per tutta questa fase.
La chiamata terminale richiede TPL_APPLICATION. Lo stato di uscita
deve essere distinto anche dal contesto, dai proprietari, dalle risorse
fisiche e dalla tabella dei servizi: il modulo controlla le sovrapposizioni
prima di scrivere lo stato o chiamare il firmware.

## Verifica prima dell'uscita

`boot_validate_exit_handoff` ricostruisce il piano completo e verifica
il grafo delle tabelle. Controlla copertura EfiLoaderData senza attributo
runtime per kernel, stack, tabelle, contratto e buffer della mappa. Quando
Descriptors e presente verifica anche blocco, codice, due allocazioni, quattro mappature
e guardie non presenti. Il controllo degli alias dello stato comprende
il nuovo owner e le sue pagine; anche ogni nuova fotografia dei tentativi
terminali deve coprirli.
Confronta la bozza del contratto con questi oggetti: root, ingresso,
stack, indirizzo virtuale, capacita, byte usati e formato dei descrittori.
Richiede Ready e firma validi, con EnabledFeatures ancora zero.

Questa verifica non alloca, non modifica il contratto e non usa servizi
firmware. Il percorso reale UEFI la esegue dopo la preparazione della
mappa. La build diagnostica effettua poi cleanup e ritorno al firmware;
`-BootKernel` usa invece la sequenza reale e la transizione persistente.

## Fotografia di ciascun tentativo

`boot_exit_memory_handoff` esegue la verifica iniziale, poi usa il percorso
controllato del modulo ExitBootServices. Ogni tentativo:

1. Aggiorna nello stesso buffer con GetMemoryMap.
2. Ricontrolla tutte le risorse e le foglie delle tabelle.
3. Rigenera la bozza e allega dimensione, capacita, stride e versione
   della fotografia appena ricevuta.
4. Chiama ExitBootServices con la relativa MapKey, anche se zero.

Solo EFI_INVALID_PARAMETER permette un nuovo tentativo, fino a tre.
Il controllo intermedio usa solo memoria gia disponibile: non alloca,
non libera, non stampa e non chiama protocolli firmware. Il passo dei
descrittori viene usato come restituito, senza assumere sizeof locale.

`boot_attach_memory_snapshot` centralizza la pubblicazione dei metadati.
Richiede una bozza valida e una fotografia valida in un buffer distinto;
respinge alias del contratto con buffer o metadati della mappa. Non prova
da solo che la mappatura esista: il produttore e il percorso terminale
la verificano prima. Su input rifiutati non modifica l'output.

Il controllo del percorso base riceve una fotografia in sola lettura e
un contesto preso in prestito. Deve rispettare gli stessi divieti di
servizi e di allocazione, anche dopo un tentativo fallito. Modifiche ai
metadati della fotografia vengono respinte e ripristinate; la chiave
alterata non viene passata a ExitBootServices. Lo stato di uscita non
puo risiedere nel buffer o nei metadati della mappa.

## Risultato e durata delle risorse

Con Exited, mappa e contratto descrivono la fotografia del tentativo
riuscito e tutte le risorse restano possedute e vive. EnabledFeatures
rimane zero: l'uscita non attiva NXE, WP o CR3. Il codice di
[attivazione](../transition/activation.md) verifica NXE/WP, pubblica i relativi
bit, cambia CR3 e rilegge la root prima della CALL al kernel.

Un errore durante la sequenza invalida il contratto senza perdere i
proprietari. Un errore iniziale di preflight non modifica la bozza.
Una nuova chiamata con stato Restricted o Exited viene respinta senza
chiamate firmware o modifiche dello stato gia registrato.

In Available il chiamante puo ancora effettuare il normale cleanup.
In Restricted o Exited sono vietati il normale ritorno all'ingresso UEFI,
la console e i rilasci firmware. Il chiamante `-BootKernel` arresta la CPU
in Restricted e usa il codice persistente in Exited.

## Verifiche e limite attuale

Sedici scenari host controllano uscita immediata, due sequenze di retry,
chiave zero, dimensione e stride variabili, chiave sempre scaduta, errori
prima e dopo il primo tentativo, mappa troppo grande, copertura errata,
grafo alterato, servizi incompleti e rientro vietato. Il mock verifica
che il contratto coincida con la fotografia prima di ogni ExitBootServices
e segnala qualunque allocazione o rilascio durante la sequenza.
Due test del percorso base verificano anche alias dello stato e
alterazione della chiave da parte di un controllo difettoso.
Quattro ulteriori regressioni verificano stato dentro contratto,
pagina delle tabelle, buffer della mappa e tabella dei servizi:
nessuna chiamata firmware e nessuna modifica dei byte originali.

I dieci scenari QEMU diagnostici mantengono CR3 invariato. Il test kernel
separato verifica ExitBootServices reale, stato Exited e LastStatus zero
letti dal monitor, nuova root e ingresso ELF, oltre a tre fault di memoria.
Hardware fisico non e ancora verificato.

[UEFI 2.11: GetMemoryMap e ExitBootServices](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html).
