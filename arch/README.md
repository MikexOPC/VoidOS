#                                   VoidOS — Architetture

## Cos'è?

Questa cartella raccoglie il codice che dipende dalla piattaforma hardware:
boot, CPU, paging e backend dei driver. I servizi comuni restano in
[kernel/](../kernel/), le librerie generali in [lib/](../lib/).

# #=======================================================================================================================================# #

## Struttura

| Cartella | Contenuto |
|---|---|
| [x86/](x86/) | Implementazione attuale per x86-64: loader UEFI, CPU, memoria e driver specifici. |
| `lib/` | Area per eventuali librerie condivise tra architetture; attualmente vuota. |

Non sono ancora presenti backend ARM o RISC-V. La cartella `x86/`
non implica che sia disponibile anche un kernel x86 a 32 bit.

# #=======================================================================================================================================# #

## Organizzazione

Un backend espone al kernel le operazioni dipendenti dalla macchina,
mentre la logica comune rimane fuori da `arch/`. Ogni architettura contiene
solo i moduli realmente implementati, senza cartelle segnaposto.

I test rispecchiano questi percorsi in [test/arch/](../test/arch/).
Vedere [arch/x86/README.md](x86/README.md) per la struttura della piattaforma attuale.
