# GDT, TSS e stack di emergenza del kernel

Il kernel sostituisce GDT e TSS del loader dopo l'inizializzazione della memoria.
La prima IDT kernel usa temporaneamente IST1 del loader; la sostituzione della TSS
avviene prima di assegnare IST2 a NMI e IST3 a double fault. Lo stack principale
di ingresso viene poi sostituito dallo [stack principale autonomo](../stack/main-stack.md).

## Storage e selettori

`arch/x86/kernel/descriptors/segments.c` mantiene un owner statico di 176 byte,
allineato a 16: GDT di 40 byte, padding esplicito di 8, TSS di 104, operando GDTR
di 10 e padding finale di 14. Il limite GDTR e 39. CS usa 8, i segmenti dati 16
e TR 24. Il descrittore TSS occupa 16 byte e punta alla TSS del kernel, limite
103; il byte tipo e `0x89` prima di LTR e `0x8b` dopo. L'offset della bitmap I/O
e 104, oltre il limite della TSS: nessuna bitmap I/O abilitata.

Sono riutilizzati i costruttori e validatori del progetto in
`arch/x86/lib/descriptors/`. Il validatore kernel controlla inoltre IST2/IST3
e lo stato busy previsto, senza ignorare altri bit. Gli header standard MSVC/UCRT
servono solo alla compilazione; il link continua a usare `/nodefaultlib`.
RSP0 viene inizialmente preparato per lo stack loader, poi aggiornato al top
del nuovo stack principale sotto il profilo UP/CPL0/IF=0. La verifica distingue
questi due valori previsti; il nuovo valore e ammesso solo nella TSS busy.

## Geometria e ownership degli stack

| Uso | Base inclusiva | Top esclusivo | Guardia inferiore |
| --- | --- | --- | --- |
| IST1, eccezioni ordinarie | `0x700000041000` | `0x700000045000` | `0x700000040000` |
| IST2, NMI | `0x700000046000` | `0x70000004a000` | `0x700000045000` |
| IST3, double fault | `0x70000004b000` | `0x70000004f000` | `0x70000004a000` |

La guardia finale e `0x70000004f000`. Le quattro guardie sono PTE zero;
non consumano pagine fisiche. I tre intervalli scrivibili occupano quattro
pagine ciascuno, supervisor RW/NX. Un unico lease fisico contiguo di dodici
pagine WB convenzionali consuma 49.152 byte e viene pin-nato prima del mapping.
Non occorrono nuove tabelle: le foglie PT 64..79 appartengono allo stesso ramo
gia usato da stack principale e finestra di allocazione, in un intervallo distinto.

Rust determina base, top, ordinali delle dodici pagine e limiti fisici CPUID,
con addizione checked. C controlla parent e vista ricorsiva prima delle foglie,
rifiuta ogni PTE preesistente e verifica il mapping completo. Sono tollerati
soltanto i bit hardware Accessed/Dirty sui mapping presenti. Le guardie devono
restare esattamente zero. Gli stack vengono azzerati e verificati prima di LTR.

Owner e allocatore devono restare mappati e alla stessa posizione. Il controllo
numerico di un puntatore non dimostra che sia mappato: questa e una precondizione
del chiamante C. I metadati non possono sovrapporsi tra loro o alla regione IST.
L'owner non si copia e non si sposta mentre contiene un lease.

Prima del commit, il rollback revoca tutte le dodici PTE, esegue INVLPG locale,
rimuove il pin, restituisce il lease e azzera i metadati. Un mapping corrotto
non viene cancellato ne restituito all'allocatore. Dopo commit, gli stack sono
permanenti nel bootstrap: il rilascio e respinto anche se un controllo successivo
fallisce. Non si libera memoria ancora raggiungibile dalla CPU.

## Transizione

L'Assembly esegue LGDT e LTR, ricarica segmenti dati/stack e CS. Il far return
usa `REX.W + CB` (`48 CB`) e due slot da otto byte: RIP seguito da CS. FS e GS
vengono impostati a selettore nullo; questo kernel non usa TLS o dati tramite
FS/GS. La routine rispetta i registri non volatili dell'ABI Microsoft x64.

SGDT deve restituire l'operando preparato; il descrittore deve risultare busy,
il pool deve avere esattamente dodici pagine in meno e mapping/pin devono essere
ancora validi. Solo allora la IDT assegna IST2 al vettore 2 e IST3 al vettore 8.
Durante la transizione il vecchio storage resta valido e IST1 della nuova TSS
e gia pronto. `KERNEL_DESCRIPTORS_OK` viene stampato dopo le verifiche.

## Test e limiti

```powershell
./test/kernel/host.ps1
./kernel/scripts/build.ps1 -Install
./test/kernel/firmware.ps1 -UseInstalled
```

La ESP deve contenere il loader costruito con `-BootKernel`. Gli artefatti dei
fault sono isolati in `kernel/build/`; la build impedisce di installarli nella
ESP ordinaria. I test host verificano codifica indipendente, stato vergine,
3.696 alterazioni di bit disponibili/busy con RSP0 iniziale o nuovo,
1.000 corruzioni PTE IST respinte,
ownership, rollback di ciascuno stack mancante, OOM e 256 cicli senza perdite.
Il test Rust del profilo principale porta il totale kernel a 19.

I 18 scenari QEMU controllano GDTR, cache TR, busy in memoria, CR3, PTE/guardie
e frame reali su IST1/IST2/IST3. Il monitor inietta NMI dopo KERNEL_READY.
La variante double rende non presente la gate GP e forza un GP: il fault durante
la consegna genera DF sulla terza IST. Il RIP salvato del DF non viene assunto
definito. I log CPU devono essere privi di triple fault.

Profilo attuale: una CPU, IF disabilitato, quattro livelli paging e INVLPG locale.
Nessun recupero IRET, SMP, IRQ o supporto per CPU con CET attivo. La guardia
terminale delle eccezioni resta globale e un secondo ingresso si arresta senza C.
Il test NMI dimostra un ingresso terminale, non recupero o robustezza generale
contro NMI annidati/machine check. Le pagine del loader non vengono ancora
reclamate. Stack principale e RSP0 sono ora propri del kernel; il vecchio mapping
e revocato, mentre il backing fisico resta riservato.
Questi risultati riguardano QEMU, non hardware fisico.

## Fonti ufficiali consultate

- [Intel SDM 3A, revisione 090](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
  sezioni 7.14.5, 10.2.3, 10.2.4 e 10.7: IST, descrittore e TSS a 64 bit.
- [Intel SDM 2A](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf),
  LTR e LGDT: selettore a 16 bit, descrittore di 16 byte e busy dopo LTR.
- [Intel SDM 2B](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf),
  RET: operand size esplicita a 64 bit per il far return.
- [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170),
  registri non volatili, allineamento e shadow space.
