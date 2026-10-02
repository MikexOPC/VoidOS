# Codice persistente per il passaggio finale

Il componente `handoff/transition/` prepara una pagina autonoma di codice:
580 byte di trampoline e handler, seguiti da padding INT3 fino a 4096.
La pagina appartiene allo stesso owner dei descrittori, ma ha una foglia
distinta R/X; descrittori e stack restano RW/NX. Non ci sono alias RW
del codice nel nuovo grafo verificato.

## Layout e durata

| Offset nella pagina | Contenuto |
| --- | --- |
| 0 | Tratto finale del trampoline, 27 byte |
| 27..63 | Padding INT3 |
| 64 + vettore * 16 | Handler per ciascun vettore CPU 0..31 |
| 576 | Stop comune CLI/HLT con loop relativo |
| 580..4095 | Padding INT3 |

Gli handler impostano EAX al vettore e saltano allo stop comune, senza
toccare ulteriormente lo stack. Tutti i salti del blocco sono relativi
e interni. Il codice copiato non contiene puntatori al PE EFI, chiamate
C/Rust o dipendenze dal runtime. La IDT persistente contiene gli indirizzi
dei nuovi handler nell'ultima pagina dell'allocazione.

L'owner possiede ora dieci pagine EfiLoaderData, 40 KiB: nove per dati e
stack, l'ultima per il codice. Il piano mappa quest'ultima in identita
(indirizzo virtuale uguale al fisico), registra CapsuleBase e verifica
che esista esattamente una foglia di una pagina, R/X. Il contratto kernel
v2 non cambia. La pagina del codice non riduce il budget ELF di 128 MiB.

La preparazione ordinaria scrive e legge la pagina con il paging firmware,
senza eseguirla. EfiLoaderData puo essere NX nel firmware: la futura fase
deve attivare le proprie mappature prima di entrare nel tratto finale.
La mappatura in identita nel nuovo grafo non prova i permessi del vecchio.
Il blocco resta vivo fino all'ultimo utilizzo di trampoline e IDT; non
puo essere liberato dopo aver caricato descrittori che lo referenziano.

## Contratto del tratto finale

All'ingresso della pagina: RCX contiene l'indirizzo del kernel, RDX la
cima allineata dello stack e R8 l'indirizzo del contratto. Il codice:

1. Disabilita gli interrupt mascherabili e azzera DF.
2. Conserva l'ingresso in RAX, imposta RSP alla cima e sottrae 40 byte.
3. Scrive un indirizzo di ritorno nullo e riserva 32 byte di shadow space.
4. Mette il contratto in RCX, azzera RBP e salta all'ingresso in RAX.

Il kernel riceve RSP modulo 16 uguale a 8, come un ingresso con ABI
Microsoft x64. Non deve ritornare. Gli altri registri e lo stato SIMD/FPU
non vengono inizializzati da questo blocco. CLI non protegge da NMI o SMI.
Questo non e un trampoline completo che cambia da solo ambiente CPU:
non carica GDTR/IDTR/TR, non scrive CR3, EFER o CR0 e non chiama firmware.

## Ruolo di Rust e controlli

`rust/validation/capsule_profile.rs` e codice di produzione no_std,
collegato all'immagine EFI. Espone un oracolo dei byte del profilo e
controlli numerici di indirizzi, dimensioni, allineamento e sovrapposizioni.
Usa aritmetica controllata e non dereferenzia puntatori, non alloca e
non richiede blocchi unsafe. Il confine C/Rust scambia solamente scalari;
gli attributi unsafe(no_mangle) dichiarano esplicitamente i simboli esportati.

C verifica il template assembly contro l'oracolo prima della copia,
richiede una pagina allineata posseduta dal chiamante e ricontrolla
tutta la pagina, incluso il padding. L'owner viene pubblicato Ready
solo dopo codice e descrittori verificati. Il preflight terminale
ricontrolla entrambi, grafo e copertura delle dieci pagine nella mappa.
I controlli non sono autenticazione crittografica ne un decoder generico
x86; proteggono questo profilo esatto. L'accessibilita dei buffer resta
una responsabilita esplicita del chiamante C e dell'allocatore firmware.

## Prove e sequenza ancora da costruire

La build esegue tre test Rust, incluse 1024 basi riproducibili, test C
con 32768 mutazioni della pagina e regressioni di mapping e cleanup.
Il probe QEMU copia il codice in una pagina EfiLoaderCode separata:
tre eccezioni reali verificano gli handler copiati con LTR/IST1, mentre
un quarto scenario entra nel trampoline e controlla RSP, argomento,
ritorno nullo, IF e DF. Il destinatario di quel salto e un arresto di
test nell'immagine EFI, non il kernel e non un cambio CR3.
Il probe esegue CPUID dopo la copia, prima dell'attivazione, preservando
gli argomenti. Questa serializzazione segue Intel SDM 3A, sezione 9.1.3;
la futura transizione deve includere la stessa garanzia di pubblicazione
del codice sul processore che lo eseguira.

Restano il tratto iniziale persistente che attiva il paging e i descrittori,
il passaggio sicuro fra vecchie e nuove mappature, ExitBootServices reale
e il salto al kernel ELF. Occorre verificare risorse raggiungibili a ogni
istruzione della transizione, compresi stack, IDT/TSS e errori terminali.
I probe usano il paging firmware e non certificano hardware fisico.

Riferimenti: [ambiente x64 UEFI 2.11 e calling convention](https://uefi.org/specs/UEFI/2.11/02_Overview.html#x64-platforms),
[servizi UEFI](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel SDM volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
