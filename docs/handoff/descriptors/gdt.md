# Descrittori x64 preparati dal boot

La GDT descrive i segmenti usati dai selettori della CPU. Il primo passo
prepara una bozza di tre voci: segmento nullo, codice ring 0 a 64 bit e
dati ring 0. Non carica GDTR, non cambia CS/SS e non esegue il kernel.

## Formato e verifica

La libreria C riutilizzabile e `arch/x86/lib/descriptors/gdt.c`.
Espone preparazione e verifica tramite `gdt.h`, consumabile anche da C++.
I descrittori occupano 24 byte; il futuro operando LGDT e una sequenza
little endian di 10 byte, con limite 23 e base a 64 bit. La codifica a byte
evita padding, bitfield dipendenti dal compilatore e letture non allineate.

| Voce | Selettore | Proprietà |
| --- | --- | --- |
| Nulla | 0 | Tutti i bit a zero |
| Codice | 8 | Present, ring 0, execute/read, L=1, D=0 |
| Dati | 16 | Present, ring 0, read/write, L=0, B=1 |

Il bit accessed e gia impostato nelle due voci non nulle: evita che il
futuro caricamento del selettore richieda al processore di impostarlo.
La GDT non sostituisce i permessi delle pagine e non implementa W^X.

La base deve essere non nulla, allineata a 8 byte e l'intera GDT deve
rientrare nella meta canonica bassa del profilo a quattro livelli.
Questo e un limite del nostro profilo, non un limite generale di x64.
Gli output devono essere oggetti accessibili, distinti e posseduti dal
chiamante. Gli argomenti invalidi vengono rifiutati senza modifiche.
La verifica controlla l'esatto profilo supportato; non e un parser di
GDT arbitrarie. La base numerica non dimostra che le pagine siano mappate.

## Uso corrente e durata

`handoff/descriptors/prepare.c` prepara e ricontrolla la bozza sullo stack
di `efi_main`, poi stampa il successo. La bozza vive fino al ritorno al
firmware. Non usa AllocatePages e non viene aggiunta al contratto v2.
Non si puo usare questa bozza dopo un cambio CR3. Il boot prepara anche
una [GDT estesa persistente](storage.md), con operandi e mappature proprie,
che resta inattiva in attesa del trampoline.

Sono ora presenti anche [IDT e ingressi terminali](exceptions.md), preparati
ma inattivi nel boot ordinario. [TSS/IST](tss.md) sono verificati nel probe
separato e preparati nelle nuove pagine persistenti del percorso ordinario;
mancano caricamento dei descrittori nel passaggio finale e trampoline.
Questa bozza e un prerequisito verificabile,
non una transizione hardware pronta all'uso.

## Test

La build standard esegue `test/handoff/descriptors/gdt_test.c`: controlla
i byte con un oracolo separato, altera tutti i 272 bit di GDT/GDTR,
prova 512 basi riproducibili e due estremi, verifica guardie, argomenti
nulli, indirizzi fuori profilo e 24 sovrapposizioni degli output.
Il consumatore C++ verifica anche l'interfaccia della libreria C.
Nei due scenari QEMU validi la nuova fase deve precedere il cleanup.
Queste prove non verificano l'esecuzione di LGDT o un cambio di CS.
La prova separata descritta in exceptions.md esegue LGDT/LIDT e verifica
CS=8 dopo eccezioni reali, usando ancora le mappature firmware.

Riferimento: [Intel SDM volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 3.4.5, 3.5.1 e 3.5.2: descrittori e tabelle in IA-32e mode.
