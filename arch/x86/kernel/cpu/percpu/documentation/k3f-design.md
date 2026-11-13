# K3-F — terminale iniziale per fault e NMI degli AP

29 ottobre 2026. Opzione B scelta dall'utente: sistema di panic SMP completo.
La proposta A resta come confronto, non e il contratto da implementare.
Il progetto ampliato e in [k3f-smp-design.md](k3f-smp-design.md).
Snapshot della proposta iniziale. Dal 30 ottobre il backend scelto è
implementato e verificato sul BSP: [prove e limiti](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu).
Il gate SMP richiede ancora l'avvio e il collaudo degli AP.

## Riscontri nel codice e confine del blocco

K3-D/E hanno predisposto GDT/TSS, tre IST e contesti software privati,
senza caricarli su un AP. La pagina metadati di ogni unita AP contiene
176 byte di descrittori; il resto e attualmente verificato come zero.

Il terminale BSP attuale normalizza il frame in Assembly e prende una
guardia globale con scambio atomico prima di entrare in C. Un secondo
ingresso si arresta senza C o seriale. Questo non dimostra una race della
guardia: significa che la diagnostica ammette un solo vincitore globale.
La successiva emissione usa il servizio seriale ordinario.

La IDT iniziale BSP viene preparata su IST1, poi separa NMI su IST2 e
double fault su IST3. L'attivazione/verifica GDT/TSS corrente controlla
gli oggetti e gli indirizzi del BSP. Non costituisce gia un ingresso AP.

Il nuovo blocco riguarda solo il terminale di bring-up AP in long mode,
dopo l'installazione della TSS privata e prima di poter dipendere da GS.
Non copre ancora real mode, transizione CR3 o finestra fra LGDT e LTR:
il trampolino dovra progettare esplicitamente anche quelle fasi.

## Ricerca verificata

[Intel SDM Vol.3A, 253668-078US, dicembre 2022](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§6.14.1 e Figura 6-8, gate x64 da 16 byte e indice IST di tre bit;
§6.14.2, frame a slot di otto byte con SS/RSP salvati anche a pari CPL;
§6.14.4/5, cambio stack, SS corrente nullo e puntatori IST nella TSS.
Un IST seleziona lo stack della TSS corrente: la IDT puo essere comune
con TSS e stack privati. Rientrare sullo stesso IST non offre uno stack
automaticamente nuovo per ogni annidamento.

[Linux v6.12, head_64.S](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/head_64.S):
`startup_64` e `early_idt_handler_array/common` distinguono infrastruttura
iniziale, normalizzazione del frame e handler successivo. Linux prepara
GS e puo chiamare C nel proprio ambiente iniziale; questa non e una
garanzia trasferibile al nostro ingresso AP non ancora implementato.
GPL-2.0: riferimento concettuale, nessuna copia del codice.

## A — record in RAM e arresto, proposta consigliata

Una IDT iniziale AP dedicata, condivisa e non modificata dopo la
preparazione, contiene solo i 32 vettori di eccezione. Gate ring zero,
IST1 ordinario, IST2 per NMI, IST3 per double fault. Non e la tabella IRQ
del BSP e non autorizza STI o gestione device sugli AP. Non si aggiunge
ora un mapping RO alla IDT: immutabile e un contratto software verificato.

Per ciascun AP un record fisso da 128 byte a offset 256 della pagina
metadati esistente: non sovrapposto ai descrittori, allineato a 64 byte,
senza nuove pagine o lease. Il record include indice logico, indirizzo
del frame, vettore/error code, RIP/CS/RFLAGS/RSP/SS e CR2. Schema e offset
esatti saranno congelati con assert C/Assembly e prove di formato.

L'ingresso Assembly identifica l'unita dalla posizione del proprio frame
sull'IST, verificando indice, intervallo e capienza prima di toccare il
record. Non usa GS, stack BSP, heap, lock, servizi di logging o codice C.
Un frame fuori dalle geometrie ammesse arresta la CPU senza tentare una
scrittura su un indirizzo ricavato arbitrariamente.

Il record ammette un solo produttore vincente per AP. Pubblicazione con
stati EMPTY, WRITING e COMPLETE e lettura acquire sul BSP; primitive e
ordering devono essere verificati sulla fonte ufficiale prima del codice.
Un secondo ingresso non azzera ne sovrascrive un record gia acquisito.
WRITING rimasto incompleto non e un rapporto valido. Nessun riuso o reset
dopo il commit delle risorse; solo il rollback pre-avvio puo ritirarlo.

Il protocollo protegge la pubblicazione del record, non promette di
recuperare il frame originale se una nuova eccezione ha gia riutilizzato
lo stesso IST prima della sua copia. Un annidamento puo produrre record
incompleto o frame non valido: entrambi sono fallimenti diagnostici.
Non promette recupero da corruzione della TSS, stack assente o triple fault.

Il BSP legge i record durante il futuro controllo di avvio e stampa la
diagnostica nel proprio contesto. Il terminale AP arresta la CPU che lo
esegue; non dichiara arrestati gli altri core e non implementa un panic
broadcast. Il bring-up dovra controllare fault e deadline prima di
dichiarare pronta una CPU. Nessuna seriale concorrente dal terminale AP.

C possiede IDT/record e lifetime; Rust verifica geometria, stati e campi
scalari degli snapshot, senza dereferenziare memoria AP. Il vecchio
validatore BSP non va riusato assumendo SS sempre uguale a 16: il frame
interrotto puo contenere SS nullo in un ingresso annidato. Assembly resta
limitato a ingresso, cattura/pubblicazione e arresto senza ritorno.

Sequenza concettuale, non eseguibile:

    risorse PREPARED -> record/IDT verificati -> commit K3-E
    futuro ingresso: TSS privata -> IDT AP -> GS -> verifica ammissione
    fault: EMPTY -> WRITING -> COMPLETE -> CPU arrestata
    lettore BSP: COMPLETE + campi validi -> diagnostica; altri stati -> errore

L'ordine completo di caricamento GDT, segmenti, LTR, IDT e GS, comprese
le finestre non ancora protette, appartiene al progetto del trampolino.
La sequenza sopra ne esprime i prerequisiti, non e gia un entry point.

## B — infrastruttura generale di panic SMP subito

Unificare adesso handler C, contesti attivi, seriale d'emergenza, record
per CPU e arresto coordinato degli altri core richiede piu contratti,
inclusi ingressi prima di GS, ricorsione e CPU che non rispondono.
Produce una base piu generale, ma anticipa sincronizzazione e recovery
prima che esista il primo avvio AP. Richiede un progetto piu ampio.

## Perimetro da concordare e prove

Modulo `arch/x86/kernel/cpu/percpu/`, sottocartella funzionale per i fault;
soli adattatori necessari a risorse/descrittori, interfacce eccezioni,
bootstrap, build e test centralizzati. Nessuna modifica a scheduler,
device, allocatore, terminale BSP ordinario o vecchio OS non necessaria.
Un'estensione ulteriore richiede prima una nuova discussione.

Host C/Rust: geometrie 1/2/4/64, frame al bordo, indici invalidi,
overlap, bit riservati, stato parziale, campi incoerenti e rollback.
ASan sul C host e accounting guest separati; coverage N/D se non misurata.

QEMU positivo: almeno dieci boot finali per preparazione senza attivare AP,
verifica RAM/IDT/lease e regressioni dei percorsi K3-E interessati.
Diagnostici isolati: BSP con una TSS/IST AP di prova, record osservato dal
monitor dopo eccezione reale, senza ritornare al boot ordinario. Esercitare
fault senza error code, page fault, NMI, double fault e rifiuto di record
incompleti; distinguere i test sintetici dai casi hardware realmente
prodotti. La macchina ferma e silenziosa non basta come prova.

Il prestito della TSS AP al BSP e solo un banco di prova: non dimostra
SMP, esecuzione su un AP o avvio di quattro core. Le prove simultanee e
quelle prima/dopo GS richiedono il successivo ingresso AP reale.

Per questa fase solo documentale: fonti, link, coerenza e diff; nessun
nuovo test runtime dichiarato eseguito. Implementazione dopo la scelta.
