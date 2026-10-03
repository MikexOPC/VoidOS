# Stack principale autonomo

Il kernel passa dallo stack del loader a sedici pagine proprie, supervisor RW/NX,
con due guardie. Lo stack attivo e RSP0 non dipendono piu dal backing del loader.
Il vecchio mapping viene revocato; i frame fisici restano riservati.

## Geometria e costo

| Regione | Indirizzo | Dimensione |
| --- | --- | --- |
| Guardia inferiore | `0x700000060000` | 4 KiB, PTE zero |
| Stack principale | `0x700000061000` | 64 KiB, sedici PTE RW/NX |
| Top esclusivo / RSP0 e guardia superiore | `0x700000071000` | 4 KiB, PTE zero |

Sono usate le foglie PT 96..113 nello stesso ramo degli IST, senza nuove tabelle.
Il lease principale contiene sedici pagine contigue WB convenzionali, 65.536 byte;
gli IST mantengono il proprio lease di dodici pagine. Totale persistente per gli
stack kernel: 28 pagine, 114.688 byte. Il mapper condiviso ora serve due profili
chiusi: 1 = IST, 2 = principale. Rust valida pagine, limiti fisici checked e RSP.
I metadati del mapper occupano 72 byte; l'owner principale completo 96 byte.

## Transizione e ownership

La preparazione verifica root/parent, sedici PTE loader contigue e due guardie.
I frame del vecchio stack devono essere esclusi da tutti i free extent e lease
vivi, prima di allocare o azzerare il nuovo: il backing ancora attivo non puo
essere sovrascritto. L'acquisizione riusa pin, zeroing, mapping e rollback degli IST.

La fase 1 consente il rollback. Il commit richiede RSP sul vecchio stack valido
e rende permanente il lease prima di pubblicare RSP0 o cambiare RSP. I byte 4..11
della TSS attiva vengono aggiornati al nuovo top senza ripetere LTR su un
descrittore busy. IST, descriptor e bitmap I/O restano invariati. La scrittura
richiede UP, CPL0 e IF=0: non ci sono ingressi di privilegio che leggano RSP0
durante l'aggiornamento; NMI/DF usano i propri IST.

L'Assembly imposta RSP al nuovo top, azzera RBP e riserva 32 byte di shadow space.
CALL inserisce il ritorno sul nuovo stack: all'ingresso C RSP modulo 16 vale 8.
La continuazione non riceve argomenti e usa owner statici; non si copiano frame C.
Dal nuovo stack, il kernel verifica nuovamente il mapping loader, azzera tutte
le sedici PTE e poi esegue sedici INVLPG. Il successo viene stampato dopo RSP,
mapping, pin, budget e revoca verificati.

Non si ritorna a kernel_main o al loader. Un ritorno del callback viene arrestato
con RAX `0x4b53545245540001`. Il backend raw richiede top mappato/allineato e un
ingresso statico RX del kernel: il controllo numerico non dimostra da solo
l'eseguibilita di un indirizzo arbitrario. Owner, pool e contratto devono essere
buffer mappati, separati e persistenti, fuori dalle regioni guardate.
Gli owner non si copiano o spostano. Il rilascio e vietato quando RSP e nel nuovo
intervallo, anche se disallineato, e sempre dopo commit.

BOOT_START_INFO conserva StackBase/StackBytes originali come descrizione
dell'ingresso, non dello stack attivo. Nessuna pagina loader viene liberata.

## Test

```powershell
./kernel/test/host.ps1
./kernel/scripts/build.ps1 -Install
./kernel/test/firmware.ps1 -UseInstalled
```

La ESP deve contenere il loader con -BootKernel. I test host coprono pin/zeroing,
rollback, OOM, alias, owner copiato, RSP errato, overlap fisico con free extent
o lease, 1.120 corruzioni PTE nuove, 1.120 corruzioni del mapping loader e
128 cicli senza perdite. La TSS viene verificata con RSP0 originale e nuovo.
I test Rust kernel sono 19.

I 18 scenari QEMU verificano RSP, RSP0 in memoria, sedici PTE nuove RW/NX,
due guardie e diciotto PTE loader zero, oltre alla regressione GDT/TSS/IST.
Il push sotto la nuova base genera PF su `0x700000060ff8`. Le prove aggiuntive
leggono il vecchio stack revocato e la guardia superiore, tentano esecuzione
sullo stack NX e fanno ritornare il callback. Le build con fault restano isolate
in build e non sono installabili nella ESP ordinaria.

## Limiti e fonti

E un passaggio definitivo del bootstrap, non uno switch di thread. IF resta zero;
niente IRQ/SMP/CET attivo o unwind attraverso il confine. Gli handler restano
terminali. Tabelle paging, contratto, mappa e altre risorse loader sono ancora
prestiti persistenti. QEMU non sostituisce prove su hardware fisico.

- [Intel SDM 3A](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
  sezioni 7.14.4, 7.14.5 e 10.7: RSPn, IST e TSS a 64 bit.
- [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170),
  registri, allineamento e shadow space.
