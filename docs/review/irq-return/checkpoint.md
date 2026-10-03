# Checkpoint: IDT completa e controller interrupt

## Risultato

La IDT completa conserva i 32 terminali e i loro IST. I nuovi ingressi
salvano GPR e stato FXSAVE64, chiamano un dispatch bounded e ritornano
con IRETQ solo per self-test e spurious. Il test 0xf0 verifica 15 GPR,
RSP, DF e XMM0/XMM15 dopo una modifica SIMD intenzionale nello stub;
0xff esercita il ritorno senza EOI. IF resta zero.

I/O APIC e PIC legacy sono mascherati. Le rotte I/O APIC ricevono una
destinazione BSP e un vettore legale, mantenendo chiusa la consegna.
LVT mascherabili, TPR e SVR sono programmati con readback. Gli NMI
locali dichiarati nella MADT rimangono abilitati sui LINT pertinenti
al BSP, con il terminale NMI protetto da IST2.

Il preflight controlla capacita, riserve, alias, PAT e assenza di
richieste/servizi pendenti. I prestiti MMIO vengono revocati e il
budget dell'allocator non cambia durante la configurazione. Nessun
frame viene allocato dal nuovo blocco.

## Correzioni della revisione

- La prima campagna ha rilevato che mascherare anche LINT1 impediva
  l'iniezione NMI QEMU. La configurazione distingue ora i NMI MADT
  dagli IRQ mascherabili; wildcard UID, UID BSP, polarita e conflitti
  sono testati. Il profilo rifiuta NMI locali level prima delle scritture.
- Il layout LVT usa un match Rust bounded, evitando una dipendenza
  a panic_bounds_check in un oggetto freestanding senza runtime.
- Gli encoding controller sono separati dai validatori del frame IRQ
  nelle rispettive cartelle funzionali Rust.
- La verifica SIMD confronta tutti i byte delle sentinelle; il backend
  richiede SSE2 e OSFXSR/OSXMMEXCPT, con OSXSAVE disabilitato.
- ISR/IRR e stato di consegna vengono controllati prima di modificare
  rotte; nessun EOI viene usato per nascondere stato firmware pendente.
- La nuova IDTR viene riletta dopo i self-test; rollback non verificabile
  arresta il kernel. I gate NMI e double fault conservano IST2/IST3.

## Evidenza finale

- Regressione host kernel completa: modello C della configurazione con
  un errore alla volta su ogni lettura/scrittura, readback errato, rotte
  pendenti, PIC, capacita corrotte, 120 ingressi e tutti i LVT supportati.
- IDT: ogni byte della tabella completa corrotto e riconosciuto, alias,
  target fuori intervallo, ambiente invalido e rollback riuscito/fallito.
- 41 test Rust complessivi; quattro nuovi per frame/IDTR e encoding LVT/NMI.
- Immagine ELF valida e sette mutazioni invalide verificate.
- 23 scenari QEMU, con monitor di gate, IDTR, LVT, ISR/IRR, PIC e tutte
  le 24 rotte I/O APIC. Incluse regressioni NMI, double fault, NX e guardie.
  Directory finale: kernel/build/firmware-49d8f072ce204ea6b3c99cd04ea8e63f.
- SHA256 ELF finale: 0C0DD4C201F1413A3E7C21A6B90BB58A6938CFD7104B03279650856DE9D3F4AA.

ELF 98304 byte, segmenti residenti arrotondati a pagina 110592 byte:
cinque pagine residenti aggiuntive rispetto al checkpoint MMIO.
Inventario normale: 15 tabelle, 116 foglie. La IDT aggiunge 4096 byte
di storage persistente, oltre a operando IDTR e contatori; la superficie
restante comprende stub, tabella di indirizzi e codice di configurazione.
FXSAVE occupa 512 byte sullo stack del solo ingresso corrente, con
32 byte separati di shadow space. Non e una misura del massimo consumo
totale dello stack C/Rust.

I test includono le modifiche utente preesistenti nei file diagnostici
e nell'header ACPI, preservate fuori dal commit. L'hash identifica la
working tree testata, non una ricompilazione del solo commit.

## Prossimo passo e limiti

Rimane la task del primo driver seriale a IRQ: scelta rotta GSI/vettore,
ownership e durata dei mapping per gli handler, ACK UART, EOI LAPIC e
abilitazione controllata di una sola sorgente. La seriale attuale usa
ancora polling; nessun IRQ hardware con ritorno e dichiarato provato.
La stima di una task dipende dai problemi emersi nella prova reale.

UP, ring zero, niente AVX/XSAVE o user mode. Il profilo APIC standard
non configura lo spazio esteso AMD. NMI/fault durante il setup sono
terminali; un errore hardware puo lasciare rotte parzialmente configurate
e richiede arresto con IF=0, senza riaprire quelle del firmware.
Hardware fisico non testato. Non si attribuisce una percentuale a un
OS completo sulla base di questi soli test.

[Contratto, limiti e fonti](../../kernel/interrupts/irq-return.md).
