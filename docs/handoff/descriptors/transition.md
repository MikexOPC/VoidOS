# Blocco coerente di descrittori per la transizione

`handoff/descriptors/transition/bundle.c` espone tre funzioni:

- `boot_prepare_descriptor_bundle`: prepara GDT estesa, TSS, IDT e operandi
  in una bozza locale; verifica tutto prima di copiare e pubblicare la firma.
- `boot_validate_descriptor_bundle`: controlla firma, campi riservati,
  indirizzi incrociati, TSS disponibile e IST1 nei 32 gate.
- `boot_invalidate_descriptor_bundle`: revoca il preflight modificando solo
  la firma. Non azzera descrittori che potrebbero essere usati dalla CPU.

Il blocco e di 704 byte, allineato a 16. GDT e descrittore TSS sono contigui:
limite GDTR 39, selettori codice 8, dati 16, TSS 24. IDT/IDTR mantengono
32 gate e limite 511. Gli offset sono calcolati da oggetti validi; il layout
ha spazi riservati espliciti e non dipende da header CRT o struct packed.
Il contratto kernel v2 non cambia.

## Proprieta, indirizzi e pubblicazione

Il chiamante possiede output e profilo. Devono essere accessibili e non
sovrapposti. La base lineare dichiarata rappresenta una futura pagina
di 4096 byte e puo differire dall'indirizzo del buffer usato per costruire
il blocco: dopo una copia, le mappature devono far corrispondere tale base
ai byte preparati. La libreria non alloca e non prova questa corrispondenza.

Pagina descrittori e due stack da 16 KiB devono essere distinti, allineati
a pagina e nel profilo canonico basso. Le cime degli stack devono restare
canoniche. Gli ingressi non possono puntare nella pagina descrittori o
negli stack. Intervalli adiacenti sono ammessi; gli estremi finali sono esclusi.
Il controllo del singolo indirizzo ingresso non dimostra che l'intera
funzione sia mappata eseguibile: questa verifica appartiene al piano paging.

Errori di preparazione conservano l'output precedente. La firma viene
pubblicata per ultima dopo la verifica della bozza. Questo e un protocollo
di preflight a singolo chiamante, non una pubblicazione atomica fra CPU.
Non costruire o copiare il blocco sopra descrittori gia in uso.
La validazione e destinata alla fase prima di LTR: il busy scritto dalla
CPU nel descrittore TSS modifica il profilo disponibile e viene rifiutato.
Invalidare la firma non scarica GDTR/IDTR/TR e non autorizza a liberare memoria.

## Verifiche e limiti

I test host alterano tutti i 5.632 bit del blocco e i 2.240 bit del profilo,
verificano errori, alias, sentinelle, indirizzi estremi e invalidazione.
L'interfaccia e consumata anche da C++.

Il probe EFI ora usa direttamente queste tre funzioni, senza modificare
manualmente il limite GDTR o i gate. La sua pagina statica e allineata a
4096; dopo preparazione e controllo, revoca il preflight prima di LGDT,
LIDT e LTR. #DE, #UD e #GP verificano che la CPU usi ancora i descrittori
integri e passi allo stack IST1 anche dopo la revoca della firma.

Nel boot ordinario resta attiva la preparazione delle bozze precedenti.
Il percorso ordinario alloca ora anche il blocco in [pagine persistenti](storage.md),
con due stack, guardie non mappate e verifica di copertura. Ricostruisce
il grafo inattivo e aggiorna il preflight. Il [codice persistente](../transition/capsule.md)
contiene gli ingressi copiati e il trampoline completo. La build diagnostica
conserva il cleanup; `-BootKernel` esegue [uscita UEFI e ingresso ELF](../transition/activation.md)
con descrittori e paging propri. La validazione del blocco resta precedente
a LTR: dopo l'attivazione il descrittore TSS diventa busy.
Il profilo con IST1 comune a tutti i vettori e limitato all'arresto terminale:
non risolve eccezioni annidate o NMI sullo stesso stack. Il kernel ibrido
richiedera un proprio profilo di eccezioni e durata delle risorse.

Riferimento: [Intel SDM volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 6.14 e 8.2.3-8.2.4.
