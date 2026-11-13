# Registro degli interventi futuri

Aggiornato il 24 ottobre 2026.

Questo file raccoglie limiti accettati temporaneamente e sviluppi rinviati.
Non certifica funzionalita implementate e non sostituisce le review dei
moduli. Per le prove gia eseguite vale [lo stato corrente](status.md).
Le voci iniziali derivano dalle decisioni e dai limiti documentati: questo
registro non e il risultato di un nuovo audit completo del repository.

Un crash, una corruzione o una violazione dell'ownership nel profilo
supportato richiedono un fix nel blocco corrente: non diventano accettabili
perche inseriti qui. Nessuna scadenza di calendario e ancora concordata.

## Come mantenere il registro

- Aggiungere una voce quando si accetta un limite o si rinvia un requisito.
- Specificare il profilo interessato, cosa lo rende necessario e le prove
  richieste per chiuderlo; collegare il progetto del modulo quando esiste.
- Prima di ampliare un profilo, controllare le voci che ne sono prerequisiti.
- Chiudere una voce solo con riferimento a implementazione e validazione;
  una scelta architetturale o un test mock non bastano.
- Le priorita sono relative al proprio prerequisito, non una promessa di
  implementare tutti i componenti in parallelo.

## FW-001 — Isolamento DMA tramite IOMMU

**Stato (24 ottobre 2026):** isolamento DMA verificato su QEMU con una unita
Intel VT-d. Blocco 3: DMA reali di `edu` permessi, vietati, non mappati e revocati
([prove](validation/consolidamento.md#validazione-dei-backend-x86)).
V1: GPU VirtIO sotto traduzione con ACCESS_PLATFORM. V2b: traduzione persistente per
tutta la sessione interattiva con `-VgpuIommu`
([sessione](../arch/x86/kernel/drivers/video/virtio/iommu/documentation/design.md)).
Restano aperti i punti elencati in FW-011 (RMRR, invalidazione per pagina, interrupt
di fault, hardware reale). Il testo sotto descrive l'avvio della voce.
**Priorita:** requisito prima di dichiarare isolamento da DMA ostile o
ampliare il profilo DMA a dispositivi non fidati/passthrough.

Il rinvio del19 ottobre e stato superato da questa priorita.
La [discovery Intel](../arch/x86/kernel/drivers/memory/iommu/intel/discovery/documentation/validation.md)
passa host e QEMU ma non abilita la traduzione. Console persistente sospesa;
nessuna estensione dell'eccezione alla sessione interattiva.
Per ora il lavoro VirtIO resta nel solo diagnostico QEMU fidato,
senza passthrough. L'eccezione al gate IOMMU è limitata a questo profilo.
La regola generale per dispositivi non fidati resta descritta nel [riferimento sulla sicurezza](review/instructions/security.md).
Un ID PCI corretto non dimostra che un dispositivo sia fidato.

Scelto Intel VT-d; discovery DMAR completata nel profilo documentato.
Il [mapping MMIO posseduto e la lettura capacita](../arch/x86/kernel/drivers/memory/iommu/intel/registers/documentation/validation.md)
sono verificati nel diagnostico, con revoca, rebind e budget ripristinato.
QEMU dichiara ECAP.C=0: la coerenza delle strutture di traduzione e un
prerequisito esplicito, non risolto dal mapping UC dei registri.
La [preparazione privata del dominio e delle tabelle](../arch/x86/kernel/drivers/memory/iommu/intel/domains/documentation/validation.md)
e verificata con IOVA espliciti, permessi e rollback; le strutture restano
scollegate dall'hardware. Questo non chiude il gate di isolamento DMA.
La [coerenza CPU e coda hardware di invalidazione](../arch/x86/kernel/drivers/memory/iommu/intel/invalidation/documentation/validation.md)
sono implementate nel diagnostico senza pubblicare la root o attivare TE.
Il protocollo QI e il cleanup non dimostrano ancora isolamento DMA.
Il nuovo [snapshot topologico](../arch/x86/kernel/drivers/bus/pci/topology/documentation/design.md)
percorre due volte tutti i bus MCFG, mantenendo i controlli completi per
ogni lettura ECAM. Nei profili Q35 misurati richiede oltre16000 letture
e decine di secondi di tempo host complessivo. E un percorso diagnostico,
escluso dall'immagine ordinaria: prima di integrarlo nel boot ordinario
misurare separatamente scansione e verifiche, definire un budget e ridurre
il costo senza introdurre autorizzazioni riutilizzate dopo cambi hardware.
La [selezione GPU-DRHD](../arch/x86/kernel/drivers/memory/iommu/intel/routing/documentation/validation.md)
e verificata (sessanta boot, nessuna scrittura ai registri VT-d).
L'[attivazione temporanea e i fault primari](../arch/x86/kernel/drivers/memory/iommu/intel/activation/documentation/validation.md)
sono verificati nel diagnostico (venti boot con TE, nessun DMA durante TE).
Restano: prove DMA consentito/vietato con `edu`, invalidazione di traduzioni usate e durata
dei mapping DMA. Intel VT-d e AMD-Vi richiedono backend distinti; non sono
entrambi implicitamente inclusi nella prima implementazione.

Nel frattempo separare indirizzi CPU, fisici e DMA nelle interfacce e
conservare ownership esplicita dei buffer. NX, guardie CPU, pinning e bounds
non impediscono a un dispositivo malevolo di accedere ad altra RAM.

**Chiusura:** test di DMA consentito e vietato, fault osservati, revoca e
invalidazione verificate prima del riuso della RAM, cleanup e accounting;
dieci boot consecutivi del profilo finale. Dichiarare separatamente quali
backend e hardware sono effettivamente provati.

Riferimento: [progetto code e DMA VirtIO](../kernel/drivers/video/virtio/queues/documentation/design.md).

## FW-002 — Completare il percorso video VirtIO

**Stato (24 ottobre 2026):** console ordinaria presentata dalla GPU VirtIO tradotta,
con ritorno al GOP a ogni errore (V2c,
[report](../arch/x86/kernel/drivers/video/virtio/iommu/documentation/design.md)).
Restano: recupero delle righe dopo il clip, cursore VirtIO, backing oltre
16 MiB; elenco completo in FW-011. Il testo sotto descrive lo stato precedente.
**Priorita:** percorso attuale verso video e cursore gestiti dal driver.

Code, buffer DMA e richieste/risposte sono verificati nel diagnostico:
[report](../kernel/drivers/video/virtio/queues/documentation/validation.md).
Una risorsa2D con backing contiguo alla risoluzione display e verificata:
[report risorse](../kernel/drivers/video/virtio/resources/documentation/validation.md).
Limite attuale16MiB, una superficie e allocazione fisica contigua; nessun
fallback di risoluzione silenzioso. Per risoluzioni piu grandi o RAM
frammentata, progettare backing scatter/gather e limiti di risorse prima
di estendere il profilo. Il formato32bit a4K supera il limite attuale.
Scanout e flush sono verificati con screenshot interi e aggiornamento
parziale: [report visivo](../kernel/drivers/video/virtio/resources/documentation/presentation-validation.md).
Da completare: sessione video ordinaria persistente,
aggiornamenti delle aree cambiate, cursore e integrazione input. Recovery,
timeout e teardown accompagnano ciascun blocco. Un passo validato non
certifica automaticamente quelli successivi.

**Chiusura:** output e movimento verificati, stress definito, assenza di
fault inattesi, cleanup/accounting e misure ripetibili. Nessuna promessa
di accelerazione3D, Vulkan o compatibilita con GPU fisiche.

## FW-003 — Prestazioni e presentazione video

**Stato (24 ottobre 2026):** misure V2d e profilo dei flush eseguiti
([latenze](validation/consolidamento.md#validazione-dei-backend-x86),
[profilo](validation/consolidamento.md#validazione-dei-backend-x86),
[verifica condizionata](validation/consolidamento.md#validazione-dei-backend-x86)).
Il flush V2c e sceso da circa 43.600 a circa 8.700 tick PM, ma costa ancora circa 3,4
volte il GOP; fluidita e atomicita restano non certificate.
**Quando:** durante l'integrazione VirtIO, prima di dichiarare fluida la GUI.

Misurare separatamente input, scheduling, composizione, trasferimento e
presentazione. Confrontare immagini e carichi definiti; distinguere scaling
della finestra QEMU e artefatti prodotti dal guest. Decidere buffering e
sincronizzazione in base alle capacita del backend, senza assumere che una
copia RAM renda atomico l'aggiornamento del display.

**Chiusura:** risultati riproducibili, latenza e artefatti riportati con
carico, risoluzione e ambiente; limiti residui dichiarati.

## FW-004 — Primo compositore e GUI

**Stato:** direzione discussa, progetto eseguibile non ancora approvato.
**Quando:** backend video e input sufficientemente consolidati.

Progettare superfici, ownership, finestre, focus, eventi, danni e ordine
di composizione. Idea utente: griglia logica non grafica, sfondo, dock e
widget, finestre, cursore. Il bridge verso i servizi kernel deve avere
contratti testabili; non equivale a isolamento dei processi.

**Chiusura:** design concordato e prima scena interattiva verificata.
Scelta del toolkit e runtime resta separata: Flutter, Qt, Slint e C# non
sono dipendenze gia disponibili o impegni approvati.

## FW-005 — Userspace e isolamento

**Stato:** non implementati nel profilo corrente.
**Quando:** prima di eseguire applicazioni non fidate.

Progettare spazi indirizzi, stack e transizioni ring3, syscall, user copy
con recupero dai fault e gestione del ciclo di vita. Riesaminare entry
eccezioni/IRQ, TSS/IST e attributi di memoria loader/kernel come gate;
questa voce non afferma che vi sia un nuovo bug gia riprodotto.
SMEP/SMAP e mitigazioni speculative richiedono prerequisiti CPU e threat
model; KPTI non viene dichiarato necessario o sufficiente per ogni CPU.

**Chiusura:** prove positive e negative dei confini, fault isolati e
assenza di accessi user/kernel non autorizzati nel profilo supportato.

## FW-006 — Scheduler, SMP e servizi permanenti

**Stato:** scheduler cooperativo BSP con task kernel fidate.
**Quando:** quando carichi e requisiti giustificano preemption o altri core.

Separare evoluzione del runtime dei servizi, preemption e avvio AP/SMP.
Definire contesto interrotto, sincronizzazione fra core, lock order e
durata degli oggetti. IF=0 non e un lock SMP. Non scegliere un algoritmo
complesso prima di misurare carichi e requisiti.

**Chiusura:** progresso e fairness misurati sul carico scelto, stress e
cleanup; SMP dimostrato da AP realmente avviati, non dal numero di vCPU.

## FW-007 — Driver e piattaforme aggiuntivi

**Stato:** candidati futuri, non nuova implementazione autorizzata.
**Quando:** richiesti da una funzionalita concreta.

Valutare storage/filesystem, rete, USB e input oltre PS/2; GPU fisiche e
rendering3D sono percorsi separati. Per ciascun device servono fonti della
revisione corretta, discovery, ownership, IRQ/DMA, recovery e test propri.
Pianificare hardware di prova separato prima di estendere le garanzie oltre
QEMU; il PC principale dell'utente non e una piattaforma di collaudo gia
autorizzata.

**Chiusura:** per dispositivo e piattaforma, con evidenze distinte.

Nota compatibilita del22 ottobre: uno smoke ECAM su QEMU i440fx ha
raggiunto KERNEL_ECAM_ABSENT, poi KERNEL_WATCHDOG_FAILURE=3 in CAPTURE e
panic LAPIC_TIMER_INVALID. Non e un PASS ne una causa timer isolata.
Evidenza: kernel/build/ecam-firmware-8396b1d8921a48b795be6bd922ddeab7/.
Il nuovo lettore viene collaudato sul profilo Q35; l'assenza MCFG end-to-end
su Q35 usa un filtro diagnostico dichiarato. Compatibilita completa con
i440fx e timer su tale piattaforma restano da diagnosticare separatamente.

## FW-008 — Portabilita build e automazione

**Stato:** toolchain Windows corrente; seconda toolchain rinviata.
**Quando:** collaborazione o riproducibilita richiedono altri ambienti.

Progettare una build Linux senza alterare silenziosamente ABI e formato
immagine. Valutare CI riproducibile, toolchain fissate e conservazione delle
evidenze. L'esistenza di harness locali non implica una pipeline CI attiva;
verificare lo stato effettivo prima di pianificarne l'estensione.

**Chiusura:** build e runtime verificati per ogni ambiente dichiarato,
con gli stessi gate funzionali pertinenti.

## FW-009 — Spezzare kernel/main.c

**Stato:** richiesto dall'utente il22 ottobre2026, non iniziato.
**Priorita:** prima di integrare la GUI, dopo il commit del blocco VT-d in corso.

`kernel/main.c` (303 righe, 61 direttive `#if/#elif/#else/#endif`) mescola
la sequenza di avvio di produzione con i probe dei profili diagnostici.
Obiettivo: leggibilita e scalabilita, non prestazioni (il codice gira una
volta). Prima un documento con sequenza, prerequisiti di ogni passo e
tabella dei profili; poi una cartella dedicata con una funzione per passo e
un solo `#ifdef` per profilo, senza cambiare ordine o condizioni.
Chiusura: mappe dei simboli equivalenti, audit di produzione, dieci boot per
ogni profilo toccato.

## FW-010 — Pulizia del repository e degli artefatti

**Stato:** richiesto dall'utente il22 ottobre2026, non iniziato.
**Priorita:** dopo il commit del lavoro in corso; prima dell'inventario
nessuna cancellazione.

Inventario prima di cancellare: file non tracciati, script e diagnostici
superati, documenti storici, artefatti in `kernel/build/`. Le evidenze citate
dai report di validazione non si eliminano senza archiviarle o aggiornare il
report. `reuse/` resta escluso. Pulizia degli artefatti con uno script che
conserva le ultime campagne citate; regole `.gitignore` per cache Python e
build. Chiusura: build e suite host invariate, link dei report verificati.

## FW-011 — Miglioramenti del percorso video e IOMMU (dopo V2)

**Stato:** elenco aperto il 24 ottobre 2026 dai limiti dichiarati nei report V1-V2d e H1-A.
**Priorita:** prima di dichiarare il video pronto per la GUI
o per hardware reale, nell'ordine che le misure e la GUI renderanno necessario.

Geometria e presentazione:
- **Geometria della GPU**: prima versione fatta (politica A, griglia 142x44 a 1280x800, clip a
  113x42 al fallback; [report](../arch/x86/kernel/drivers/video/virtio/iommu/documentation/design.md)).
  Restano: recupero delle righe perse nel clip, cambio di risoluzione a sessione avviata.
- **Timeout della present sotto carico dell'host**: fatto. Present asincrona, `SLOW` a 100 ms,
  fallback a 1 s, recupero dopo un timeout, 10/10 boot con host carico
  ([report](../arch/x86/kernel/drivers/video/virtio/iommu/documentation/design.md)).
  Restano l'harness host di `iommu/present/`, la taratura dei due limiti e il ritorno della griglia
  a 142x44 dopo un recupero.
- **Present con piu rettangoli**: oggi un solo inviluppo unisce celle e riquadri del puntatore
  lontani (area media circa 50.000 pixel per un gruppo da 1.296).
- **Costo residuo del flush V2c** (circa 3,4 volte il GOP): present, `prepare` e disegno nella
  superficie da analizzare con il profilo (`-ProfileVgpuFlush`).
- **Cursore VirtIO hardware** (`cursorq`) al posto del puntatore software.
- **Backing scatter/gather** oltre 16 MiB e cambio di risoluzione a sessione avviata.
- **Interrupt di completamento** al posto del polling, se le attese fenced diventano il collo di
  bottiglia (oggi circa 0,05-0,13 ms di media).
- **Fallback da errore reale**: oggi provato con un errore iniettato (`-TestVgpuPresentFault`).
- **Host test di `iommu/present/`** con una coda simulata (oggi coperto da test delle code/risorse e
  da QEMU).

IOMMU:
- **RMRR**: oggi la traduzione persistente viene rifiutata; servirebbe la mappatura in identita
  per il device proprietario. Il rifiuto e provato solo nel decoder host, non in QEMU.
- **Invalidazione per pagina** (page-selective, CAP.PSI) al posto di quella globale.
- **Interrupt di fault** (FEDATA/FEADDR) al posto del polling.
- **Sezioni IF=0**: l'avvio non e misurato (comincia prima del clock); l'arresto dura circa
  66-89 ms in TCG.
- **Hardware reale** e piu unita VT-d; interrupt remapping.

Misure e verifica:
- **Timer in TCG**: senza `icount` il timer PM segue il tempo dell'host, quindi le misure
  includono la velocita dell'emulazione. Valutare `-icount` per confronti assoluti.
- **H1-A**: periodo di 64 sessioni come ipotesi; misurare costo e tempo di scoperta.
- **Varianti di inventario**: una sola regola per tutti i moduli grazie al registro video;
  restano piu catene di tag (inventario, GPU, video) da unificare quando si aggiunge
  un'authority.

**Chiusura per voce:** implementazione, test host e prove QEMU dedicate, report aggiornato.

## FW-013 — Heap del kernel: crescita, lock e costo di verifica

**Stato:** aperto il 24 ottobre 2026 con G3 ([report](../kernel/mm/heap/README.md)). L'heap ha 16 MiB fissi
all'avvio.
- **Crescita a runtime:** richiede di rivedere la contabilità della console (`FreePages` costante,
  live group) insieme a G5.
- **Lock** per SMP.
- **Costo della verifica:** risolto con G5. La scansione completa è uscita dal flush, che ora costa
  7.544 tick PM contro 9.680, meno anche di prima dell'heap
  ([politica](mm/paging/verification-policy.md)). La crescita può usare il registro dei detentori
  (G5 V3). Resta il blocco di 15–25 ms dell'audit in inattività, da spezzare con un audit
  incrementale.
- **Primo utilizzatore:** le superfici delle finestre della GUI.

## FW-012 — Suite di test automatiche complete

**Aggiornamento (24 ottobre 2026):** `scripts/test/run.ps1` elenca le 31 suite host, rifiuta
ogni `test.ps1` non elencato (escluso esplicitamente solo il DMA `edu`, che usa QEMU), prosegue
dopo un errore e chiude con un riepilogo. La prima esecuzione completa ha trovato due mock
mancanti dopo la geometria A (corretti) e la verifica di formato su `read.c`, file non
committato dell'utente. Resta da valutare uno smoke QEMU per profilo.

**Stato:** aperto il 24 ottobre 2026. La suite host delle code VirtIO non linkava piu dal
commit V2a e nessuna suite automatica se n'e accorta: `scripts/test/run.ps1` non comprende le
suite IOMMU, VirtIO, arena DMA, latenza e paging.
**Priorita:** prima del prossimo blocco grande.

Aggiungere a `scripts/test/run.ps1` tutte le suite host per modulo (o una scoperta automatica
degli script `scripts/test.ps1`), con esecuzione completa anche dopo il primo errore e un
riepilogo finale. Valutare uno smoke QEMU per profilo.

**Chiusura:** un comando esegue tutte le suite host e fallisce su una qualsiasi.

## Voci chiuse

Nessuna in questo registro iniziale. I checkpoint storici restano nei
report dei rispettivi moduli e non vengono riclassificati come prove nuove.
