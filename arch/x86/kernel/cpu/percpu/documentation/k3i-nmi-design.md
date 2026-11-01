# K3-I — finestra NMI: scelta A approvata, prima prova CPU passata

30 ottobre 2026. L'utente ha approvato A: passaggio dentro handler NMI,
con prova dedicata. Il diagnostico AP dedicato resta la destinazione.
Primo esperimento CPU in [k3i-nmi-validation.md](k3i-nmi-validation.md);
nessun trampolino del kernel attivato e nessun AP avviato.

## Fonti primarie verificate

[Intel SDM Vol. 3A, 253668-078US, dicembre 2022](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§6.7 distingue NMI hardware/APIC da INT 2; §6.7.1 mantiene il blocco degli
NMI successivi fino a IRET e precisa che persino IRET faulting li sblocca.
§10.7.1–2 tratta IVT iniziale e finestra prima della sua installazione;
§10.8.5.2 richiede di impedire eventi nel cambio del formato IDT.
Questi requisiti non sono soddisfatti dalla sola istruzione CLI.

L'uso del blocco NMI per attraversare i modi è una proposta derivata da
queste regole, non una sequenza Intel pronta o una funzionalità già provata.
Non copiare codice Linux né assumere garantito l'effetto in tutte le VM.

## A — proposta consigliata: passaggio dentro un vero handler NMI

1. BSP prepara codice, dati, IVT privata, GDT e root; commit prima di INIT.
2. AP inizializza stack basso e IVT in modo reale, poi pubblica NMI_READY
   per la generazione attesa. Nessun accesso a GS o chiamata C in questa fase.
3. BSP osserva la pubblicazione con acquire e invia un solo vero NMI
   unicast via LAPIC, con clock e limite di controlli. INT 2 è escluso.
4. L'handler reale verifica il record e attraversa protected/long mode
   senza IRET intermedio. Gli ulteriori NMI devono restare pendenti.
5. Prima di sbloccarli: root operativa, stack privato, IDT a 64 bit,
   GDT/TSS/IST e GS verificati, record panic privato e percorso terminale
   pronti. Un frame IRETQ costruito e validato porta all'ingresso alto.
6. Solo dopo l'uscita verificata dalla transizione si pubblica PARKED:
   nessun riferimento al record condiviso deve essere ancora necessario.

Il record di fase bassa è separato dallo stato principale a tre bit:
non si aggiungono valori oltre sette al formato attuale. Il sottoprotocollo
deve essere legato allo stesso token e non consentire una seconda traversata.
Un NMI già arrivato dopo IVT_READY può prendere lo stesso ingresso; la
sequenza deve evitare una seconda inizializzazione e non dipendere dal fatto
che il NMI sia proprio quello inviato dal BSP.

Timeout, clock invalido, conferma errata o invio dubbio: quarantena, niente
AP successivo e niente rilascio/riscrittura delle pagine. IF resta disabilitato
nella fase iniziale. I fault non sono mascherati da questo meccanismo: GDT,
PTE, puntatori e IRETQ richiedono controlli propri e prove negative.

## Limite iniziale che A non elimina

Prima del caricamento di stack/IVT esiste ancora una finestra. Intel
§10.7.2 richiede supporto della piattaforma in quella fase. Il diagnostico
QEMU deve controllare esplicitamente le sorgenti NMI: nessuna iniezione
QMP o watchdog NMI prima di IVT_READY, nessun target panic non ammesso.
Il profilo deve rifiutare configurazioni incompatibili. Questo limite
impedisce di dichiarare il percorso pronto per hardware reale generico.
Non è corretto sostenere che il protocollo NMI renda sicuro l'intero reset.

## B — alternativa: transizione diretta nel solo QEMU controllato

Nessun NMI dall'esterno o dal BSP fino all'IDT finale, con configurazione
e harness che lo impongono. Meno codice e un solo handshake, ma non prova
la gestione di un NMI pendente durante il cambio di modo. Il vincolo di
assenza degli NMI si estende a tutta la transizione, non soltanto all'inizio.

## Prove necessarie per A

Prima dell'integrazione AP: diagnostico dedicato che dimostri blocco di un
secondo NMI, ingresso reale e uscita 64 bit, stato NMI prima/dopo IRETQ,
nessuna consegna nel tratto IDT incompatibile. L'esito richiede marker e
stato CPU, non solo assenza di reset. Testare anche NMI arrivato appena
dopo IVT_READY e timeout prima della prima consegna. Gate separato da
K3-I completo; successivamente dieci avvii AP e panic SMP remoto.

Nessuna nuova dipendenza di moduli oltre al perimetro AP/LAPIC/eccezioni,
bootstrap/build/test già autorizzato. La scelta è sul sottoprotocollo NMI.

## Toolchain disponibile

NASM portabile 3.01 scaricato dal sito ufficiale in kernel/build/tools/;
nessuna installazione globale o modifica al PATH. Versione e SHA-256
verificati localmente; gli hash registrano i byte scaricati via HTTPS,
non costituiscono una firma indipendente del distributore.
[NASM bin e BITS](https://www.nasm.us/doc/nasm09.html): formato flat senza
runtime Windows e selezione esplicita 16/32/64 bit. Questa verifica non
dimostra ancora che un blob AP sia corretto o avviabile.
