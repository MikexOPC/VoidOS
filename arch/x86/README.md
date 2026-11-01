#                                   VoidOS — x86

## Cos'è?

Questo è il backend x86-64 di VoidOS. Contiene il bootloader UEFI e le
operazioni del kernel che dipendono da CPU, registri, interrupt e paging x86.

# #=======================================================================================================================================# #

## Struttura

| Cartella | Contenuto |
|---|---|
| `boot/` | Loader UEFI: caricamento ELF, preparazione dell'ambiente e passaggio al kernel. |
| `configs/` | Configurazioni e layout degli indirizzi specifici x86. |
| `kernel/` | Backend CPU, descrittori, eccezioni, interrupt, timer, scheduler e driver x86. |
| `mm/` | Paging, finestre di accesso, mapping e collegamenti alla memoria del kernel. |
| `lib/` | Helper x86 per processore, descrittori e sincronizzazione. |

In `kernel/drivers/` i dispositivi sono organizzati per categoria, come
input, video, bus e memoria. Il codice comune dei driver si trova invece
in [kernel/drivers/](../../kernel/drivers/).

# #=======================================================================================================================================# #

## Confini e verifiche

`boot/` prepara l'ingresso al kernel; non contiene i servizi che devono
continuare a funzionare dopo il boot. `mm/` realizza le operazioni x86,
mentre allocatori e politiche comuni restano in [kernel/mm/](../../kernel/mm/).

I test sono in [test/arch/x86/](../../test/arch/x86/).
La piattaforma viene verificata con QEMU/OVMF; le prove degli AP sono
ancora diagnostiche e non equivalgono a uno scheduler SMP operativo.
Lo [stato corrente](../../docs/status.md) riporta i limiti verificati.
