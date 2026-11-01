# K3-I — descrittore unico del gruppo CPU, 1 novembre 2026

Checkpoint del solo snapshot. Il successivo [blocco contesti](k3i-context-validation.md)
aggiorna provider e collegamento alto; le prove qui sotto restano riferite
alle proprie immagini precedenti.

Implementata la scelta A del [progetto di ammissione](k3i-admission-design.md):
un solo snapshot stabile, pubblicato dopo il sigillo e la preparazione panic.
Collegamento attivo nel solo diagnostico joint-panic. Non è ancora la
promozione del contesto AP: gli AP restano fermi e i provider rimangono
BSP-only. Nessun gate K3/K4 dichiarato chiuso.

## Contratto implementato

admission/group.h espone il descrittore di 5160 byte: header di 40 byte e
64 record di 80 byte. L'header contiene count, larghezza fisica, root e PAT;
ogni record contiene identità, area locale, indirizzo wait, copia del wait
e copia della sorgente PM. Non vengono copiati Depth, frame o altri
metadati mutabili dei lock. I riferimenti persistenti puntano alle aree e
ai descrittori statici reali, non alla bozza temporanea.

Il collector nativo è riservato al BSP con IF=0 e senza lock posseduti.
Richiede gruppo joint SEALED, payload basso ancora verificato, risorse
committed e registrate, contesti AP sigillati e panic pronto con solo BSP
online. Riconcilia identità, contesti, wait e root/PAT con le autorità
esistenti. Non richiama il controllo dell'arena tutta zero dopo aver
inizializzato i record panic.

Il validator Rust safe verifica ABI, count/indici, root, PAT, indirizzi
canonici/allineati, overflow, politica PM e assenza di alias interni.
Il C controlla inoltre duplicati e sovrapposizioni fra core e con input,
snapshot e parola atomica. La validazione numerica non è una prova di
mapping: la raccolta nativa usa solo oggetti noti del kernel e binding
già verificati. L'API di copia di basso livello richiede input mappato,
stabile ed esclusivamente posseduto dal chiamante fino al ritorno; non è
un'interfaccia per puntatori utente.

La parola di pubblicazione è distinta dal descrittore: 0 vuoto, 1 copia
in corso, 2 pubblicato. Claim tramite la primitiva atomica esistente;
il BSP è l'unico writer ammesso. Lo store finale è release, i lettori
usano acquire e non accedono al payload negli stati 0/1. Nessun reset,
riscrittura o rilascio dopo la pubblicazione. Gli errori di validazione
prima del claim non pubblicano nulla e consentono di correggere l'input.

L'immutabilità è del protocollo/API, non una nuova protezione PTE in sola
lettura. Le copie usano memoria statica: 5160 byte di snapshot, altrettanti
di bozza ripulita e pochi byte di controllo. Nessuna nuova allocazione o
lease e nessuna bozza da 5 KiB sullo stack. I controlli quadratici sono
limitati a 64 core e avvengono una volta durante la preparazione;
xag_group e xag_match hanno accesso costante.

xag_match confronta dati dichiarati dal chiamante con lo snapshot; non
legge CPUID/GS e non autentica il core in esecuzione. Nessun token viene
creato o consumato da questa API. Non modifica ACTIVE, PRIVATE/PARKED,
maschera panic online, scheduler o trasporto INIT/SIPI.

## Fonti e limiti del profilo

La sincronizzazione usa l'ABI Rust atomica già esistente, con il contratto
[Release/Acquire](https://doc.rust-lang.org/core/sync/atomic/enum.Ordering.html).
La copia volatile evita helper runtime; la sincronizzazione deriva dalla
pubblicazione atomica, non da volatile o IF=0.

Le verifiche numeriche conservano i requisiti già scelti: root a 4 KiB,
PAT[0]=WB e tipi non riservati secondo Intel SDM 253668-078US §§4.5 e
12.12; timer PM secondo ACPI 6.5 §4.8.3.3. L'esclusione delle porte COM1
e i limiti dei wait sono politica locale già presente, non requisiti
universali ACPI. Non sono introdotti nuovi accessi a registri o I/O.

## Evidenza finale

- Host: kernel/build/k3i-group-host-final/. Cinque casi Rust in debug/release:
  header/PAT, identità/puntatori, alias, policy wait, confini sorgente.
  Venti processi C/Rust, alternando 4 e 64 record; ciascuno usa quattro
  lettori Windows concorrenti con il writer, per 80 lettori complessivi.
  Rifiuti prima della pubblicazione, retry corretto, duplicati e alias
  fra core; dopo la pubblicazione, input modificato senza alterare la
  copia e secondo publish rifiutato. C compilato /W4 /WX con ASan.
- Analisi statica finale: kernel/build/k3i-group-static-final/, nessuna
  diagnostica bloccante su C del modulo e probe nativo. Messaggi
  informativi sugli include di sistema conservati. Audit degli oggetti
  senza helper di allocazione, memcpy/memset o __chkstk.
- UEFI finale: kernel/build/ap-joint-firmware-e41fd2e71238439181107f59ca0442e4/,
  10/10 boot consecutivi con kernel/build/k3i-group-joint-final/kernel.elf.
  Ogni boot richiede rifiuto prima del panic, gruppo nullo, risorsa non
  committed, root diversa, wait mancante e sorgente PM alterata; poi
  ripristino, pubblicazione unica e rifiuto del duplicato.
- Il monitor legge tutti i 5160 byte del descrittore finale: header/self,
  root/PAT/larghezza fisica, identità di ciascuno dei quattro core, indirizzi
  locali e wait, tutte le copie dei wait/sorgenti e slot inutilizzati zero.
  Ordine sigillo -> panic -> pubblicazione richiesto dai marker. Contesti
  AP ancora PREPARED, maschera panic solo BSP, ownership e budget verificati
  dal runner joint esistente. Non sono quattro CPU in esecuzione.
- Ordinario: kernel/build/percpu-firmware-7c30dd19d0264d7a99a74bdfeeab2f20/,
  10/10 con kernel/build/k3i-group-ordinary-v1/kernel.elf. Il suo kernel.map
  non contiene i simboli xag: il blocco resta fuori dal boot ordinario.

Manifest e audit: kernel/build/k3i-group-manifest.json e
kernel/build/k3i-group-source-audit.json. Le prove precedenti joint-v1,
pur passate, non sostituiscono quelle finali con tutti i nuovi negativi.
Il primo harness host non collegava tre stub di piattaforma: corretto
solo il test; la suite v2 finale passa tutte le venti invocazioni.

Ripetizione finale dopo la revisione dei separatori: host-final passa
nuovamente tutte le venti invocazioni. I wrapper PowerShell rimasti aperti
dopo PASS e JSON sono stati chiusi dopo la verifica degli artefatti; il
loro exit code d'interruzione non viene contato come PASS. I due runner
UEFI finali hanno terminato normalmente con exit code 0.

ASan strumenta il C host, non Rust, firmware o guest. I thread host provano
la pubblicazione concorrente, non AP del kernel né assenza universale di
race; TSan non eseguito. Nessun Valgrind, leak detector guest, coverage o
hardware fisico. Nessuna deduzione di zero leak fisici da ASan.

## Prossimo collegamento ancora necessario

Occorre consumare lo snapshot nell'ammissione C reale: identità hardware,
token ARMED/claim, GS e stack privati, CR3/PAT, GDT/TR/TSS e wait; stato
osservabile atomicamente, provider contesto/clock utilizzabile sull'AP,
IDT finale prima del panic admit, piano e IRETQ. Poi trasporto nativo,
conferme PRIVATE/PARKED e prove AP/panic remoto. La pubblicazione di questo
blocco non autorizza a saltare tali verifiche.
