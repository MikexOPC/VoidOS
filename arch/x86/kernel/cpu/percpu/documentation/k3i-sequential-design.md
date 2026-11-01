# K3-I — avvio sequenziale del gruppo AP

Stato: contratto di riarmo approvato dall'utente. In implementazione;
il primo prerequisito è la verifica delle tabelle dopo l'ingresso AP.
Continua la scelta già acquisita: root condivisa e AP avviati uno alla
volta. Il boot ordinario rimane BSP durante il collaudo del gruppo.

## Punto di partenza

Il primo AP e il panic remoto BSP→AP sono verificati. `Startup::reserve`
accetta già soltanto l'indice successivo dopo PARKED e incrementa la
generazione. Il supervisore nativo invece usa ancora indice 1 e online=3.
`transfer_private` rifiuta tutte le scritture quando le risorse sono
trattenute/congelate. Non basta sostituire l'indice in un ciclo.

Il prefisso alto copia il record nello stack privato e passa a CR3,
GDT/TSS e GS definitivi prima dell'ammissione. Il riuso deve comunque
dimostrare che l'AP precedente non acceda più alla pagina dati condivisa:
nessuna deduzione dal solo numero di vCPU o da un'attesa temporale.

## Contratto proposto

- Un solo BSP supervisore, IF=0, nessun lock detenuto e finestra libera.
- Gruppo CPU pubblicato immutabile; identità e risorse di ogni nuovo AP
  derivano dal suo elemento, non dai getter PREPARED ormai chiusi.
- Solo PARKED del token precedente, provider accettato e maschera online
  attesa autorizzano la preparazione del prossimo indice.
- Prima di modificare memoria, validare il nuovo record e l'immagine
  attesa, compresa l'identità del codice invariato.
- Un canale dedicato consente esclusivamente la scrittura della pagina
  dati posseduta, con lease pinned e controlli prima/dopo. Non si riapre
  `Io.Transfer` in modo generale e non si abbassano gli stati SEALED.
- Codice basso, root e tabelle restano immutabili; nessuna nuova lease.
- Riscrivere e rileggere la pagina dati, azzerando Gate/errori e stack
  transitorio prima di INIT/SIPI/NMI verso il solo nuovo AP.
- Nessun AP online viene reinizializzato. I suoi record privati restano
  invariati; le maschere online crescono soltanto dopo ammissione.
- Errore o scrittura parziale: arresto diagnostico e tutte le risorse
  trattenute; niente retry, rollback o rilascio dopo il riarmo iniziato.

Pseudocodice non eseguibile: PARKED precedente → verifica lifetime e
record successivo → prenotazione nuova generazione → scrittura dati
limitata e readback → INIT/SIPI → IVT pronta → NMI → PRIVATE/PARKED.

## Alternativa

Preparare record distinti immutabili prima del sigillo ed estendere il
trampoline per selezionarli. Evita la riscrittura dei record ma cambia
il percorso Assembly iniziale e la disposizione dei dati/stack condivisi.
La proposta mantiene il trampoline esistente e limita la nuova autorità
alla pagina dati, nel solo intervallo tra AP verificati.

## Perimetro e accettazione

Modulo per-CPU/startup, con i soli adattatori a transizione, probe,
build e test necessari. Nessun avvio parallelo, hotplug, scheduler SMP
o cambiamento generale del paging.

Host C/Rust: sequenza, token stantii, indice saltato, AP duplicato,
riarmo prima di PARKED, panic già eletto, lease errata, tentativi di
scrittura a codice/tabelle e failure injection durante scrittura/readback.
ASan sul C e accounting esplicito, senza dichiarare leak detection fisica.

QEMU/OVMF: dieci boot del gruppo con quattro CPU realmente inizializzate,
registri e strutture private distinti, budget/pin invariati, controlli
del codice e delle tabelle, nessuna mutazione degli AP precedenti.
Negativi separati e regressione del diagnostico primo AP. Il panic del
gruppo completo e l'origine panic da AP restano prove successive.

Riferimenti interni: `startup/rust/protocol/state.rs`,
`startup/transport/native/`, `transition/native.c` e
`startup/assembly/private/entry.asm`. Le IPI e il percorso NMI riusano
le fonti e i vincoli del diagnostico già verificato; questa proposta
non introduce nuovi offset hardware.
