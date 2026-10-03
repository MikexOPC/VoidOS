# Checkpoint: pool di oggetti persistenti

## Implementazione e revisione

Il nuovo wrapper `arch/x86/mm/arena/` collega il pool comune a un
mapping kernel vivo da 64 KiB. Un binding esclusivo impedisce unmap
e rilascio dei frame finche il pool conserva il buffer. Le API verificano
owner non spostabile, lease, borrower, PTE, pin, dimensione del buffer
e handle prima degli accessi. La matematica degli oggetti continua a
usare i validatori Rust del pool comune, senza duplicarne la logica.

Release azzera l'oggetto senza liberare frame. Detach e consentito solo
senza oggetti, scollega prima il pool e poi il binding. NextId viene
preservato alla riapertura. Un secondo borrower, copie obsolete,
metadati discordanti e alias vengono respinti. Un errore durante
l'inizializzazione del pool annulla il binding.

La revisione ha controllato overflow, alias degli output rispetto a
tutti gli owner, allineamento, riapertura con ID preservati e ordine
di cleanup. Non si dereferenziano oggetti attraverso un mapping ormai
rilasciato: il mapping resta pinned e il wrapper ne verifica l'identita.
I puntatori C restano prestiti che il chiamante deve scartare prima
del rilascio; non viene dichiarata sicurezza contro codice kernel ostile.

## Evidenze sullo stato corrente

- Host: `kernel/build/arena-host.log`, regressione completa superata.
- Nuova suite: binding esclusivo, unmap respinto con copie dell'handle,
  OOM di byte e 64 record, riuso, owner copiato e alias.
- 256 cicli di allocazione e 64 riaperture senza riavvolgere gli ID.
- 256 alterazioni degli handle respinte senza modificare pool o budget.
- Backend non allineato: attach fallisce, binding annullato, pool non
  pubblicato; il mapping resta del chiamante e puo essere riusato.
- 23 test Rust esistenti superati, inclusi i bounds usati dal nuovo pool.
- MSVC C11 `/W4 /WX`, MASM `/W3 /WX`, Rust `-D warnings`, nessun CRT,
  import, TLS o rilocazione.
- Convertitore ELF: immagine valida e sette mutazioni invalide verificate.
- 22/22 scenari QEMU, nessun triple fault. Contesto e inventario letti
  dopo switch allo stack kernel e revoca dello stack loader.
- Monitor: oggetti allineati, disgiunti e dentro il secondo mapping,
  contratto con firma/versione corrette e 144 byte di oggetti vivi.
- Regressioni delle PTE reali, unmap, guardie, NX, NMI, double fault,
  fault annidato, panic e 128/256 MiB superate.

Log QEMU: `kernel/build/firmware-3646c5e2050d48fe8af3c6b58bdef72f/`.
ELF costruito, installato e copiato nel caso ready, SHA-256:
`079F61F8A39AA8E320C57A47ABBBEB34BB1001E3A10C3E9B84F880C5F1A60B0E`.
Disco 61440 byte, segmenti residenti arrotondati 65536 byte: una pagina
in piu rispetto al checkpoint del mapper. EFI non ricostruito.

## Costi e limiti

Arena owner 2152 byte, mapper da 160 a 192 byte; handle oggetti gia
previsti dal pool. Si riusa il blocco da 64 KiB esistente: nessun nuovo
frame per contesto (120 byte) e inventario (24 byte). Il budget fisico
rimane invariato durante allocazione, rilascio e riuso degli oggetti.
Moduli principali: wrapper 200 righe, test 205, kernel/main.c 329.

Questa e una base bounded UP/IF=0, con 64 record e 64 KiB. Non e un
heap generale, uno scheduler o isolamento microkernel. La fotografia
UEFI e le tabelle paging restano ereditate e riservate. Host e QEMU
non costituiscono validazione su hardware fisico.

Il prossimo blocco puo definire il registro delle risorse del kernel
e delle pagine di paging ancora vive, prima di espandere le tabelle o
reclamare memoria loader. Interrupt e scheduler richiedono prerequisiti
di piattaforma e restano task distinti.

[API, ownership e durata di vita](../../kernel/memory/persistent-objects.md).
