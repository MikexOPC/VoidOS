# Verifica IDT e ingressi eccezioni del 2 ottobre 2026

Tre cicli: libreria IDT con test host; ingressi assembly e integrazione
della bozza nel boot; prove CPU separate e rinforzo dei controlli di alias,
allineamento, IF ed error code. La revisione ha corretto anche la lettura
condivisa del log QEMU e il parser dei registri RFL/stack del monitor.

Build completa MSVC /W4 /WX, assembly /W3 /WX e Rust con warnings vietati:
superata, inclusi test precedenti, 4.176 mutazioni IDT/IDTR, 514 gruppi
di basi/ingressi, guardie, alias e verifica macchina dei 32 ingressi.
Il test dell'ingresso completo richiede 14 sezioni. IDT allineata a 16 byte;
bozza completa 528 byte sullo stack, zero nuove allocazioni firmware.

Bootloader ordinario QEMU/EDK2: 10/10 scenari sulla stessa immagine.
Probe EFI separati: 3/3 eccezioni CPU reali (#DE, #UD, #GP), con controllo
CS, GDT/IDT, RAX, RIP, IF=0 e HLT=1; error code #GP 0x18 conservato.
Nessuna esecuzione privilegiata dei nuovi handler nei test DLL Windows.

EFI ordinario: 59.392 byte, +2.048 rispetto al checkpoint buffer kernel.
La crescita include codice e allineamento delle sezioni PE; non e una misura
del consumo RAM complessivo del firmware. Nessun cambiamento al contratto v2.

| Artefatto in arch/x86/boot/uefi/build/ | SHA256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | A10896BE9C9012A6C4638BA13133FB21101B430683B4DB890FF5602BB43E7509 |
| exceptions-build.log | 50714585394B9DB28A3BBDA8794A4CF833E2EF321D1A3AAB4C959E0D3E0FCEAB |
| exceptions-firmware.log | 7978FD3B4E1F9C09584DEF1AB6E91686F86A80D7CC14DC21570662B4761AF366 |
| exception-probe.log | CCF3F93B395E4EAFF11519A802A101A6D2F315DA1A20DA077A0502E3DB914CB4 |

Log firmware: `firmware-test-2a2c81389d334f1d99e9fd6405208071`.
Log probe e immagini dedicate: `exception-test-d4ae6c6349f5421d8b7d25c4a726ef22`.
Per ogni vettore sono conservati serial.log e registers.log;
exception-probe.log identifica anche gli hash delle immagini di test.
Gli artefatti grezzi restano locali e ignorati da Git.

Il boot ordinario non carica GDTR/IDTR, non cambia CR3 e non salta al kernel.
Le prove CPU usano paging e stack firmware. Mancano mappature persistenti
della transizione, TSS/IST e diagnostica autonoma. Il kernel resta previsto
ibrido; questa modifica non decide la futura divisione dei suoi servizi.

Dettagli e comandi: [eccezioni del boot](../handoff/descriptors/exceptions.md).
Formato verificato sul [manuale Intel](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
