# TSS e stack alternativo del boot

`arch/x86/lib/descriptors/tss.c` costruisce e verifica un TSS x64 di
104 byte e il descrittore GDT di 16 byte: present, ring 0, tipo disponibile
0x9, limite 103, granularita in byte. Gli altri campi sono zero.
RSP0 e IST1 contengono le cime di due regioni distinte di 16 KiB.
L'offset della bitmap I/O e 104, oltre il limite: la bitmap e assente.
Questo non limita gli accessi I/O del codice ring 0 con IOPL sufficiente.

Gli oggetti sono codificati per byte, senza struct packed o accessi a
interi non allineati. La base TSS deve essere allineata a 16 e gli stack
a pagina. TSS, regioni stack e loro cime devono rientrare nel profilo
canonico basso a quattro livelli. Le regioni non possono sovrapporsi.
Tutti gli argomenti vengono controllati prima delle scritture.

Il chiamante deve possedere buffer accessibili, garantire la durata delle
regioni e verificarne le mappature. La libreria non alloca gli stack,
non aggiunge guard page, non espande la GDT attiva e non esegue LTR.
Il verificatore accetta il descrittore disponibile, prima di LTR: il bit
busy impostato dalla CPU rende quel descrittore diverso dal profilo iniziale.
La GDT dovra essere scrivibile durante LTR; non bisogna azzerare il bit
busy di un TSS in uso per riutilizzare il verificatore.

## Test

La build esegue `test/handoff/descriptors/tss_test.c`: oracolo byte del
descrittore, decodifica indipendente di RSP0/IST1, campi riservati,
960 mutazioni di bit, alias degli output, limiti, stack sovrapposti e guardie.

Il [probe QEMU delle eccezioni](exceptions.md) ora prepara anche un TSS,
due stack statici di 16 KiB e una GDT estesa a 40 byte nell'immagine di test.
Il selettore TSS e 24. Dopo LGDT/LIDT, LTR carica il TSS. Il
probe viene ora preparato dal [blocco coerente della transizione](transition.md),
senza modifiche manuali ai descrittori. Ogni gate del
probe usa IST1. #DE, #UD e #GP arrivano allo stop con RSP nello stack
alternativo, IF=0 e HLT=1. Il monitor verifica TR=24 e il bit busy nella
GDT in memoria. #GP usa il selettore inesistente 0x28 e conserva tale
error code sullo stack IST1.

I due stack statici e il TSS appartengono solo all'immagine sacrificabile
di test. Il boot prepara separatamente [due allocazioni persistenti](storage.md),
48 KiB comprendenti descrittori, due stack, guardie e codice. La build
diagnostica non esegue LTR; `-BootKernel` lo esegue nella transizione reale.
Il probe isolato usa ancora il paging firmware; il test kernel separato
copre cambio CR3 e #PF con IST1, incluse guardia e NX. Restano esclusi
double fault e NMI annidati. Uno stesso IST riutilizzato da
eccezioni annidate puo sovrascrivere un frame: il futuro profilo definitivo
dovra distinguere le eccezioni che richiedono stack separati.

Riferimento: [Intel SDM volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 6.14.5, 8.2.3, 8.2.4 e 8.7.
