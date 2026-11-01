# K3-I — componenti verificati, avvio AP ancora mancante

30 ottobre 2026. L'utente ha scelto il diagnostico dedicato prima del boot
ordinario e autorizzato gli adattatori descritti in [design](k3i-design.md).
La prima tappa implementava soltanto il coordinatore in startup/rust/protocol/,
con header C startup/startup.h e test centralizzati. I blocchi successivi
sono distinti qui sotto, con le rispettive prove. L'ingresso AP non è ancora
collegato alla build del kernel e nessun AP è stato avviato.

## Contratto concretizzato

Una sola configurazione con 1–64 CPU, BSP all'indice zero. Gli AP vengono
prenotati in ordine. Un token contiene generazione non nulla a 32 bit e
indice a 6 bit; tre bit bassi rappresentano la fase atomica. Generazione
esaurita: rifiuto, senza wrap. Il token distingue tentativi, non autentica
chiamanti ring 0: il futuro adattatore verifica identità CPU e ownership.

PREPARED -> COMMITTED -> ARMED -> ENTERED -> PRIVATE -> PARKED.
ARMED va pubblicato prima dell'invio hardware, così un AP veloce può
confermare senza attendere una scrittura BSP successiva a SIPI. Non significa
che il comando sia già stato consegnato. Errori di invio dopo ARMED richiedono
quarantena; il coordinatore non effettua I/O né misura deadline.

Il chiamante può annullare soltanto PREPARED. Un nuovo tentativo per lo
stesso indice riceve una generazione diversa. COMMITTED richiede che il
chiamante abbia già sigillato tutte le risorse: la sola atomica non pinna
memoria e non sostituisce il commit congiunto ancora da implementare.

Da COMMITTED fino a PRIVATE, timeout/fault porta a QUARANTINED, irreversibile.
Conferme tardive e prenotazioni successive vengono rifiutate. PARKED è la
conferma che nessun riferimento AP dipende più dalla memoria transitoria:
solo allora il BSP può prenotare il successivo AP. Il futuro ingresso deve
rispettare questo lifetime prima della pubblicazione; il coordinatore non
lo deduce da un valore di stato. Una gara PARKED/timeout ha un solo vincitore.

Rust usa atomiche native a 64 bit con acquire/release, senza unsafe nel
motore, allocazioni o puntatori FFI. CAS non debole per ciascun avanzamento;
la quarantena riprova al massimo cinque volte: possono interferire al più
quattro progressi monotoni. Nessuna attesa illimitata nel motore.

## Test e review

Dieci casi Rust in debug e release: capacità, sequenza fino a 63 AP,
annullamento, generazioni precedenti, conferme duplicate/fuori ordine,
quarantena da ogni fase ammessa, pubblicazione concorrente e gare fra
prenotazioni, commit/annullamento e parcheggio/timeout (128 iterazioni).
La suite C chiama l'oggetto Rust reale, prova due AP completi e il terzo
annullato/riprenotato/quarantinato. Warning trattati come errori.

La prima prova C aveva /TC applicato anche all'oggetto Rust: errore dello
script corretto separando compilazione e link, non un errore nel protocollo.
Le successive esecuzioni finali complete sono distinte da quella fallita.
Il primo tentativo PowerShell senza ExecutionPolicy di processo è stato
bloccato prima dei test. Non sono state cambiate policy persistenti.

ASan strumenta il test C, non il codice Rust. Il motore non alloca e non
possiede pagine fisiche: nessuna deduzione su leak guest o risorse AP.
Ispezione dumpbin della prima tappa Rust: sei esportazioni xs_, nessun simbolo
UNDEF. Verificati limiti file/cartelle e assenza di whitespace finale/tab.
Coverage e Valgrind N/D. Test concorrenti host non provano SMP guest né
costituiscono una dimostrazione formale del memory model.

## Record d'ingresso e collegamento al protocollo

Il record versione 1 è di 104 byte, allineato a otto. C e Rust verificano
dimensioni e offset di token, root, stack, entry e campo riservato. C
costruisce stack e indirizzo descrittori dalla geometria AP esistente:
stack fino alla pagina 17 esclusa e metadati nella pagina 48. Su rifiuto
il costruttore non modifica l'output. Non alloca o accede a indirizzi fisici.

Rust verifica identità xAPIC (ID diverso dall'indice, broadcast escluso),
token/generazione, codice/dati bassi distinti, quattro pagine della root
transitoria sotto 4 GiB, root operativa nei limiti fisici. Rifiuta alias
fra queste risorse. Verifica span canonici alti a 48 bit, allineamenti,
overflow e separazione numerica di stack, descrittori, area GS e ingresso.
Le dimensioni di area GS (448 byte) e stack (64 KiB) derivano dalle ABI
per-CPU esistenti, non da un formato hardware. Cambiarle richiede aggiornare
questo contratto. PAT ammette solo codifiche definite e PAT0 WB; riferimento
Intel SDM 253668-078US §12.12.2, Tabella 12-10, verificato online.

xs_entry_claim valida il record, confronta l'ID osservato e tenta soltanto
ARMED -> ENTERED con il suo token. Nessuna conferma fuori ordine, duplicata
o successiva alla quarantena è accettata. L'adattatore futuro deve ricavare
l'ID dalla CPU e la larghezza fisica dalle capacità verificate: passarli
come argomenti non li autentica. Deve inoltre verificare ownership, lease,
permission e corrispondenza ai record preparati; questi controlli numerici
non dimostrano che entry o puntatori GS siano mappati o autorizzati.

Sei nuovi casi Rust includono tutte le 256 codifiche di ciascun byte PAT,
estremi fisici/canonici, alias e token errati. La seconda suite C/Rust prova
il costruttore per 63 indici AP, output invariato sugli errori e collegamento
al coordinatore, inclusi identità errata e quarantena. Totale: 16 casi in
debug e 16 in release, due eseguibili C/Rust con ASan sul C e cppcheck
senza diagnostiche bloccanti. Il codice Rust non è strumentato da ASan.

Evidenza finale del record: kernel/build/ap-startup-host-86a2c7cd725e42e99c0fad55e060fef0/,
con tools.log, result.json e cppcheck.xml. L'oggetto Rust finale ha otto
esportazioni xs_ e nessun simbolo UNDEF secondo dumpbin. Sorgenti entro
400 righe e cartelle funzionali entro cinque file; nessun whitespace
finale o tab iniziale nei file del blocco.

## Congelamento della durata delle risorse — 31 ottobre

xt_freeze verifica le strutture inattive, congela il prestito della pagina
codice e porta l'owner da stato 3 a 4. Richiede BSP, IRQ disabilitati e
nessun lock posseduto. Il registro del prestito conserva un flag distinto
e irreversibile: xt_cancel, xt_build e il trasferimento nativo in scrittura
lo controllano, impedendo il cleanup anche se State viene riportato per
errore a 1 o 3. Un owner copiato non può congelare il prestito originale.
xt_verify richiede coerenza fra stato e registro; può leggere lo stato
congelato, ma non lo rende nuovamente reversibile.

Prima del congelamento, fallimenti di verifica lasciano disponibile il
rollback esistente. Dopo, anche una tabella corrotta conserva le risorse.
Sono sei pagine pinned già esistenti: codice, dati e quattro tabelle.
Il blocco non alloca nuova memoria e non cambia il layout dell'owner.
Non è protezione da scritture arbitrarie di codice ring 0: vale per i
percorsi di ownership verificati. Il registro resta esclusivo del BSP
di bootstrap; non si presenta IF=0 come sincronizzazione fra CPU.

Prove finali, revisione v2:

- Host transizione: kernel/build/ap-transition-host-4302ba0b801e4191a31867bac6386cbd/.
  Due casi Rust in debug/release; lifecycle C con 13 failure point I/O,
  sei rifiuti di verifica prima del freeze, owner copiato, BSP errato,
  stato alterato dopo freeze, risorse retained e nessun rilascio/ricostruzione.
  ASan sul C e cppcheck passati; CPU/IRQ/finestra sono modellati.
- Regressione riserva: kernel/build/ap-low-host-8bb26edfd9194b17b54199343a253430/,
  due casi Rust in debug/release e dieci ripetizioni C con allocatore reale
  del progetto su memoria simulata, ASan e cppcheck passati.
- Kernel UEFI/OVMF freeze: kernel/build/ap-transition-firmware-ad4dac22a3f64a97a2ace3751f8ca917/,
  10/10 boot consecutivi. Marker, HLT diagnostico, lease fisiche, budget,
  tabelle, stato 4, CR3 e PML4 operativa ispezionati dal monitor.
- Kernel ordinario: kernel/build/percpu-firmware-5b5514158a724d1cb66edb45184dcd0a/,
  10/10 boot consecutivi, alternando uno/quattro vCPU e RAM del profilo.
  Per-CPU BSP, lock, risorse AP predisposte e KERNEL_READY verificati.

kernel/build/k3i-freeze-manifest.json lega hash delle immagini e dei sorgenti
alle prove finali. Le campagne v1 precedenti (anch'esse 10/10) non sono
usate come prova della revisione v2. Nuova campagna riproducibile:
percpu-transition-freeze in scripts/test/campaigns.json; build con
-TestApTransition freeze e runner transition/firmware.py --freeze.

Nel debug sono stati corretti una dichiarazione BSP mancante e il modello
host che lasciava IRQ abilitati. Il secondo controllo del registro è stato
aggiunto dalla review prima della ricompilazione e dei boot v2.

Questo congela la durata di risorse ancora inattive: il codice del blob
non è stato installato e i dati sono ancora quelli zero del preparatore.
Non prova il sigillo del contenuto eseguibile, il commit congiunto con
arena/contesti AP o la pubblicazione dei dati per l'AP successivo. Restano
separati e obbligatori prima di INIT/SIPI. Nessun AP avviato, nessuna nuova
campagna video, prova su hardware fisico, Valgrind o coverage misurata.
Le sei pagine trattenute dopo freeze sono intenzionali, non un test di
rilascio completo; nessuna deduzione di assenza universale di leak.

## Blob rilocabile — 31 ottobre

startup/assembly/trampoline/entry.asm ora produce una pagina da 4 KiB:
432 byte logici, compresi header e GDT; resto zero. Il pacchetto C prepara
separatamente la pagina dati, con validazione del record e dei limiti in
Rust. Il template è input fidato della build, non un eseguibile arbitrario
autenticato da questa API. xb_verify confronta codice e dati byte per byte
con il contenuto atteso prima della pubblicazione; non è una user-copy
fallibile e il chiamante deve possedere buffer mappati e durata esclusiva.
Nessuna allocazione o scrittura nelle lease fisiche del kernel in questa tappa.

Layout software della pagina dati: record 0–103, fase a 104, errore a 108,
GDTR/puntatori far/IDTR tra 112 e 161; IVT a 192, IDT32 a 320, IDT64 a 576.
Metadati fino a 1088 escluso, stack iniziale dalla fine della pagina verso
il basso. Offset, selettori e layout sono verificati da builder, static
assert e prove CPU. Gli handler iniziali sono terminali: non sono ancora
il panic SMP completo del kernel. Il futuro BSP dovrà leggere errore/fase
e coordinare l'arresto, conservando la quarantena e le risorse.

Ingresso reale: stack separato, IVT, capacità e identità, pubblicazione
IVT pronta. Un vero NMI reclama la fase una sola volta e attraversa i modi
senza IRET. In protected mode verifica il PAT e abilita il profilo PAE,
SSE2 e NX richiesto. Non riscrive PAT o MTRR: un PAT diverso ferma il percorso.
Prima del salto alto installa l'IDT64 provvisoria. Restano IF=0 e blocco NMI;
RCX contiene il puntatore basso ai dati e RDX il token. Non è una CALL C.

Il CR3 operativo NON è caricato dal codice basso: ne perderebbe il mapping.
L'ingresso Assembly alto deve copiare i riferimenti necessari, passare a
stack/root/GDT/TSS/IST/GS privati, verificare il contesto CPU e predisporre
il ritorno IRETQ e il panic prima di sbloccare NMI o chiamare C.
Questa parte non è implementata dal blob. I dati mutabili di fase/stack
non vanno confrontati con l'immagine iniziale dopo l'esecuzione.

Host finale: kernel/build/ap-blob-host-d00c56fd566541c89ca707f327d4f20c/.
Tre casi Rust debug/release; C/Rust con ASan sul C, controlli di alias,
capacità, input invalidi e 8192 corruzioni di byte tra le due pagine.
Disassemblati separatamente i tre modi. Compilazione C freestanding di
prova senza GS/runtime: solo xs_entry_valid e xs_blob_layout restano da
risolvere, forniti dal Rust. Cppcheck senza diagnostiche bloccanti.
La prima stesura del test aveva I/O dentro assert: corretto dopo cppcheck.

CPU: kernel/build/ap-blob-cpu-312d6cad02c7493d908bb7b22ad9ff37/, 14/14 casi.
Dieci avvii consecutivi, alternando codice/dati 0x8000/0x9000 e 0xa000/0xb000,
su BSP SeaBIOS/TCG, non AP. Bytes installati ricontrollati, destinazione alta
eseguita con RCX/RDX attesi; codice e record rimangono invariati, NMI ancora
bloccato. CR0.WP ed EFER.NXE e le PTE reali sono osservati. Quattro negativi:
esecuzione della pagina NX e scrittura della pagina codice RO producono #PF
con CR2 atteso; PAT e ID errati fermano l'ingresso prima del salto alto.
Il test usa tabelle proprie e una destinazione Assembly diagnostica.

Le aggiunte finali di citazioni/assert ABI non hanno cambiato le cinque
immagini del pacchetto: hash confrontati con i byte della campagna CPU.
kernel/build/k3i-blob-manifest.json lega i sorgenti attuali alle evidenze.
Non sono prove UEFI, INIT/SIPI, CPU AP dopo reset, cache/MTRR degli AP,
mapping del kernel o gestione ordinaria delle eccezioni. KernelBoots=0,
ApStarted=0 e KernelInstalled=false. Nessuna prova su hardware fisico,
Valgrind o coverage misurata. La finestra prima della IVT resta soggetta
al vincolo della piattaforma e del profilo NMI già documentato.

## Prefisso alto privato — 1 novembre

Implementato il tratto Assembly che copia il record sullo stack alto,
mantiene IDT/GDT valide attraverso il cambio di selettore, carica la root
operativa, GDT/TSS private e GS, inizializza x87/MXCSR/XMM. La continuazione
resta Assembly: nessuna CALL C, promozione runtime AP o IRETQ. Contratto,
ownership del frame e prerequisiti in [k3i-private-entry.md](k3i-private-entry.md).

La GDT intermedia risolve la differenza fra selettore 0x18 del codice basso
e 0x18 della TSS privata. Gli handler provvisori sono terminali e richiedono
stack sano; non sostituiscono IST e panic SMP. Il frame non pubblica fasi
del coordinatore Rust e non autorizza PARKED o riuso della pagina bassa.

Campagna finale con stato SIMD inizialmente alterato: 10/10 positivi
consecutivi, alternando due posizioni del blob, e 3/3 negativi. Accesso
al vecchio record basso e lettura guardia producono #PF con CR2 atteso;
TSS invalida produce #GP. Ogni caso terminale verifica fase/errore e
assenza di triple fault. I positivi verificano CR3 operativo con metà
bassa vuota, record copiato, GDT/IDT alte, TSS busy, GS e stato x87/SSE,
compreso NMI successivo ancora trattenuto prima dell'ammissione finale.

Evidenza finale: kernel/build/ap-private-cpu-040ce4cc58f74511b066b310406d0013/,
con risultati, stato CPU, trace, comandi, oggetti, immagini e disassemblaggio.
kernel/build/k3i-private-manifest.json lega hash di sorgenti e immagini;
ricontrollati anche gli hash delle dipendenze del blob rispetto al relativo
manifest precedente. Le campagne precedenti del prefisso restano separate.

Compilazione NASM COFF, ABI C /W4 /WX, collegamento alto /NODEFAULTLIB /WX,
disassemblaggio e cppcheck senza diagnostiche bloccanti passati. Le sole
diagnostiche cppcheck sono informative, inclusi header standard non forniti.
Il COFF ha una sola dipendenza esterna prevista: xs_private_arrive.
Le prime prove hanno corretto la gestione dell'avviso NASM sulla normale
relocation esterna; il tentativo QEMU senza accesso loopback non ha raggiunto
il guest. Non sono conteggiati nella campagna positiva finale.

Il runner usa il BSP SeaBIOS e pagine preparate dall'host: KernelBoots=0,
ApStarted=0, ModuleReady=false. ASan/Valgrind e accounting fisico non misurati
per questo prefisso Assembly; il C aggiunto verifica soltanto il layout.
Nessun test su hardware fisico o coverage. Il completamento dell'ingresso,
il panic/IST definitivo e l'integrazione con le lease restano gate aperti.

## Installazione inattiva nelle lease — 1 novembre

Pagine codice e dati ora scritte dal pacchetto C/Rust nel diagnostico kernel,
con readback integrale, controllo del record rispetto alle lease e verifica
delle tabelle. `xt_verify` conserva il requisito dati zero; il verificatore
distinto `xt_verify_tables` non attesta il payload. Prestito esclusivo del
contenuto impedisce cleanup, build e freeze generico fino alla cancellazione
verificata delle due pagine, anche con State alterato. Nessuna pagina nuova
allocata dall'installatore; il suo owner statico è di 12424 byte.

Revisione v3: 10/10 boot UEFI installato inattivo, 10/10 cancellazione,
10/10 ordinari. Host: 26 failure point d'installazione, 22 di cancellazione,
8192 corruzioni dei byte, budget ripristinato nei percorsi reversibili,
ASan C e cppcheck senza errori bloccanti. Registro congelato verificato
separatamente con risorse intenzionalmente trattenute. Il tentativo ordinario
senza --ordinary è documentato come errore del comando, non cancellato.

[Report, fonti e prove finali](k3i-installation.md). Manifest:
kernel/build/k3i-installation-manifest.json. Il blob non viene eseguito,
il token della fixture non è un commit Rust e la destinazione è uno stub
diagnostico mai chiamato. L'ingresso alto privato non è ancora integrato.
Nessun AP avviato e nessuna garanzia di attivazione dedotta dal readback.

## Commit congiunto inattivo — 1 novembre

Scelta utente: arresto diagnostico dopo la barriera irreversibile. Registro
di retention indipendente dai flag mutabili, seal dell'immagine in sola
lettura e commit arena/contesti; errore successivo terminale e risorse
trattenute. Prima della barriera resta il rollback. SEALED è distinto da
XS_COMMITTED: il diagnostico non autorizza INIT/SIPI o una nuova generazione.

Prove finali: 53 scenari host, 48 failure point I/O, regressioni per-CPU,
risorse, identità, clock, installazione e transizione. QEMU: 10/10 sigilli
completi, due arresti intenzionali prima/dopo il seal privato e 10/10
ordinari. Nessun AP avviato. Dettagli, modelli e limiti nel
[report finale](k3i-joint-validation.md); manifest kernel/build/k3i-joint-manifest.json.

## Ritorno IRETQ e panic dopo il sigillo — 1 novembre

Piano C/Rust numerico e helper Assembly del ritorno con controlli di contesto
ACTIVE, GDT/TSS e gate/IST, prima di caricare la IDT finale e sbloccare NMI.
17/17 esperimenti CPU isolati: dieci con NMI pendente e sette controlli,
compreso IRET faulting con #GP e NMI consegnato su IST privato. Non è
ammissione nativa: l'harness prepara contesto e handler diagnostici.

Risolto il doppio commit nella preparazione del panic: il percorso riconosce
il gruppo già sigillato prima di inizializzare i record. Profilo joint-panic
10/10 UEFI, panic precedente 10/10 e ordinario 10/10. Restano promozione C
AP, provider del contesto/clock e collegamento xs_private_arrive al piano
e al protocollo. [Prove e limiti](k3i-return-validation.md); manifest
kernel/build/k3i-return-manifest.json. Nessun AP avviato.

## Gate aperti

### Trasporto INIT/SIPI verificato su host

startup/transport/wake.c implementa la sequenza a destinatario unico,
collegata al coordinatore Rust reale: richiede COMMITTED, pubblica ARMED
prima degli accessi e porta in QUARANTINED ogni fallimento operativo.
Non contiene un backend MMIO nativo e non è collegato alla build kernel.

Fonti verificate online: Intel SDM 253668-078US §9.4.4.1, Tabella 9-1
(sequenza e intervalli), §11.6.1/Figura 11-12 (ICR), §11.5.3 (ESR).
INIT 0x4500 e SIPI 0x4600|vettore hanno livello assertito, trigger edge,
destinazione fisica esplicita, nessuno shorthand. ICR +0x310 contiene
l'ID nei bit 31:24; +0x300 avvia l'invio e bit12 indica pending.
ESR +0x280 viene scritto a zero prima di leggere gli errori aggiornati.
Il vecchio INIT level-deassert non viene emesso: questo profilo è per
xAPIC moderno, non una compatibilità P6/Pentium o x2APIC implicita.

Attesa di almeno 10 ms dopo INIT; primo SIPI; se la IVT è già pronta,
conclude senza il secondo SIPI, altrimenti attende almeno 200 us prima
del secondo. La conferma finale è IVT pronta, non CPU online o PARKED.
Il valore uno deve essere pubblicato solo per il token corrente dal
futuro adattatore; valori diversi da zero/uno sono rifiutati.

L'intera chiamata usa un budget finito di controlli e una deadline;
gestisce wrap 24/32 bit, salti indietro, clock fallito/fermo, ICR pending,
ESR non nullo e fallimenti delle callback. Stato Rust e guardia Safe sono
ricontrollati prima degli accessi. Richieste invalide non fanno I/O.
L'output non può sovrapporsi al descrittore di trasporto. Nessuna allocazione
o API di rilascio è presente: il fallimento non autorizza a riusare risorse.

Safe deve verificare CPU BSP/identità destinazione, IF disabilitato,
assenza di lock posseduti, ownership del canale ICR, risorse già sigillate,
mapping UC e durata delle callback. Clock e callback devono essere stabili,
limitate e non modificare input/output del trasporto tramite alias. Ready
richiede lettura sincronizzata e verifica della generazione. Queste sono
obbligazioni dell'adattatore ancora da implementare, non garanzie fornite
da un numero APIC o da volatile. Nessun invio NMI è aggiunto in questa tappa.

Campagna finale: kernel/build/ap-wake-host-3337a3b774104bf0b32b133f0df4289c/,
41/41 processi host, MSVC /W4 /WX, ASan sul C e cppcheck passati, codice
runner 0. Casi: primo/secondo SIPI, wrap a 24 e 32 bit, ICR occupato, clock fermo/fallito/
indietro, guardia ritirata, errori ESR, conferma assente/errata e ritiro
concorrente dello stato. Failure injection in ognuna delle letture e
scritture dei tre possibili messaggi. Richiamare il trasporto dopo successo
o quarantena non ripete IPI. Il C è strumentato, il Rust no.

Read/Write/Ready/Safe e clock sono modelli host. Non dimostrano delivery,
timing reale, APIC ESR del device o traduzione AP. Nessun test QEMU del
trasporto, leak detector fisico o coverage misurata: ModuleReady=false,
NativeMmio=false, GuestBoots=0 nel manifest. La precedente prova CPU NMI
non convalida questo trasporto. L'integrazione con blob e risorse resta aperta.

Prima esecuzione del solo coordinatore: kernel/build/ap-startup-host-ecee5552b904415b96591ccfd2a6ead8/.
tools.log conserva comandi/output; result.json dichiara esplicitamente
RuntimeIntegrated=false, GuestBoots=0 e ModuleReady=false.

Installazione/readback nelle lease reali sono verificati nel solo diagnostico
inattivo descritto sopra. Restano integrazione del record e della fase NMI
con il BSP, collegamento del sigillo al protocollo e adattatore MMIO del trasporto, completamento
dell'ingresso alto con contesto privato, panic e prove reali AP.
La prima ricognizione non aveva trovato NASM nel PATH. Ora NASM portabile
3.01 è in kernel/build/tools/, con script assembler.ps1 per setup esplicito
e verifica degli hash prima dell'esecuzione. Nessun uso implicito di LLVM
da Unity e nessuna modifica al PATH globale. test-assembler.ps1 verifica
una fixture flat mista da 18 byte, riproducibilità e disassemblaggio dei
tre tratti. Evidenza: kernel/build/ap-assembler-5e05bbc296974dcfa08424b1a95c235c/.
È soltanto un test della toolchain: nessun CR3 o modo CPU viene cambiato.

Scelta NMI A approvata: prima prova CPU isolata passata con dieci avvii
consecutivi e tre controlli, fuori dal kernel e senza AP. Fonti e limiti
in [k3i-nmi-validation.md](k3i-nmi-validation.md). L'integrazione nel
trampolino rimane aperta, insieme al vincolo prima dell'IVT iniziale.

Nessun gate QEMU chiuso da questa tappa. Prima di dichiarare K3-I pronto
servono i dieci boot dell'immagine finale e i negativi definiti nel design.
I risultati storici K3-H non sostituiscono quelle prove.
