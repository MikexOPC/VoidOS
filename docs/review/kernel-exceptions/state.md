# Revisione: IDT e diagnostica terminale del kernel

Data: 3 ottobre 2026. Base del lavoro: commit `57e6763`.

## Risultato verificato

Il kernel usa la propria IDT a 32 gate, caricata con LIDT e riletta con SIDT.
Gli ingressi Assembly sono nel codice RX del kernel. Il C prepara e verifica
gate e IDTR; Rust valida gli indirizzi dei terminali e il formato del frame.
Le eccezioni producono una diagnostica autonoma sulla seriale e arrestano la CPU.

La task e completa rispetto a questo obiettivo. L'avvio di un kernel reale e
gia raggiunto: non manca una percentuale di bootloader prima di iniziare il
kernel. Questo non misura la completezza di un OS o il supporto a PC reali.

## Test dello stato finale

- `kernel/test/host.ps1`: regressione C di contratto, memoria fisica, mapping,
  pool e logging; test IDT con corruzione dei 512 byte dei gate e dei 10 byte
  dell'operando, rifiuto alias/null/disallineamento e preflight host negativo.
- Rust: 14 test complessivi, di cui 3 nuovi su indirizzi e frame delle eccezioni.
- Build C `/W4 /WX`, Assembly `/W3 /WX`, Rust `-D warnings`: superata.
- QEMU: 11/11 scenari finali. Ready con 128/256 MiB, panic, esecuzione NX,
  lettura dopo unmap, guardie inferiore/superiore, UD2, selettore fuori GDT,
  divisione per zero, push nella guardia dello stack kernel.
- Confronto dell'intero frame a 56 byte in memoria con la diagnostica, controllo
  IDTR/CS/SS/CR3, RAX terminale, stack IST, CR2 e codici d'errore attesi.
- Log `-d int,cpu_reset`: nessun triple fault; colori limitati alle etichette.
- `git diff --check`: superato.

La lettura del monitor usa sette qword per il frame: otto attraverserebbero il
confine superiore dello stack IST. Il test Rust del double fault richiede zero
anche se il vettore appartiene al gruppo che riceve un error code dalla CPU.

## Artefatti della prova

Directory QEMU finale:
`kernel/build/firmware-ec38678b058b4151930b732a5a8e94b5`.

ELF normale: 32.768 byte su disco, 40.960 byte residenti in quattro segmenti.
Rispetto al checkpoint precedente: una pagina residente aggiuntiva, 4 KiB.
IDT statica: 512 byte; operando IDTR: 10 byte. Nessuna nuova allocazione di
pagina o tabella paging durante l'attivazione; IST di 16 KiB gia persistente.

SHA-256 ELF costruito, installato e copiato nello scenario ready finale:
`184AADA5FCB9A345A7C46A5254B95931C54572D5B787A722387CBDE7BC888211`.

Il firmware EFI usato e invariato:
`07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3`.

Log host `kernel/build/exceptions-host.log`:
`FE0DD4A58DF7B40DF319340827C6E232165B030E7263F525ECA969CD2BC4DA0B`.

Log build `kernel/build/exceptions-build.log`:
`24640354C4BA651C616FE9B68D94129BDAD4EEBCB54DE1CC2A1A96890F99CED3`.

Log firmware: `kernel/build/exceptions-final-firmware.log`. Gli artefatti restano
in build e non vengono versionati.

## Limiti e prossimo passo

GDT/TSS/IST restano prestiti del loader, le cui pagine non sono reclamate.
Il kernel verifica i selettori attivi e IDTR, ma non valida indipendentemente
il contenuto della TSS. Tutti i terminali usano lo stesso IST; eccezioni annidate,
NMI durante la diagnostica e machine check non sono dimostrati robusti.
Non sono stati provocati double fault, #CP, #VC o #SX.

IF resta zero. Non ci sono ritorno IRETQ, recupero dei fault, IRQ, APIC, SMP,
scheduler o esecuzione ring tre. Nessuna prova e stata fatta su hardware fisico.

Il prossimo passo coerente e rendere GDT, TSS e stack d'emergenza risorse
esplicitamente possedute dal kernel, separando gli stack critici prima di
abilitare interrupt o iniziare un percorso recuperabile.

Dettagli e fonti ufficiali: [IDT terminale](../../kernel/exceptions/terminal-idt.md).
