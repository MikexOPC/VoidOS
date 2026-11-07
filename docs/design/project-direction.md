# VoidOS — Direzione del sistema e degli strumenti

Aggiornato il 5 novembre 2026.

## Scopo e stato

Costruire un sistema operativo con sottosistemi propri, coerenti con il
kernel ibrido e con le applicazioni che ospiterà. Le priorità sono sicurezza,
prestazioni misurate e tempi brevi per sviluppare e verificare le modifiche.

Questo documento raccoglie gli obiettivi concordati e le proposte della
discussione. Le sezioni successive descrivono lavoro futuro. Ogni nuovo
contratto richiede il design del modulo, la scelta prevista dalle regole
del progetto e prove prima di essere dichiarato operativo.

Per le funzionalità verificate consultare lo [stato del progetto](../status.md).
Per il lavoro corrente vale il [piano K4 e successivi](../handoff/k4-remaining.md).
I limiti accettati e gli interventi rinviati restano nel
[registro degli interventi futuri](../future-work.md).

## Sottosistemi progettati per VoidOS

La direzione concordata è sviluppare internamente le parti in cui il
controllo dell'interfaccia e delle politiche offre un vantaggio verificabile.
Prima di ogni scelta valutare implementazioni esistenti, standard,
licenze, costo di manutenzione e alternative più semplici.

| Ambito | Obiettivo da progettare |
|---|---|
| Processi e task | Ciclo di vita esplicito, isolamento, attese, cancellazione e raccolta delle risorse. |
| Scheduler | Politiche e carichi misurati, sincronizzazione corretta, progresso e limiti documentati. |
| Memoria | Ownership, handle validati, permessi, pinning e recupero coerente dopo gli errori. |
| IPC e servizi | Messaggi con ownership definita, capacità limitate, timeout, chiusura e gestione dei peer terminati. |
| Driver | Interfacce comuni e backend hardware distinti, con stop verificato prima del rilascio. |
| GUI | Compositore, superfici e input isolati dalle applicazioni, con budget di memoria e rendering. |

Condividere librerie quando esiste una responsabilità comune e verificata.
Mantenere le dipendenze hardware in arch/x86 e i servizi comuni nel kernel.
Per ciascun modulo definire stati, ownership, concorrenza, failure e cleanup.
L'originalità del sistema deve emergere dal comportamento e dalle interfacce.
Un'implementazione interna richiede benchmark prima di attribuirle vantaggi
di velocità, memoria o sicurezza.

## Compatibilità e filesystem

L'obiettivo è poter scambiare dati con Windows e Linux. Definire in un
blocco dedicato i formati, le versioni e le operazioni supportate, comprese
le differenze fra lettura, scrittura e recupero dopo un arresto.

È possibile progettare un'interfaccia filesystem interna a VoidOS e
collegarvi driver per formati condivisi. Prima serviranno storage, cache,
ownership dei buffer e una politica per errori e persistenza.
La compatibilità dei file su disco e l'esecuzione dei programmi Windows
o Linux sono obiettivi distinti. Il secondo richiede anche API, ABI,
formati eseguibili e servizi compatibili, da progettare separatamente.

## Sicurezza su misura e mediazione delle richieste

L'obiettivo concordato è controllare i permessi delle applicazioni e
richiedere conferma e autenticazione per alcune operazioni sensibili.
La proposta architetturale da approfondire comprende questi elementi.

- Il kernel verifica l'autorità del chiamante su ogni accesso protetto,
  incluse syscall, IPC, handle e memoria condivisa.
- Un servizio di policy decide concessione, durata, ambito e revoca dei
  permessi. Un componente fidato presenta le richieste all'utente.
- Il bridge espone richieste tipizzate e verificabili fra applicazioni e
  servizi. Rust è il candidato per gestire stati, parsing e ownership.
- Un handle identifica una risorsa e i diritti concessi. Generazioni e
  lifetime devono impedire il riuso di riferimenti scaduti.
- Durante una richiesta di conferma si sospende l'operazione interessata,
  con cancellazione e timeout definiti. Evitare attese sotto lock.
- La conferma autorizza una richiesta precisa, legata a identità e risorsa.
  Verificare nuovamente le condizioni al momento dell'esecuzione per
  gestire modifiche concorrenti e revoche.
- Password e conferma richiedono un percorso di input e visualizzazione
  fidato, protezione delle credenziali e limiti ai tentativi.

Prima dell'implementazione definire il modello di minaccia, la base fidata,
il comportamento se il servizio si arresta e la gestione delle deleghe.
Il kernel deve rifiutare accessi privi di autorizzazione anche quando
un'applicazione invoca direttamente l'interfaccia sottostante al bridge.
Rust riduce alcune classi di errori di memoria. Restano da verificare
policy, unsafe, FFI, race, DMA e transizioni di privilegio.

## Linguaggi e organizzazione

Scegliere il linguaggio per responsabilità, disponibilità degli strumenti
e costo totale di sviluppo e manutenzione.

| Linguaggio | Impiego previsto |
|---|---|
| C | Backend esistenti, driver, ABI e integrazione freestanding. |
| Rust | Stati, validatori, ownership, servizi sensibili e strumenti host condivisi. |
| C++ | Strumenti host e test complessi quando tipi e RAII semplificano il codice. Per sottosistemi kernel serve prima una politica su ABI, runtime, eccezioni, RTTI e allocazione. |
| Assembly | Ingresso CPU, interrupt, switch e operazioni privilegiate necessarie. |
| PowerShell e Python | Automazione esistente, da migrare per responsabilità e beneficio misurato. |

C resta nella cartella funzionale. Gli altri linguaggi seguono
linguaggio/funzione/. Test in test/, script in scripts/, artefatti in build/.
Le percentuali mostrate da GitHub descrivono la composizione del repository.
Gli obiettivi operativi sono minore duplicazione e verifiche più rapide.

## Build con Meson e Ninja

La direzione richiesta dall'utente è valutare Meson + Ninja per ridurre
l'orchestrazione scritta a mano. La precedente proposta CMake resta
un'alternativa da confrontare se il progetto pilota evidenzia ostacoli.
La scelta operativa finale dipende dalla prova sulla toolchain di VoidOS.

Meson configura target, dipendenze e test. Ninja esegue il grafo delle
compilazioni incrementali. Meson richiede Python sull'host di sviluppo.
Il risparmio atteso deriva dal riuso di queste funzioni e dalla riduzione
della logica personalizzata da mantenere.

La migrazione è prevista al confine K4/K5. Il piano esistente parte dalla
risoluzione dei profili in uno strumento Rust. Nel design della migrazione
stabilire quali decisioni appartengono a Meson e quali richiedono il tool,
così ogni profilo ha una sola definizione autorevole.

1. Misurare una suite host frequente e una build rappresentativa.
2. Provare Meson + Ninja su una suite C/Rust circoscritta, con risultati
   confrontabili con il percorso attuale.
3. Verificare MSVC, MASM, NASM, rustc, ABI e dipendenze generate. Separare
   gli eseguibili host dagli oggetti freestanding del kernel e del boot.
4. Migrare un profilo controllando sorgenti, flag, ordine di link,
   conversione PE/ELF, layout e validazione dell'immagine.
5. Verificare build pulita, incrementale e modifica di header, moduli Rust,
   font e asset. Un input modificato deve invalidare gli output pertinenti.
6. Eseguire i gate del profilo, poi sostituire il percorso corrispondente
   e rimuovere la logica duplicata. Ogni blocco validato riceve un commit.

Pilota host sull'editor di riga, Windows x64, MSVC 19.44.35229, rustc
1.98.1, Meson 1.12.1 e Ninja 1.13.2. Su tre esecuzioni, le mediane
di compilazione e test sono 3,33 s con PowerShell, 3,24 s con Meson a
freddo dopo la configurazione e 2,17 s senza modifiche. La prima
configurazione ha una mediana separata di 7,82 s. Verificato il rebuild
selettivo per header C, modulo Rust di test e modulo Rust di produzione.
Le misure riguardano solo questa suite. [Comandi del pilota](../../scripts/README.md#pilota-mesonninja-per-i-test-delleditor-di-riga).

Sul kernel ordinario, tre coppie alternate con output vuoti e la stessa
toolchain hanno dato una mediana di 27,50 s per la build standard e
34,48 s con `-UseRustPlan`. Le immagini coincidono salvo il timestamp di
debug del linker. Su nove prove separate, produrre il piano ha richiesto
315 ms in PowerShell e 23 ms con il planner Rust già compilato. Il
percorso sperimentale ricompila comunque il planner e tutti gli oggetti.
I tempi riguardano l'host Windows x64 a otto CPU logiche, PowerShell 7.6.9,
MSVC 14.44.35207, SDK 10.0.26100.0 e rustc 1.98.1.

## Test rapidi con evidenze affidabili

- Usare test host nel linguaggio del modulo per logica pura e casi limite.
  Riutilizzare il codice di produzione nelle prove quando possibile.
- Usare Rust per strumenti condivisi di parsing, verifica artefatti e
  gestione delle campagne quando la migrazione riduce il lavoro ripetuto.
- Usare C e C++ per ABI, layout, backend e fixture con vantaggio concreto.
- Conservare le asserzioni e i casi negativi quando si sostituisce un runner.
  Verificare che un fallimento intenzionale produca un esito negativo.
- Parallelizzare prove indipendenti con file, porte, ESP e VM separate.
  Limitare la concorrenza delle prove temporali per evitare interferenze.
- Durante lo sviluppo eseguire i test pertinenti. Prima del commit e alla
  chiusura applicare i gate del modulo e le regressioni richieste.
- Per QEMU mantenere i 3/5/10 boot consecutivi previsti dalle regole,
  immagine verificata, deadline, marker e chiusura della sola VM posseduta.
- Distinguere sanitizer host, leak host e accounting della memoria fisica.
  Riportare copertura N/D quando manca una misura.

Misurare build pulita e incrementale, durata delle suite, tempo fino al
primo errore, avvii QEMU e lavoro manuale necessario. Annotare hardware
host, versioni, profilo e ripetizioni. Il beneficio della migrazione va
confrontato anche con il suo costo di sviluppo e manutenzione.

## Sequenza di lavoro e criteri di accettazione

Seguire la sequenza del piano corrente K4, K5, K6 e dei relativi prerequisiti.
Ogni sottosistema nuovo avrà ricerca, design discusso, implementazione e
validazione del proprio modulo. Le proposte di questo file orientano quei
blocchi e richiedono decisioni specifiche prima di introdurre nuove API.

Un blocco deve avere un risultato osservabile, ownership e failure
documentate, test positivi e negativi pertinenti, misure quando promette
prestazioni migliori e diff finale verificato. Registrare limiti e prove
nel documento di validazione del modulo e aggiornare questo file quando
una direzione cambia o viene concretizzata.

## Fonti degli strumenti

- [Meson, guida iniziale e requisiti](https://mesonbuild.com/Quick-guide.html).
- [Meson, esecuzione e parallelismo dei test](https://mesonbuild.com/Unit-tests.html).
- [Ninja, grafo delle dipendenze e build incrementali](https://ninja-build.org/manual.html).

Verifica di questo blocco limitata a documentazione, link e diff.
Build, sanitizer e QEMU non applicabili a questa modifica documentale.
