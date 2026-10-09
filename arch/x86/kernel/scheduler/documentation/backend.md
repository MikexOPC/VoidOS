# Backend cooperativo x86: implementazione e verifica host

Rapporto storico del primo blocco host. Stato finale del modulo:
[mapping nativi, dispatcher e validazione QEMU](integration.md).

8 ottobre 2026. Stato: backend implementato e verificato su Windows x64;
integrazione nel kernel ancora aperta. Non dichiara lo scheduler pronto.
Aggiornamento successivo: [review breve, stack owner e nuove prove](resources.md).
Contratto architetturale: [context](../../../../../kernel/scheduler/documentation/context.md)
e [design](../../../../../kernel/scheduler/documentation/design.md).

## Implementazione

- `context.c`: costruzione atomica del record iniziale, geometria dello stack,
  controllo dell'autorita proprietaria, formato FP, return address autorizzato.
- `capabilities.c`: predicato puro sullo snapshot; BSP, CR3 atteso, IF/DF,
  FXSR/SSE/SSE2 e prerequisiti di controllo. Vietati LA57 e CET nel profilo v0.
- `assembly/profile/capture.asm`: lettura CPU solo dopo CPL=0; il template
  inizializza FP/SSE e ripristina lo stato originale prima del ritorno.
- `assembly/switch/context.asm`: salva RSP e GPR non volatili, FXSAVE64,
  FXRSTOR64, nuovo RSP e RET; bootstrap con shadow space Microsoft x64.

Gli assert C controllano ogni offset dei due record. Il backend non alloca
memoria e non modifica CR3, CR0, CR4 o MSR. I 592 byte per contesto includono
512 byte FP allineati a 16. Il nuovo task non eredita payload x87/XMM precedenti.

Le funzioni sono private: il chiamante deve avere oggetti tipizzati stabili,
owner distinti, stack vivo RW/NX con guardie, codice RX e CPU gia verificata.
`stack_authority` e `task_start` richiedono implementazioni native prima del link
kernel. Il test fornisce implementazioni Windows: non validano PTE del kernel.
Nessun prestito Rust attraversa uno switch. IF/DF, esclusione IRQ/NMI, lifetime
dei task, assenza di TLS e policy SSE/x87 devono essere garantiti dal dispatcher.
La ripresa non e una validazione di puntatori user e non intercetta page fault.

## Fonti primarie e valori verificati

Edizione Intel SDM 093, settembre 2026, scaricata dalla
[raccolta ufficiale](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html):

- [Vol. 1](https://cdrdv2-public.intel.com/929351/253665-093-sdm-vol-1.pdf):
  capitolo 21 CPUID.1.EDX APIC9/FXSR24/SSE25/SSE2 26;
  §§3.4.3.2–3 DF10/IF9; §11.6.6 MXCSR_MASK, default 0xFFBF se zero.
- [Vol. 2A](https://cdrdv2-public.intel.com/929353/253666-093-sdm-vol-2a.pdf):
  FXSAVE/FXRSTOR pp. 3-409–419, Table 3-48 formato REX.W=1,
  512 byte allineati a 16; FCW0, MXCSR24, MASK28, ST32, XMM160.
  Voci FNINIT/FLDCW/FNSTCW/LDMXCSR/STMXCSR per inizializzazione e controlli FP.
- [Vol. 3A](https://cdrdv2-public.intel.com/929359/253668-093-sdm-vol-3a.pdf):
  §2.5 CR0.EM2/TS3/WP16/PG31, CR4.OSFXSR9/LA5712/CET23;
  §13.4.4 e Figure 13-5 IA32_APIC_BASE MSR 0x1B, BSP8.
- [Microsoft ABI x64](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170):
  GPR non volatili e XMM6–15, controlli FP, shadow space32, stack allineato16.
- [VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc):
  il test riserva 64KiB+due pagine e committa solo la parte centrale RW.

Copie e hash PDF in `kernel/build/scheduler-references/`:

| PDF | SHA256 |
|---|---|
| Vol1 | F6FFC430DFA2DADA4F791B1ECB462810E59827BEFE9A9C23F9160941EEA642F9 |
| Vol2A | 87C5ACB6F27E24D9D364841A0D2346C91A8482E36F403409954E2D2A8C817BAC |
| Vol3A | 310C6A64513E8095ABFC32B6A158539560F499D53444050343022F7DDA240211 |

## Evidenza finale

Comando: `arch/x86/kernel/scheduler/scripts/test.ps1`.
Risultati dell'immagine host finale in
`kernel/build/scheduler-context-cb8fdad2f96d48cabe056ea25662c976/`.

| Gate host | Risultato |
|---|---|
| MSVC C /W4 /WX, MASM /W3 /WX | Nessun warning |
| Stato/geometria/CPU, C con AddressSanitizer | 10 esecuzioni consecutive, 55.500 controlli ciascuna |
| Switch Assembly effettivo, senza sanitizer | 10 esecuzioni consecutive, 4.004 switch ciascuna |
| Registri e stato | 8 GPR, XMM6–15 entrambe le meta, FCW e MXCSR con pattern distinti, canary locali |
| Guardie Windows | 4 access violation previste per esecuzione, SEH solo sullo stack Windows originale |
| Rilascio risorse | 2 riserve VirtualAlloc rilasciate per esecuzione, dopo fine dei task |
| Cppcheck 2.22 | Zero error/warning/performance/portability; diagnostica completa in cppcheck.xml |
| Disassembly | FXSAVE64/FXRSTOR64 con REX.W verificato; nessuna istruzione AVX/AMX rilevata negli oggetti esaminati |

Totale finale: 555.000 controlli di stato e 40.040 switch reali, 40 fault guardia
previsti e 20 riserve rilasciate. Non sono dieci boot QEMU.
Cppcheck segnala tre style (guardie fixture non lette, callback chiamato da
Assembly) e informazioni su header di sistema mancanti/checkers: il suo modello
Windows/SEH non sostituisce l'esecuzione nativa.
Gli hash dei nove sorgenti/script finali sono in `sources.json`.

ASan strumenta il codice C del test di stato; Assembly non strumentato.
Il test con cambio di stack non usa ASan: non sono implementati hook sanitizer
per fiber. Accounting VirtualAlloc non e Valgrind ne misura leak di RAM fisica.
Copertura linee/branch: N/D. CR/MSR ring0: mock; il rifiuto CPL3 e eseguito davvero
e lascia lo snapshot invariato prima di qualsiasi lettura privilegiata.

## Debug e gate successivo

Il primo fixture metteva il contesto nella fascia riservata alle guardie:
rifiuto corretto; aggiunte fasce dedicate nel fixture. Corretto anche un ritorno
a capo PowerShell che interrompeva l'argomento della ricerca nel disassembly.
Le prove sopra sono successive a entrambe le correzioni e ai nuovi casi limite.

Prima dell'integrazione occorrono stack nativi con lease e PTE RW/NX, guardie
assenti, rollback completo, readback e verifica anti-alias dei mapping.
La geometria proposta non deve sovrapporsi al LAPIC (leaf288) o alle altre
risorse vive. Poi dispatcher C collegato all'owner Rust e gate CPU/IRQ/NMI.
Solo dopo: build kernel finale, scenari di errore e almeno dieci boot QEMU
consecutivi con marker e controlli di contesto. Hardware fisico non provato.
