# K3-I — commit congiunto inattivo verificato, 1 novembre 2026

Scelta utente: arresto diagnostico dopo errore irreversibile, con tutte le
risorse trattenute. [Progetto approvato](k3i-joint-commit-design.md).
Questo blocco sigilla il gruppo inattivo; non avvia AP e non completa K3.

## Comportamento implementato

`startup/commit/commit.c` esegue preflight dell'immagine e delle risorse
private prima di una barriera di retention irreversibile. Il registro
separato conserva identità del gruppo, immagine, transizione, arena e numero
di CPU: non deduce il lifetime dai flag mutabili dei singoli owner.
La preparazione è esclusiva del BSP, con IRQ disabilitati e nessun lock;
non è un protocollo di sincronizzazione generale fra CPU.

Dopo la barriera: seal del payload/prestito basso, commit arena/contesti e
verifica finale in lettura. `xb_seal` porta immagine e transizione in stato 4;
`xb_sealed` verifica binding, codice/dati completi e tabelle senza consentire
scritture. `xt_verify` conserva il requisito storico dati zero. Il vecchio
freeze inattivo resta distinto e rifiuta il contenuto posseduto dall'installer.

Soltanto il successo complessivo porta il gruppo a SEALED=2. Un errore dopo
la barriera pubblica FAILED_RETAINED=3 con motivo e chiama il terminale kernel:
nessun ritorno normale, rollback o retry. Prima della barriera un rifiuto
lascia il gruppo vuoto e permette il cleanup reversibile dell'installazione.

La barriera blocca cancellazione dell'immagine, restituzione del prestito,
scritture attraverso il backend nativo, unbind dell'arena e cancellazione
dei contesti AP. Il test nativo prova l'unbind anche azzerando temporaneamente
Committed: il registro indipendente mantiene il divieto. Le altre guardie
di ownership/freeze continuano a proteggere tabelle e pagina bassa.
Non è difesa da scritture arbitrarie ring 0 o rilascio deliberato tramite
API dell'allocatore che aggirino i contratti dei componenti.

Il gruppo C è di 72 byte. Nessuna nuova pagina o allocazione dinamica.
Gli adapter/callback e la loro capacità sono input kernel fidati, stabili
per tutta la chiamata; non sono un'interfaccia per puntatori utente.
Context e identità delle risorse devono coincidere. Il preflight nativo
controlla registry CPU, identità/area locale, root/PAT/capacità fisiche,
lease, descrittori e contesti predisposti. Il coordinatore non autentica
callback arbitrarie né dimostra il mapping di un indirizzo dal solo range.

## Gate di avvio intenzionalmente separato

SEALED non pubblica XS_COMMITTED e non autorizza INIT/SIPI. La destinazione
rimane lo stub diagnostico mai eseguito; non è il prefisso alto AP collegato
al runtime. Restano obbligatori panic/IST definitivo, ammissione per-CPU,
IRETQ e adattatore LAPIC prima di qualsiasi avvio reale.

Non esiste ancora un percorso per riscrivere la pagina dati sigillata fra
AP sequenziali. Il futuro riuso richiederà PARKED e verifica del lifetime;
un timeout o un gruppo fallito non consentono di riscriverla o liberarla.
Questi test non dimostrano protezione da NMI nella finestra prima della IVT,
coerenza cache dopo INIT o esecuzione SMP: valgono i limiti K3-I già descritti.

## Prove host finali

`kernel/build/k3i-joint-host-final-accounting/` e relativo `.log`: 53 processi
host riusciti, compreso un positivo, rifiuto del preflight, fallimenti prima
del seal privato, fra arena e contesti modellati, verifica finale e ciascuno
dei 48 trasferimenti del percorso positivo. In totale: un successo, 13
rifiuti reversibili e 39 terminali intenzionali con retention.

Inclusi alias del gruppo con immagine/template, capacità e CPU count errati,
identità Context/Resources discordi, owner copiato, corruzione fisica del
codice, retry e alterazione State dopo il seal. Il registro resta efficace.
Nei terminali il test intercetta il fail-stop con longjmp esclusivamente host:
il percorso kernel non torna. Callback arena/contesti e I/O sono modellati;
builder/validator C/Rust, allocatore e registri di prestito sono quelli reali.

Ogni rifiuto reversibile termina col budget originario di 259 pagine.
I casi irreversibili conservano intenzionalmente sei pagine pinned: budget
253, lease ancora valide e pool coerente. Non si afferma che il gruppo sia
rilasciabile dopo un terminale o che ASan misuri leak fisici guest.
MSVC /W4 /WX e ASan sul C; Rust non strumentato. Cppcheck senza errori,
warning, performance o portability bloccanti; rimangono style/information.

Regressioni host eseguite:

- `k3i-joint-percpu-host`: provider GS e terminali previsti, token e gating BSP.
- `k3i-joint-resources-host`: 64 geometrie, 63 sweep descrittori, 101 guasti,
  budget del modello a 64 CPU ripristinato.
- `k3i-joint-clock-regression` e `k3i-joint-identity-regression`: clock/wait,
  terminali previsti, identità sparse e pubblicazione.
- `k3i-joint-transition-regression`: due casi Rust debug/release, 13 guasti
  I/O, rollback e freeze inattivo.
- `k3i-joint-installation-regression`: 26 guasti installazione, 22 erase,
  8192 corruzioni fisiche e accounting reversibile.

Questi nomi indicano directory in `kernel/build/`, con log adiacenti.
Analisi statica nativa e probe: `kernel/build/k3i-joint-static-v2/`.

## QEMU UEFI/OVMF sulle immagini finali v2

- Positivo, 10/10 consecutivi: `kernel/build/ap-joint-firmware-632fd2ba764342b2bd325f3bce78ed5b/`.
  Immagine `kernel/build/k3i-joint-image-v2/kernel.elf`.
- Errore prima del seal privato, 1/1 terminale atteso:
  `kernel/build/ap-joint-firmware-58aa4b1a8ef14feda8bed11706a1112a/`.
  Immagine `kernel/build/k3i-joint-before-v2/kernel.elf`.
- Errore dopo arena e contesti sigillati, 1/1 terminale atteso:
  `kernel/build/ap-joint-firmware-b505e53b90c6435c894d4af2e3395260/`.
  Immagine `kernel/build/k3i-joint-after-v2/kernel.elf`.
- Ordinario, 10/10 consecutivi: `kernel/build/percpu-firmware-5c9ba72e408d4040a5b6d3b8b69bad88/`.
  Immagine `kernel/build/k3i-joint-ordinary-v2/kernel.elf`; alternanza fra
  uno/quattro vCPU e 128/256 MiB, nessun percorso di commit diagnostico attivo.

Il monitor controlla interi blob/record e tabelle transitorie, CR3 BSP,
lease pinned dell'arena, mapping/guardie, TSS inattive e contesti privati.
Nei negativi il gruppo è FAILED_RETAINED con motivo 2 e il BSP è realmente
in HLT col marker panic previsto; Committed dell'arena è rispettivamente
zero/uno. SEALED e KERNEL_READY non compaiono. Il caso intermedio arena
committed/contesti non sigillati è modellato su host, non iniettato in QEMU.

I 22 avvii sono 20 positivi e due fault intenzionali, non 22 boot ordinari.
Nessun AP del kernel avviato. Manifest di sorgenti/immagini/evidenze:
`kernel/build/k3i-joint-manifest.json`. Campagne riproducibili: percpu-joint,
percpu-joint-fail-before e percpu-joint-fail-after in scripts/test/campaigns.json.

## Debug, review e limiti

Prima compilazione host fermata da un include standard mancante nel test,
corretto senza abbassare /WX. Il primo runner QEMU usava una dimensione
owner sbagliata: corretto a 192 byte e aggiunti assert ABI C per offset e
dimensioni. Il kernel aveva raggiunto il marker SEALED: non era un suo crash.
Le prove finali sono distinte da questi tentativi e dalle immagini v1.

Nessuna nuova istruzione privilegiata/registro hardware in questo blocco;
si riusano i backend già verificati. Valgrind, coverage, hardware reale e
leak detector guest N/D. Dopo aver verificato report completi e VM chiuse,
eventuali wrapper host rimasti aperti possono essere interrotti: il loro
exit code non sostituisce risultati dei runner o conteggi di boot.
