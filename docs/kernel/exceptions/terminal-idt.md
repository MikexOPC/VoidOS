# Eccezioni CPU del kernel

Il kernel installa una propria IDT dopo aver verificato il contratto di ingresso,
prima dell'inventario e delle allocazioni. I 32 gate puntano a ingressi Assembly
nel segmento eseguibile del kernel; la tabella statica occupa 512 byte, allineati
a 16 byte. L'operando IDTR occupa 10 byte. Non servono allocazioni dinamiche.

## Costruzione e attivazione

Il C verifica confini, allineamento e separazione dei buffer prima della scrittura.
Rust controlla che gli indirizzi appartengano al codice degli ingressi terminali.
Ogni gate contiene selettore CS 8, interrupt gate presente ring zero `0x8e`, IST 1
e campi riservati zero. La verifica confronta tutti i byte prima di `LIDT`.

L'attivazione richiede CS 8, SS 16, TR 24 e IF disabilitato. La IDT precedente
deve avere il formato iniziale del loader: 32 gate, limite 511, base canonica
inferiore e allineata. Dopo il caricamento, `SIDT` deve restituire esattamente
l'operando preparato; in caso contrario viene ripristinata la IDT precedente.
`KERNEL_IDT_OK` viene stampato solamente dopo questa verifica.

La GDT, la TSS e lo stack IST di 16 KiB sono ancora prestiti persistenti dal
bootloader. La loro validita deriva dalla transizione gia verificata del loader:
questo modulo non ricostruisce o valida indipendentemente la TSS. Le pagine del
loader restano riservate e non sono reclamate dall'allocatore fisico.

## Frame e diagnostica

Gli ingressi normalizzano il frame in sette valori a 64 bit:
`Vector, Error, RIP, CS, RFLAGS, RSP, SS`. Per i vettori senza error code CPU,
l'ingresso inserisce zero. I vettori con error code sono 8, 10, 11, 12, 13, 14,
17, 21 e, per le definizioni AMD, 29 e 30. Non usare `INT n` per simulare queste
eccezioni: la CPU non aggiungerebbe il codice d'errore atteso.

Lo stack viene allineato a 16 byte, con 32 byte di shadow space prima della
chiamata C secondo ABI Microsoft x64. `CLD` precede il C. Il frame originale
rimane sullo stack IST. La seriale stampa vettore, errore, RIP e selettori; CR2
viene riportato solo per page fault. Rust controlla il formato del frame e
l'error code zero del double fault. Tutte le stampe sono limitate e non allocano.

Il terminale si arresta con `CLI/HLT` e marcatore RAX `0x4b4641554c540001`.
Non torna con `IRETQ`, non recupera un thread e non ripristina registri/SIMD.
Non e una gestione completa degli interrupt: IF resta zero, non ci sono IRQ,
APIC, SMP o handler recuperabili. Un unico IST condiviso non offre robustezza
per eccezioni annidate o NMI durante la diagnostica. Non e una prova su hardware.

## Verifica

Da PowerShell nella root del progetto:

```powershell
./kernel/test/host.ps1
./kernel/scripts/build.ps1 -Install
./kernel/test/firmware.ps1 -UseInstalled
```

La ESP deve gia contenere il bootloader costruito con `-BootKernel`.
Le varianti con fault non possono essere installate nella ESP ordinaria.
I test host confrontano i gate con una decodifica indipendente e alterano ogni
byte della tabella e dell'operando. Rust verifica confini degli ingressi e frame.
QEMU verifica IDTR attivo, frame in memoria, registri, CR3, stato UEFI e assenza
di triple fault tramite `-d int,cpu_reset`. Le eccezioni vengono generate da
accessi vietati, `UD2`, selettore fuori GDT e divisione per zero.
Un'ulteriore variante porta RSP al fondo dello stack kernel e forza un push
nella pagina guardia: la diagnostica deve comunque raggiungere lo stack IST.

## Riferimenti ufficiali

- [Intel SDM Volume 3A](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
  capitolo 7: IDT, frame, IST e codici delle eccezioni.
- [AMD64 APM Volume 2](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf),
  capitolo 8: vettori #VC e #SX.
- [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170),
  allineamento e shadow space.
