# Revisione stack principale kernel — 3 ottobre 2026

Modulo: `arch/x86/kernel/stack/`, con integrazione necessaria in mapper stack,
TSS, continuazione kernel e script di build/test. Nessuna modifica al loader EFI.

## Risultato

Il kernel usa uno stack proprio da 64 KiB RW/NX con due guardie. RSP0 della TSS
punta al nuovo top. Il passaggio e senza ritorno e non copia frame C. Dalla nuova
continuazione vengono revocate le sedici PTE dello stack loader, con INVLPG.
Le pagine fisiche del loader restano riservate, senza rilascio o reclamazione.

Riutilizzato il mapper degli IST con un secondo profilo e lease distinto.
Rust valida layout, limiti fisici checked e RSP. C mantiene ownership/pin/fasi;
Assembly effettua il cambio di RSP con l'allineamento Microsoft x64.

## Controlli e fix

- Parent paging verificati prima di leggere il mapping loader.
- Mapping loader confrontato per intero: sedici frame contigui, RW/NX supervisor
  e due guardie esattamente zero. Nessuna cancellazione di mapping alterati.
- Vecchio backing escluso dai free extent e lease vivi prima di allocare/azzerare.
- Owner statici, intervalli separati, profili/fasi/lease/pin e budget verificati.
- Commit permanente prima di RSP0/RSP. Rollback consentito soltanto prima.
- Corretto il rilascio per RSP disallineato dentro il nuovo intervallo: anche
  questo caso deve bloccare la liberazione di uno stack ancora attivo.
- Esclusi dai buffer anche i due guard del vecchio stack, prima degli accessi.
- RSP0 aggiornato nella TSS busy senza nuovo LTR, sotto UP/CPL0/IF=0; IST e
  descriptor preservati. Validazione con valori RSP0 iniziale e nuovo distinti.
- Callback statico senza argomenti, nessun ritorno al vecchio stack. Ritorno
  inatteso fermato in Assembly con marcatore dedicato, senza dereferenziare
  il ritorno lasciato sullo stack loader.
- Corretto il naming degli oggetti di build: due sorgenti main.c in cartelle
  diverse ora producono oggetti distinti usando il percorso nel nome.
- Corretto il fixture host WB: base a 1 MiB, distinta dalle tabelle simulate.
- Aggiunti test mirati retired/guard-high/execute/return. Le varianti con fault
  non sono installabili nella ESP ordinaria. Rimossi i bounds loader obsoleti
  dai controlli RSP/frame e dal probe di overflow dello stack principale.

## Evidenze finali

Build MSVC C11 `/W4 /WX`, MASM `/W3 /WX`, Rust `-D warnings`, link senza CRT
ed ELF verificato. Test host completi superati:

- 1.120 corruzioni delle PTE principali e 1.120 del mapping loader respinte;
- acquisizione/zeroing/pin, rollback, OOM, alias, owner copiato e RSP errato;
- overlap fisico con free extent o lease vivo respinto prima delle scritture;
- 128 cicli di riuso dello stack principale senza perdita del budget;
- regressione IST, inclusi 1.000 bit corrotti respinti e 256 cicli di riuso;
- 3.696 alterazioni di bit GDT/TSS/GDTR con RSP0 iniziale o nuovo;
- regressione contratto, memoria fisica, finestra, pool oggetti e IDT;
- 19 test Rust kernel, incluso il nuovo profilo principale.

**18/18 QEMU** sullo stato finale: ready 128/256 MiB, panic, quattro fault della
finestra, UD/GP/DE, push sulla guardia inferiore principale, fault annidato,
DF, NMI e quattro prove dello stack principale. Controllati CPU, CR3, GDT/TR,
RSP0 in memoria, nuove PTE RW/NX, guardie e diciotto PTE loader zero.
Ogni trace `-d int,cpu_reset` e privo di triple fault.
QEMU: `11.1.0 (v11.1.0-12130-ge470268ff4)`, q35/TCG/CPU max, EDK2 UEFI.

Log locale ignorato da Git:
`kernel/build/firmware-195153a83d404696a8dd6e2db30963ad/`.
Test host: `kernel/build/main-stack-host.log`.
Compilato, installato e testato ready hanno lo stesso SHA256:
`515797C3DF0612994F640E56F370339EBCE3BA0EB80B4FEA1C6BCA9E5F52A997`.
EFI invariato: `07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3`.
ELF 49.152 byte su disco, 53.248 byte residenti nei segmenti arrotondati.
Stack persistenti kernel 114.688 byte (28 pagine). Nessuna nuova tabella paging.

## Limiti

UP, IF zero, handler terminali, nessun IRQ/scheduler/SMP/CET attivo o unwind
attraverso il cambio. RSP0 e verificato in memoria; non e ancora provato un
ingresso da ring 3, che non esiste nel kernel attuale. Richieste sedici pagine
contigue; OOM arresta il bootstrap. Nessun recupero da corruzione arbitraria.
Il cambio stack non e un context switch di thread.

Tabelle paging, contratto, mappa e altre risorse del loader restano persistenti.
Lo stack loader e unmapped, ma non reclamato. Nessuna prova su hardware fisico.
Le modifiche preesistenti a direttive-locali, .gitignore, SKILL.md e cartelle IDE sono
escluse dal checkpoint. Moduli C principali entro 200–400 righe; header,
validatori Rust e adapter Assembly compatti.

Procedura e fonti ufficiali:
[stack principale autonomo](../../kernel/stack/main-stack.md).
