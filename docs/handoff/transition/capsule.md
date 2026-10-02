# Codice persistente per il passaggio al kernel

Il componente `handoff/transition/` prepara una pagina autonoma di codice:
831 byte di trampoline e handler, seguiti da padding INT3 fino a 4096.
La pagina EfiLoaderCode e la seconda allocazione dell'owner dei descrittori.
Nel nuovo grafo e R/X; dati e stack restano RW/NX, senza alias RW del codice.

## Layout e durata

| Offset | Contenuto |
| --- | --- |
| 0..26 | Tratto finale isolato per il probe ABI |
| 27..63 | Padding INT3 |
| 64 + vettore * 16 | Handler per vettori CPU 0..31 |
| 576..579 | Stop eccezioni CLI/HLT con loop relativo |
| 580..639 | Padding INT3 |
| 640..830 | Attivazione CPU, CALL kernel e stop dopo ritorno |
| 831..4095 | Padding INT3 |

Gli handler impostano EAX al vettore e saltano allo stop senza ulteriori
accessi allo stack. Salti interni e indirizzo del ritorno far sono relativi,
senza rilocazioni COFF. Il codice copiato non chiama C, Rust o firmware.
L'adattatore esterno esegue CPUID dopo la copia, preservando gli argomenti,
prima di entrare nella pagina posseduta: serializza il codice sulla CPU.

Il piano registra CapsuleBase e richiede esattamente una pagina R/X in
identita. La copertura EfiLoaderCode senza runtime, XP, RP o RO viene
controllata prima della copia e sulla fotografia finale. La pagina non
riduce il budget ELF di 128 MiB. Le risorse devono restare vive finche
CPU e kernel le usano; non si liberano tramite firmware dopo l'uscita.

## Due ingressi distinti

Il tratto a offset zero conserva il precedente ABI del probe: RCX ingresso,
RDX cima stack, R8 contratto. CLI/CLD, RSP meno 40, ritorno nullo e shadow
space precedono il salto. Non attiva descrittori o paging; il destinatario
non deve ritornare. La prova isolata resta nel paging firmware.

Il percorso `-BootKernel` entra a offset 640 con RCX al contesto posseduto.
Attiva NXE/WP, GDT/TSS/IDT, segmenti e CR3, poi chiama l'ingresso ELF con
shadow space e ritorno nella pagina persistente. Il kernel diagnostico
puo ritornare un marker prima dell'arresto. La [sequenza completa](activation.md)
descrive ordine, preflight e limiti.

## Ruolo di Rust e controlli

`rust/validation/capsule_profile.rs` e produzione no_std collegata all'EFI.
Verifica profilo numerico, allineamento, alias, root e CR4, ed espone un
oracolo dei byte del template. Usa aritmetica controllata, non dereferenzia
puntatori e non alloca. Il confine C/Rust scambia scalari; gli attributi
unsafe(no_mangle) dichiarano i simboli esportati.

C confronta il template con l'oracolo prima della copia e poi ricontrolla
la pagina, incluso padding. Ready viene pubblicato dopo codice e descrittori
verificati. Il preflight ricontrolla entrambi. L'oracolo non e autenticazione,
un decoder generico o una prova della correttezza CPU: servono anche i test reali.

## Prove e limiti

La build esegue quattro test Rust, incluse 1024 basi riproducibili e stati
CPU rifiutati, e test C con 32768 mutazioni della pagina.
Il probe QEMU isolato verifica tre eccezioni con LTR/IST1 e un ingresso ABI
nel paging firmware. Il test kernel verifica separatamente ExitBootServices,
nuova root, ingresso ELF e tre #PF per WP, guardia e NX dopo l'attivazione.

IST1 condiviso, passaggio non atomico fra GDTR e IDTR e stato SIMD/FPU/TLS
ereditato restano limiti iniziali. Non e una certificazione hardware.

Riferimenti: [ambiente x64 UEFI](https://uefi.org/specs/UEFI/2.11/02_Overview.html#x64-platforms),
[servizi UEFI](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel SDM 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
