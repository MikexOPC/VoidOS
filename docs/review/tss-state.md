# Checkpoint TSS e IST1 del 2 ottobre 2026

Libreria C riutilizzabile: TSS di 104 byte, descrittore di 16 byte,
RSP0 e IST1 su intervalli separati di 16 KiB, bitmap I/O assente.
Zero allocazioni nuove nel boot ordinario; il TSS non e ancora attivato
nel suo percorso. I due stack statici sono solo nel probe EFI separato.

Build completa superata: C/C++ /W4 /WX, assembly /W3 /WX, Rust con
warnings vietati. Il nuovo test verifica byte, campi, 960 mutazioni,
104 alias degli output, regioni stack, errori e conservazione degli oggetti.
La suite precedente resta verde. QEMU/EDK2 ordinario: 10/10 scenari.
Probe IST1: 3/3 eccezioni CPU reali (#DE, #UD, #GP), LTR eseguito,
RSP nello stack dedicato, TR=24, bit busy nella GDT, arresto corretto;
#GP conserva l'error code 0x28. Verificare il busy nella GDT evita di
dipendere dall'etichetta available mostrata dal monitor per la cache TR.

| Artefatto in arch/x86/boot/uefi/build/ | SHA256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | 2D5D9AFEF82D787870F26007C460834EE6269B13D62A29582323B2B32E367161 |
| tss-build.log | E9BAB25BA30CD8C80C9E6EFAFE754819B75929518B451F98282D19D4DC1639EA |
| tss-firmware.log | 6FE1CD32E6974F48320C7CEA0383746C447A2BAB311F325AFF3DED669CCE1252 |
| tss-probe.log | A32F701EF43FDFEB9D4175B6CA5E84442A7BF7A749BBBBC2282CB21EBA847529 |

Log ordinari: firmware-test-78050033f8cc447688aab80b49ad8dae.
Log probe: exception-test-ea0b533ade1e446f9cd3ce322e58dca5.
I log grezzi restano in build/, ignorati da Git. La libreria TSS e
collegata dai test e dal probe; il linker non la include nel boot ordinario
finche il suo percorso non la usa.

Stime di maturita, non misure di righe o tempo: manca circa 40-45% per
avviare un kernel minimale in QEMU e circa 65-75% per un bootloader
iniziale affidabile su un insieme definito di macchine x86_64 UEFI reali.
Il primo obiettivo richiede ancora risorse persistenti di transizione,
mappature, trampoline, ExitBootServices reale, attivazione protezioni e
ingresso osservabile nel kernel. Il secondo richiede anche test fisici,
copertura di piu firmware e piattaforme, diagnostica e gestione dei guasti.
Nessuna macchina fisica e stata provata: quella stima ha maggiore incertezza.
Lo scheletro del kernel ibrido puo iniziare prima del completamento del loader.

Limiti: il probe conserva paging firmware, non testa guard page,
double fault o NMI annidati. Il boot ordinario non esegue LTR, non cambia
CR3 e non entra nel kernel. [Formato e durata delle risorse](../handoff/descriptors/tss.md).
