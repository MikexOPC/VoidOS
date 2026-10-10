# Scrittura pixel x86 — 10 ottobre2026

Modulo framebuffer x86; estensione circoscritta del [mapping](mapping.md),
senza nuovo device, ownership o cambio PAT. Riutilizza il contratto numerico
Rust RGB/BGR e il mapping UC gia autorizzato. `reuse/` resta intatto.

## Contratto e fonti

`pixels/write.h` espone x86_fb_pixel. Un writer BSP con IF=0 e ambiente
eccezioni valido, owner originale Ready2, ramo pubblicato, tutti i lease
pinned, finestra temporanea idle. Prima della dereferenziazione si verifica
l'intero inventario video con autorita CPU effettiva. Un owner copiato,
un lease stale, un pin perso o foglie/cache alterate impediscono lo store.
Nessun accesso userspace o protocollo di concorrenza SMP viene introdotto.

Rust verifica coordinate visibili, stride, formato e Used; Capacity non
autorizza il padding come pixel. C verifica spazio realmente mappato,
overflow e allineamento, aggiungendo Base&4095 alla base virtuale. Store e
readback volatile a32 bit, byte riservato zero; successo solo se valore
letto e autorita finale corrispondono. Non vengono allocate pagine o heap.
Fallimento prima dello store non modifica pixel; readback fallito dopo
lo store segnala errore, non promette rollback del contenuto video.

[UEFI2.10 §12.9.2](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html):
pixel32 bit, formato0 R/G/B/riservato, formato1 B/G/R/riservato. Base indica
il pixel0,0; la riga successiva usa PixelsPerScanLine, non Width. Il kernel
usa il descrittore persistente e accesso diretto, senza chiamare GOP dopo
ExitBootServices. I formati BitMask/BltOnly sono esclusi dalla v0.

[Intel SDM Vol.3A 253668-090US §14.3, tabella14-2](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):
UC forte conserva l'ordine degli accessi; usare registri general-purpose
per load/store UC con eventuali side effect. Disassembly della TU kernel
verificato: MOV dword ptr [rax],ebp seguito da MOV eax,dword ptr [rax],
nessuno store SIMD. PAT e MTRR non modificati; policy verificata dal mapper.

## Collegamento e limiti

Il profilo TestFramebufferMapping ora scrive rosso in alto a sinistra,
verde in alto a destra, blu in basso a sinistra, bianco in basso a destra
e RGB17/34/51 al centro. Prova anche coordinate Width e Height, che devono
essere rifiutate, e verifica budget fisico invariato. Il boot ordinario
mantiene il mapper e non esegue il probe dei pixel. La prova diagnostica
resta isolata e non puo essere installata dal comando di build.

La verifica completa delle tabelle per ogni chiamata e deliberata per il
bring-up: non e un renderer efficiente per un frame intero. Rettangoli e
testo richiederanno un contratto batch delimitato e un benchmark; non
eliminare ownership o controlli per accelerare senza una transazione.
Niente compositor, font, GUI, WC, DMA o driver GPU in questo blocco.

## Prove host

`kernel/build/framebuffer-pixels-host.log`:3 casi Rust delle tabelle e8
dei validatori pixel/mode, ciascuno debug/release, Clippy; sette gruppi
lifecycle x100 e tre gruppi C aggiuntivi sotto MSVC /W4 /WX e ASan.
Due formati, canali0..255, tutti i6144 pixel di due superfici1024x3;
offset fisico4092, stride9/Width7, padding e sentinelle intatti. Rifiuto
prima dell'accesso per coordinate fuori range, owner copiato o revocato,
IF/CPU simulata non valida, PAT modificata, vista attiva, pin perso,
foglie senza RW/NX/UC o user, formato invalido e backend senza vista.
Accounting reale dell'allocatore host ripristinato dopo cleanup.

Cppcheck su11 TU senza diagnostiche bloccanti; note style/information
distinte da warning del compilatore. ASan strumenta C host, non Rust o
RAM fisica. Nessuna nuova allocazione; Valgrind non disponibile, coverage
linee/branch N/D. Errori CPU e mapping negativi sono simulati host.

## Prove firmware

Il provider possiede le VM, verifica ESP/hash/deadline e KERNEL_READY.
Per il diagnostico legge fisicamente tutte le PTE/guardie e i cinque
pixel a32 bit, poi verifica gli stessi colori nel PPM acquisito da QEMU.
Non si deduce un disegno corretto dal solo marker seriale o dalla presenza
di uno screenshot. L'immagine ordinaria e quella senza display restano
regressioni distinte. Manifest e risultati finali riportati sotto;
hardware fisico e prestazioni non misurati.

| Serie finale | Boot consecutivi | Manifest sotto kernel/build/ |
|---|---:|---|
| Diagnostico mapping+pixel |10/10|framebuffer-live-192ead97e3534f0f9c00d393e5840da7/results.json|
| Ordinario senza probe pixel |10/10|framebuffer-live-29ce703f006640378d1c7259e0d792ec/results.json|
| Ordinario senza display |10/10|framebuffer-live-32b6ca0d2e8042a49efaa016c4a8cf98/results.json|

Log campagne: `kernel/build/framebuffer-pixels-final-firmware.log`.
Kernel diagnostico SHA256:
`502341F1D4155AF7B6D03C376C8C5CBB4849B574E1B15A8BBFFBAA38DD2ED9B3`.
Kernel ordinario SHA256:
`B6417FB1438A92E60BAB7CD83CDF65622BBDA00EF2D12FC7C34942BB19F71EBC`.
Loader SHA256:
`EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7`.
Tutte le serie raggiungono KERNEL_READY, nessun FAIL/triple fault inatteso,
loader invariato, CPU/guardie/PAT verificati. Nel diagnostico BGR1024x768:
768 foglie video e cinque colori esatti, byte riservato zero, budget fisico
invariato dopo scrittura. RGB e offset4092 verificati solo host, non con
un firmware/device reale. Nessuna misura di prestazioni o claim GPU.

Review finale: `kernel/build/framebuffer-pixels-review.json`; disassembly
`kernel/build/framebuffer-pixels-disassembly.log`. Vincoli sorgenti,
separazione, whitespace e link locali passano. Vecchio OS non modificato.

Per riprodurre dalla root del repository:

```powershell
./arch/x86/kernel/drivers/video/framebuffer/scripts/test.ps1
./kernel/scripts/build.ps1 -TestFramebufferMapping -OutputDirectory kernel/build/pixel-check
python ./arch/x86/kernel/drivers/video/framebuffer/scripts/firmware.py --kernel kernel/build/pixel-check/kernel.elf --rollback --pixels --boots 10
```

Usare Python disponibile sul proprio host. La seconda immagine e diagnostica;
per il boot ordinario compilare senza TestFramebufferMapping. Screenshot
contiene cinque singoli pixel, non una schermata grafica o una GUI.

ESP aggiornata con la sola immagine ordinaria gia verificata; hash della
copia confrontato. Backup: `kernel/build/framebuffer-pixels-previous-fd1c0da2fb854f44b6990c0f50b27180.elf`.
