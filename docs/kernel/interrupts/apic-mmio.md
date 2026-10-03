# Probe temporaneo LAPIC e I/O APIC

Il kernel legge identita e capacita dei controller prima di configurare
le rotte IRQ. Il profilo BSP e la MADT devono essere gia verificati.
Questo blocco non abilita gli interrupt e non scrive le redirection entry.

## Mapping e proprieta

Il probe prende in prestito la prima pagina della finestra temporanea:
`Active = 2` impedisce l'uso delle operazioni RAM durante il prestito.
Il mapping e supervisor, NX e UC secondo lo slot PAT corrente; LAPIC
e in sola lettura, I/O APIC permette la scrittura di IOREGSEL. Ogni
pubblicazione e revoca invalida il TLB con INVLPG. I bit Accessed/Dirty
possono cambiare, gli altri bit della PTE devono coincidere.

Non vengono allocate pagine o lease. L'inventario paging ordinario viene
verificato prima e dopo, a finestra vuota: non viene esteso per accettare
PTE dispositivi persistenti. Il probe rifiuta alias con foglie RAM o
pagine delle tabelle registrate nell'inventario.

Gli input sono strutture valide possedute dal kernel. L'output persistente
deve essere allineato e disgiunto da input, allocator, mappa firmware,
finestra e vista ricorsiva. Una bozza locale di 528 byte viene pubblicata
solo dopo le verifiche. Gli errori precedenti alla pubblicazione lasciano
l'output invariato; un errore di logging successivo restituisce fallimento
con una copia valida gia pubblicata e il chiamante termina con panic.

## Riserve e cache

Una regione firmware sovrapposta deve coprire tutta la pagina ed essere
Reserved oppure MMIO, senza Read/Write Protect. Gli attributi devono
essere assenti oppure includere la capacita UC. RAM, memoria ACPI e
coperture parziali sono rifiutate. Un descrittore assente e ammesso solo
per LAPIC confermato da MSR/MADT o I/O APIC standard a `0xfec00000`.
Questo e un limite del profilo iniziale, non una verifica di autenticita
del firmware ne una regola universale per ogni piattaforma.

IF deve essere zero; CR0.CD e CR0.NW devono essere zero. PAT e APIC_BASE
vengono riletti prima e dopo gli accessi. Con MTRR firmware validi, PAT
strong UC produce UC per i tipi MTRR architetturalmente previsti dalla
tabella Intel 11-7. Il probe non riscrive MTRR e non ne valida tutta la
configurazione. Cache disabilitata o profilo cambiato causano rifiuto.

## Registri e limiti

Gli accessi sono volatile a 32 bit. LAPIC ID e Version vengono letti
agli offset 0x20 e 0x30; l'ID attuale deve corrispondere al BSP.
Il profilo accetta LAPIC integrato con versione 0x10..0x1f e MaxLVT 3..6.
Per I/O APIC si salva IOREGSEL, si selezionano ID e Version e si leggono
attraverso IOWIN. Il selettore originale viene ripristinato e riletto
anche sui fallimenti recuperabili successivi alla sua acquisizione.
Non si scrivono IOWIN, LVT, SVR, EOI o rotte interrupt.

Le versioni I/O APIC supportate sono 0x11 e 0x20. Il conteggio e
MaxRedirectionEntry + 1, limitato a 120 ingressi per mantenere gli indici
delle future coppie di registri entro il selettore a 8 bit. Non e un
limite universale ACPI. Gli intervalli GSI sono disgiunti, con estremo
esclusivo a 64 bit: 2^32 e valido, il superamento viene rifiutato.
Override ISA espliciti e NMI globali devono ricadere in un solo intervallo.
Le rotte ISA implicite e i conflitti nella futura configurazione IRQ
richiedono ulteriori controlli.

Il backend native e interno al prestito verificato; non e una API MMIO
generica. Il modello resta uniprocessore con IF=0. Un fault hardware
entra nel gestore terminale esistente: non viene promessa ripresa dopo
page fault o machine check. Non sono state eseguite prove su hardware fisico.

## Test e prossimo passo

Il modello host verifica errori di lettura, revoca, selector restore,
alias, riserve, PAT UC alternativo, piu controller, GSI e output disgiunto.
Il suo inventario e una fixture: la verifica completa dell'inventario
viene esercitata dagli altri test host e dal kernel in QEMU.
Rust valida versioni e limiti numerici senza dereferenziare puntatori.
Il monitor QEMU confronta log, copia persistente e coda azzerata;
i controlli esistenti verificano la finestra vuota dopo il probe.

Seguono mapping dispositivi con durata adatta agli handler, configurazione
controller e ingresso IRQ con ritorno. Solo dopo si abilita una sorgente
controllata, ad esempio la seriale. Una MADT a quattro CPU non significa
che il kernel stia gia eseguendo SMP.

La configurazione mascherabile e il ritorno ring zero sono ora implementati
nel [blocco IDT/controller](irq-return.md). Il prestito e il preflight
MMIO sono condivisi dal probe e dalla configurazione; il callback rimane
interno, non rientrante e richiede preflight gia superato con IF=0.

## Riferimenti

[Intel SDM, Vol. 3A: PAT/MTRR, cache e APIC](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
[Intel 82093AA, IOREGSEL/IOWIN e registri ID/Version, copia del datasheet](https://datasheet4u.com/pdf-down/8/2/0/82093AA_IntelCorporation.pdf).
[QEMU, implementazione I/O APIC](https://github.com/qemu/qemu/blob/master/hw/intc/ioapic.c).
