# Contesti per CPU, avvio degli AP e servizi inter-CPU

[Contratti del consolidamento](../../../../../../docs/design/consolidamento.md#contesti-per-cpu-avvio-ap-e-ipi)
e [prove per profilo](../../../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).
Per le fonti estese, i design precedenti sono in questa cartella al
commit `8bad38a`.

## Identità, contesti e risorse

`X86_CPU_IDENTITIES` è il registro immutabile derivato da MADT. Separa
indice logico, APIC ID e UID ACPI, con il BSP all'indice zero e al
massimo 64 CPU. Il percorso operativo usa xAPIC. `X86_CPU_LOCAL`
contiene il self-pointer, l'indice, lo stato e il contesto dei lock.
GS punta al record attivo, dopo la promozione del contesto iniziale
sul BSP. Il passaggio conserva il binding del clock e richiede che
il contesto non abbia lock acquisiti. Il clock ACPI PM serve anche
le attese con IF=0. Maschera, deadline e limite di polling sono
espliciti.

L'arena AP possiede dati e tabelle pinned. Ogni AP ha stack, IST e
descrittori privati, con guardie non mappate. `X86_AP_TRANSITION`
possiede la root temporanea e i riferimenti alle lease basse.
La riserva del codice SIPI è sotto 1 MiB. La pagina dati è separata
dal codice, che resta in sola lettura ed eseguibile. La root di
transizione condivide i rami alti necessari e resta senza slot
ricorsivo proprio. Il BSP continua sulla sua root.

## Installazione e sigillo

Il BSP entra con IF=0 e profondità lock zero. `XS_ENTRY_RECORD`
lega indice, APIC ID e token alle basi fisiche, alle root e al
contesto alto. `XB_INSTALLATION` possiede il template installato e
il record. Prima di scrivere prende in prestito le lease, prepara
il blob rilocato e rilegge codice e dati installati. Il prestito
impedisce il rilascio mentre l'immagine può essere usata.

`XJ_GROUP` coordina il commit di immagine, tabelle, arena e contesti.
Prima del punto irreversibile l'errore può cancellare l'immagine
installata e annullare le risorse ancora reversibili. Dopo il
sigillo il BSP prepara il panic e pubblica il descrittore di
ammissione con release. I lettori lo acquisiscono con acquire.
La pubblicazione è unica e chiude i getter di preparazione. Un
guasto dopo il sigillo lascia le lease trattenute e rende terminale
l'avvio. Non si riusa una pagina che un AP potrebbe ancora leggere.

## Ingresso e ammissione

La macchina Rust `Startup` usa un token per indice e generazione.
Il CAS ammette solo la fase attesa dello stesso token.

| Stato | Condizione |
| --- | --- |
| PREPARED | Il BSP ha riservato il prossimo AP e può annullare prima del commit. |
| COMMITTED | L'immagine e le risorse sono sigillate. |
| ARMED | Il tentativo ha superato la preparazione del trasporto. |
| ENTERED | L'ingresso è attribuito all'AP atteso. |
| PRIVATE | L'AP usa contesto, descrittori e stack privati. |
| PARKED | La conferma permette al BSP di avanzare al prossimo AP. |
| QUARANTINED | Il tentativo dopo il commit è fallito. Le risorse restano trattenute. |

Il trasporto xAPIC usa INIT/SIPI e il passaggio previsto via NMI.
Il trampolino attraversa 16, 32 e 64 bit, controlla il record e
raggiunge il prefisso alto. Nel passaggio protetto l'NMI resta
bloccato fino a IRETQ. Il prefisso usa lo stack privato, installa
GDT/IDT intermedie, passa alla root operativa, imposta TSS e GS e
prepara x87/SSE prima del ponte C. L'ammissione controlla identità,
token, root e contesto del descrittore pubblicato. Il ritorno
validato installa il percorso finale e raggiunge il loop di
servizio. Il BSP verifica PARKED prima di aggiornare la sola
pagina dati per l'AP successivo, mantenendo codice e tabelle.
Alla fine confronta la maschera online con il registro.
Con un solo core prepara il panic BSP e salta l'avvio AP.

## Mailbox IPI e shootdown

`XIS_NATIVE` possiede l'IDT di servizio e i contatori per CPU.
Il vettore di servizio è 0xe1. L'entry Assembly salva il contesto,
la policy Rust verifica il frame e l'handler verifica identità e
generazione. Il loop parcheggiato può ricevere IRQ con IF=1.
Il BSP attende READY, pubblica il comando con release e invia
l'IPI. L'AP acquisisce la richiesta e pubblica l'ACK dopo EOI.
Il PING di boot conferma che ogni AP ammesso serve il canale.

Nel flush il BSP è l'autore delle modifiche alle tabelle condivise.
Dopo la modifica invalida localmente e pubblica un comando per la
generazione corrente. Ogni AP controlla root e modalità, poi
ricarica CR3. Il percorso richiede PGE e PCIDE a zero, così il reload
invalida il contesto usato dal profilo. Nessun lock o allocazione
nell'handler. Il chiamante può restituire frame e tabelle solo
dopo tutti gli ACK. Un timeout dopo la modifica arresta il gruppo
con le lease trattenute. Il ramo di test che omette ACK e le
rimappature di prova sono diagnostici separati.

## Panic coordinato

Il coordinatore Rust tiene maschere known e online, owner,
destinatari, ACK, mancanti e stato dei record. L'entry Assembly
cattura il contesto prima del C. Il percorso usa stack di
emergenza e record privati e non dipende dal dispatcher.
Il primo owner eletto acquisisce la maschera dei peer online,
invia NMI diretti e aspetta entro deadline e budget. I peer
pubblicano il record e la conferma prima dell'arresto. Il reporter
congela il risultato, tenta l'output seriale delimitato e conserva
la diagnostica in RAM se la seriale è indisponibile. Un ingresso
ricorsivo non sostituisce il primo rapporto. I rendezvous con
origini concorrenti e le iniezioni di guasto appartengono ai test.

## Fonti hardware e perimetro

Intel SDM Vol. 3A §§4.10.4–4.10.5 per il reload di CR3, capitolo
Interrupt and Exception Handling per NMI, IST e IRETQ e capitolo
APIC per ICR, INIT/SIPI, EOI e consegna NMI. Il clock di riferimento
è l'ACPI PM Timer. Gli offset e i bit usati sono citati accanto alle
istruzioni o agli accessi nei backend. NMI prima della IVT iniziale,
x2APIC operativo, hotplug e hardware fisico non sono verificati
nel profilo documentato. Il parcheggio degli AP non attiva lo
scheduler preemptive ordinario su quei core.
