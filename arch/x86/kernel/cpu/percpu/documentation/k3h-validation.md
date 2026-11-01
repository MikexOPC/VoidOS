# K3-H — root condivisa inattiva per gli AP

30 ottobre 2026. Scelta A approvata: una root e avvio sequenziale.
[Design, fonti e alternative](k3h-design.md). AP ancora fermi.

## Implementazione

Il bootstrap riserva una pagina dati sotto 1 MiB subito dopo la pagina
codice K3-G e prima delle allocazioni generiche. Ciascuna ha la propria
lease pinned. Una dipendenza esplicita a singolo borrower impedisce di
rilasciare il codice mentre il proprietario della transizione lo usa.
La guardia è di bootstrap BSP, non un nuovo registro concorrente SMP.

L'allocatore fisico espone allocate_within con minimo incluso e limite
escluso, entrambi allineati a pagina. Clippa le extent prima del first-fit
allineato, con split e accounting sotto il lock già esistente. Non alloca
fuori range per poi scartare la pagina. L'API precedente delega allo stesso
motore con il range fisico completo; test e stress del core sono ripetuti.

Dopo la preparazione dell'arena AP e dell'heap, il modulo alloca quattro
frame contigui sotto 4 GiB per PML4/PDPT/PD/PT. Le tabelle rimangono
inattive. Un solo ramo basso privato espone due foglie da 4 KiB:
codice supervisor RO/X e dati supervisor RW/NX; nessuna pagina zero,
pagina grande, foglia user o collegamento ricorsivo nella nuova root.
Le mappe kernel alte sono riferimenti condivisi, non copie delle figlie.

Rust valida geometria, limiti, bit delle entry e generazione dei valori;
C possiede risorse e trasferimenti. Gli accessi fisici usano la finestra
WB già presente, con lease e assenza di alias attivi verificate prima
delle scritture. Il CR3 operativo è letto e confrontato, mai sostituito.
L'inventario attivo viene aggiornato usando l'API paging di bootstrap,
senza passare dal controller della console video non ancora inizializzato.

Il costruttore salva la PML4 sorgente e non interpreta il suo collegamento
ricorsivo come un normale ramo RW: il profilo del kernel lo richiede RW/NX.
Il confronto della sorgente tollera soltanto i bit hardware A/D previsti.
Il nuovo slot ricorsivo resta assente. Nessuna protezione del BSP rimossa.

Snapshot e scratch appartengono all'owner statico, circa 8 KiB complessivi,
per evitare buffer da pagina sullo stack e dipendenze __chkstk. I nuovi
frame fisici sono cinque, 20 KiB, oltre ai 4 KiB codice K3-G. Non ci sono
parser, heap dinamico o runtime Rust aggiuntivi.

## Stati, rifiuti e cleanup

Stato 1: dati riservati, root non utilizzabile; 2: indisponibile senza
dati; 3: tabelle preparate inattive. prepare restituisce 1 soltanto dopo
verifica completa, 2 per mancanza di frame per le tabelle, 0 per errore.
La mancanza di RAM consente al BSP ordinario di continuare con WARN;
un diagnostico che richiede la root non dichiara successo in quel caso.

Un fallimento delle tabelle conserva la precedente pagina dati e il
borrow sul codice, restituendo solo i frame del tentativo. Fallimento
del rollback o finestra non più sicura: fail-stop con risorse conservate.
La cancellazione completa restituisce prima le tabelle, poi i dati e
infine ritira la dipendenza dal codice; K3-G conserva la propria lease.

Il cleanup rifiuta copie dell'owner, lease alterate, identificatori zero
con campi residui e stati incoerenti prima di sommare i conteggi. Se le
tabelle sono state rilasciate ma il rilascio dati è negato, l'owner torna
allo stato 1 mantenendo pin e dipendenza: il tentativo può essere ripreso
senza dichiarare una pulizia già completata. Non si azzerano lease vive.

La preparazione dati è precoce e one-shot: dopo una cancellazione completa
non si ricrea tardivamente durante le allocazioni generiche. Il retry
delle sole tabelle è consentito conservando i dati iniziali. Nessun codice
per INIT/SIPI, caricamento della nuova root o avvio AP è presente.

## Revisione finale v5: preparazione inattiva verificata

L'estensione paging/memoria autorizzata è completata. Percorso lineare per
mappe ordinate con fallback originale, ricerca binaria degli alias su copia
locale e confronti fra intervalli già validati nell'allocatore. Tutti i
controlli di ownership, permessi, bounds e timeout sono conservati; nessuna
cache di autorizzazioni tra scansioni. [Analisi](../../../../mm/paging/documentation/k3h-performance.md).

Immagini in kernel/build/k3h-final-{prepare,rollback,normal,console,legacy}-v5/.
Hash sorgenti, immagini ed evidenze in kernel/build/k3h-source-manifest.json.

| Verifica finale | Esito | Evidenza in kernel/build/ |
| --- | --- | --- |
| Preparazione root inattiva | 10/10 consecutivi | ap-transition-firmware-05d603b7816b4220961a1dd51f4a8c06/results.json |
| Rollback e retry | 1/1 | ap-transition-firmware-4c5641e76c0a48d68efb1d610dd594f6/results.json |
| Boot ordinari | 10/10 | percpu-firmware-be6d1b5beda9446d84d1e28993ef7233/results.json |
| Console VirtIO/VT-d, quattro vCPU configurate | 10/10 consecutivi; pixel, movimento, input e cleanup | pointer-interactive-check-0296a0ad972744f88b53e4aefe355a5f/results.json |
| Host transizione C/Rust, ASan | PASS, inclusi 13 punti di errore I/O | k3h-transition-host-v5/ |
| Inventario paging host | 10 gruppi ripetuti 100 volte; Rust debug/release e Clippy PASS | k3h-paging-perf-host-v5/ |
| Differenziali ASan | 3.063 memoria, 7.903 metadati fisici, 8.512 alias | k3h-paging-review-v5/ |
| Allocatore, ASan e contesa host | 2/4/8 thread, 2.000/4.000/8.000 cicli; budget e pin ripristinati | k3h-physical-host-v5/ |
| Bootstrap/kernel host | PASS | bootstrap-host-a846797862a74bd6a638ecd3712027ca/ e k3h-kernel-host-v5.log |
| Legacy bootstrap diagnostico | build PASS | k3h-final-legacy-v5/kernel.elf |
| Campagna centrale percpu-transition -Smoke | 1/1 | qemu-run-dbd2702d77b748049e3e0d6e851aae01/results.json |
| Cppcheck finale dei sorgenti interessati | zero error/warning/performance/portability; 29 information e 2 style | k3h-paging-review-v5/cppcheck.xml |

Le due osservazioni style residue riguardano l'ambito di variabili esistenti;
le due segnalazioni di shadow introdotte dal modello di test sono state
rimosse rinominando soltanto variabili del test, poi ripetendo differenziali
e analisi statica. Nessuna immagine kernel è cambiata durante la campagna.

Il limite di 600 s è invariato. Le osservazioni host VT-d→puntatore variano
circa 201–511 s; non sono un benchmark controllato. Il polling diagnostico
è stato alleggerito dopo il quarto boot; questa differenza, il carico host
e TCG impediscono di attribuire le variazioni a un guadagno percentuale del
codice. Lo startup resta lento e richiede futuro profiling mirato.
L'osservatore non modifica guest, test, soglie o risultati del runner.

Il gate di integrazione K3-H è chiuso per preparazione/rollback della root
inattiva. Questo non abilita il suo utilizzo: prima dell'avvio servono
rivalidazione dei riferimenti, commit, trampolino e ingresso AP.
Un solo core OS esegue codice; il gate SMP K3-F rimane aperto.

## Snapshot v3: prove precedenti e regressione riprodotta

Le prove della tabella seguente appartengono allo snapshot v3, precedente
alla correzione prestazioni completata nella v5. Queste prove non certificavano
la correzione successiva: la console v3 superava la deadline.
Manifesto locale kernel/build/k3h-source-manifest.json, aggiornato con
hash dei sorgenti, immagini effettive e percorsi delle evidenze.

| Verifica | Esito | Evidenza in kernel/build/ |
| --- | --- | --- |
| Preparazione finale | 10/10 consecutivi | ap-transition-firmware-1ff414fc308743f5b211d62bbeba5542/results.json |
| Rollback finale e retry | 1/1 | ap-transition-firmware-68154817706c4f5494eba3c96f60b7d4/results.json |
| Boot ordinari finali | 10/10, una/quattro vCPU | percpu-firmware-f3ffaae930964505ac71e99fea3d80d6/results.json |
| Console finale isolata | FAIL: checkpoint non raggiunto entro 600 s, fase VT-d | pointer-interactive-check-b4068248bbca431b82cce4f109a76183/results.json |
| Host C/Rust finale | PASS | ap-transition-host-c9243349fad943d495fda73e963e58cf/ |
| Low-memory host | PASS | ap-low-host-1c59c51ea40d463f8a5ddb78a8746a51/ |
| Allocatore host, ASan e 2/4/8 thread | PASS, 2.000/4.000/8.000 cicli | physical-host-6b874efe75224d38a14dd3479e8ce271/ |
| Bootstrap host | PASS | bootstrap-host-934fafe68f544833a7e44c6178b973e4/ |
| Kernel host | PASS | k3h-kernel-direct.log |
| Analisi statica finale nativa | zero error/warning/performance/portability; 12 information e 4 style | k3h-native-static-v3/cppcheck.xml |

Test Rust centralizzati: due casi in debug e release, quattro esecuzioni.
Il C usa allocatore e lock reali con contesto CPU e trasferimenti fisici
modellati. Copre tredici fallimenti I/O, allocazione e pin in entrambe le
fasi, rilascio negato, retry dopo rilascio parziale, guardia sulla pagina
codice, alias, input fuori limite, lease malformate e indisponibilità reale
nelle mappe host. ASan sul C; nessun blocco unsafe Rust nella nuova logica.
Il wrapper host dell'allocatore era rimasto aperto dopo il completamento
dei casi: la sua catena posseduta è stata interrotta e le regressioni sono
state rieseguite direttamente nello stesso host, senza mascherare il risultato.

Le build prepare, rollback, ordinaria, console e diagnostico storico
-TestBootstrapMemory compilano con warning come errori. Nei diagnostici
storici la preparazione transizione è esplicitamente saltata: non sono
profili ammessi al futuro avvio AP. Nessuna fonte eseguibile modificata
dopo l'inizio della campagna finale. Max sorgente/script modificato 379
righe, massimo cinque file diretti nelle nuove cartelle funzionali;
whitespace, CR isolati e sintassi dei runner verificati. Il registro JSON
delle campagne è configurazione dati, non un sorgente C.

Il monitor verifica fisicamente tutte le entry delle quattro tabelle,
le due foglie, il contenuto iniziale dei dati e le lease reali Live/Pins.
Confronta CR3/HLT/RAX del BSP, la PML4 originale e lo snapshot salvato.
Il budget effettivo è quello precedente meno quattro frame; la sorgente
UEFI della pagina codice è verificata dal controllo low-memory riusato.
Non si considera sufficiente il marker seriale e non si deduce SMP.

## Problemi trovati durante implementazione e review

1. Buffer da pagina sullo stack e azzeramento aggregato dell'owner grande
   introducevano __chkstk: il linker freestanding ha rifiutato la build.
   Risolto con scratch posseduto dall'owner e azzeramento volatile esplicito,
   senza disabilitare le protezioni del compilatore o aggiungere un falso stub.
2. Un return dopo il terminale diagnostico noreturn produceva C4702 con
   /WX: separati i rami di compilazione, warning non soppresso.
3. Il primo QEMU è andato in panic prima del checkpoint. La diagnosi ha
   identificato zero trasferimenti e rifiuto dell'inventario: era usato
   il wrapper del video prima della sua inizializzazione. Adottata l'API
   x86_paging_inventory_capture_lapic già usata in questo stadio del boot.
4. Review del contratto esistente: il collegamento ricorsivo operativo è
   RW/NX, non RW. Corretto anche il modello host che riproduceva la stessa
   assunzione sbagliata. Non si è modificata la PTE operativa per farla passare.
5. Cleanup con lease malformate: mancavano controlli sui campi residui di
   una lease con Id=0 e sulla coerenza fra stato e pin. Aggiunto il caso
   regressivo con conteggi che avrebbero fatto wrap della somma; nessuna
   risorsa viene ora rilasciata o dimenticata su quel rifiuto.
6. Rilascio parziale: dopo avere restituito le tabelle, un rifiuto sui dati
   deve lasciare uno stato ritentabile. Test dedicato del secondo rilascio
   negato, conservazione del pin/borrow e successiva cancellazione riuscita.

Due campagne esplorative fallite restano in
kernel/build/ap-transition-firmware-87dd8dc3d89a4e06ab4de9309d38f505 e
kernel/build/ap-transition-firmware-c03c767045064c2a99a867833af33267.
La prima aspettava il checkpoint fino alla deadline; il runner ora rileva
subito FAIL e salva registri/PML4 prima di chiudere soltanto la VM posseduta.
Una campagna video intermedia è scaduta a 600 secondi nella fase VT-d:
pointer-interactive-check-6daec293f5fe45bb921b375d43c5c2e8. Nessun PASS
dedotto dal silenzio, nessun timeout aumentato. La ripetizione finale è
isolata da compilazioni e stress host.

## Origine del blocco nella revisione v3

Anche la console v3, avviata da sola dopo tutte le compilazioni, non ha
raggiunto KERNEL_POINTER_READY nei 600 secondi previsti. Non è quindi
sufficiente attribuire la scadenza intermedia al carico delle build.
Nessuna soglia modificata e nessun esito negativo riclassificato PASS.

Campionamento diagnostico separato, senza modifiche al kernel:
transition-boot-profile-a8904285b0bd43ddb4421a6ff5af0d24/samples.json.
Su 120 campioni RIP durante KERNEL_VGPU_IOMMU_BEGIN, 34 ricadono in scan
(arch-x86-mm-paging-inventory.obj), 22 in kernel_regions_overlap, sette
in kernel_inventory_memory e sette in kphys_validate. Non è un benchmark
completo o una misura percentuale esatta del tempo; localizza lavoro di
verifica memoria/paging durante il bring-up. Il campionamento non ha
aspettato il completamento della console e non è contato come test PASS.

L'utente ha autorizzato l'estensione a inventario paging e controlli di
memoria. Correzione equivalente senza cache di autorizzazioni completata:
[analisi e test differenziali](../../../../mm/paging/documentation/k3h-performance.md).
I gate rimasti aperti nella v3 sono ora ricollaudati nella v5, come
riportato sopra. Le scadenze fallite precedenti restano conservate.

## Limiti e seguito

La root è uno snapshot di preparazione, non un'autorizzazione permanente:
heap/video e altri mapping successivi possono invalidarne il confronto
con la sorgente. Prima del futuro uso occorrono nuova verifica/ricostruzione
nel punto di avvio e stabilità dei riferimenti condivisi. Le tabelle figlie
restano di proprietà dei rispettivi moduli e devono sopravvivere al passaggio.

Prima di qualsiasi INIT/SIPI servirà il commit irreversibile congiunto di
codice, dati, tabelle e risorse AP. La cancellazione di questo blocco vale
solo per strutture mai attivate; un timeout AP non autorizzerà rilascio o
riuso dei dati iniziali per un altro core. Non è ancora esposto quel commit.

Mancano blob 16/32/64 bit, GDT/IDT/NMI iniziali, passaggio al CR3 operativo,
contesto privato e avvio AP. Il codice SIPI non viene scritto né eseguito.
L'ispezione di entry inattive non dimostra traduzione o enforcement su un AP.
Quattro vCPU configurate restano un solo core OS attivo. K3-F è ancora aperto.
Hardware fisico e Valgrind non eseguiti; coverage linee/branch N/D. ASan e
accounting host non provano l'assenza di ogni leak RAM fisica del sistema.
Nessuna modifica a reuse/, commit o push.
