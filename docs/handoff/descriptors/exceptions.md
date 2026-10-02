# Eccezioni x64 del boot

La libreria `arch/x86/lib/descriptors/idt.c` prepara 32 interrupt gate:
vettori CPU 0..31, selettore codice 8, present, ring 0, tipo 0xE,
IST zero e campi riservati zero. IDT: 512 byte, allineamento 16;
IDTR: 10 byte little endian, limite 511. Non usa bitfield o struct packed.

Il profilo ammette basi e ingressi nella meta canonica bassa a quattro
livelli, fuori dalla prima pagina. La base IDT deve essere allineata e
l'intera tabella deve rientrare nel limite. Output e array degli ingressi
devono essere accessibili, distinti e con la durata gestita dal chiamante.
Gli errori vengono rifiutati prima delle scritture; la verifica numerica
non dimostra che il codice sia eseguibile o le pagine siano mappate.

## Ingressi assembly

`handoff/descriptors/assembly/exceptions/terminal.asm` contiene 32 ingressi.
Ognuno imposta RAX al numero del vettore e salta allo stop comune:
CLI, HLT, ripetizione di HLT se la CPU viene risvegliata. Non usa ulteriormente
lo stack, non chiama C o firmware, non usa heap, porte I/O o IRET.
Il frame CPU, incluso l'eventuale error code, rimane sullo stack.
Gli altri registri non vengono salvati: questo e un arresto terminale,
non un handler che consente di proseguire. Il vettore e visibile nel debugger,
non stampato sullo schermo.

CLI non impedisce NMI o SMI. Senza TSS/IST non c'e uno stack alternativo:
un errore sullo stack corrente puo ancora impedire la consegna delle
eccezioni o causare triple fault. Mancano inoltre gli interrupt esterni,
i cui vettori 32..255 non fanno parte della tabella.

## Uso nel bootloader ordinario

`boot_prepare_idt_draft` costruisce e verifica la bozza, poi stampa il
successo. La bozza occupa 528 byte sullo stack EFI corrente, senza nuove
allocazioni firmware. Vive fino al ritorno di `efi_main`. Nessuna LIDT
viene eseguita nel bootloader ordinario: resta attiva la IDT firmware.
L'assembly e nel PE EFI e i suoi indirizzi vengono rilocati dal loader UEFI.

Prima del cambio CR3 servono proprieta e mappature persistenti per IDT,
GDT, ingressi, operandi e stack. Queste bozze sullo stack EFI non sono
utilizzabili dopo quel cambio. Il contratto kernel v2 resta invariato.

## Verifiche ripetibili

La build ordinaria verifica 4.176 mutazioni di bit IDT/IDTR, 512 gruppi
di indirizzi riproducibili e due estremi, argomenti invalidi, guardie,
alias parziali e interfaccia C++. Controlla anche i byte macchina dei
32 ingressi: vettore, salto verso lo stop e loop CLI/HLT. I dieci scenari
firmware richiedono la nuova fase nei due percorsi validi.

Per la prova CPU separata, dopo la build:

```powershell
& .\arch\x86\boot\uefi\test\handoff\descriptors\exception_firmware_test.ps1
```

Lo script crea tre immagini EFI di test in build/, senza sostituire quella
ordinaria. In QEMU/EDK2 carica GDT, IDT e [TSS](tss.md), disabilita gli interrupt mascherabili
e provoca divisione per zero (#DE), UD2 (#UD) e caricamento di un selettore
oltre il limite GDT (#GP). Il monitor verifica CS=8, limiti GDT/IDT,
RAX=vettore, RIP nel loop di arresto, IF=0 e HLT=1. Verifica anche TR,
il bit busy nella GDT e RSP nel nuovo stack IST1. Per #GP verifica
anche l'error code 0x28 in cima allo stack. Ogni VM termina dopo il controllo;
il probe non ritorna al firmware dopo aver cambiato i descrittori.

La prova usa le mappature firmware: non dimostra ExitBootServices,
cambio CR3, ingresso nel kernel o funzionamento su hardware fisico.
Lo stack alternativo appartiene ancora all'immagine caricata dal firmware.
Integrazione persistente di TSS/IST e diagnostica autonoma restano passi successivi.

Riferimento: [Intel SDM volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 6.13, 6.14.1, 6.14.2 e 6.14.5.
