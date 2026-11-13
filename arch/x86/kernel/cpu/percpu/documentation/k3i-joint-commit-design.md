# K3-I — commit congiunto: progetto approvato, 1 novembre 2026

Scelta A acquisita: arresto diagnostico dopo errore irreversibile, con risorse
trattenute. Implementazione e prove in [validazione del checkpoint](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu).
Il [design K3-I approvato](k3i-design.md) richiede che tutte le risorse siano
sigillate prima di COMMITTED. Il [blocco installazione](k3i-installation.md)
ha verificato soltanto il contenuto inattivo e il rollback.

## Riscontro sulle interfacce all'inizio del blocco

- `xb_installed` verifica pacchetto, binding e readback; `xb_erase` richiede
  transizione in stato 3 e prestito esclusivo. Non esiste un seal del payload.
- `xt_freeze` rifiuta un contenuto installato: richiede dati zero e nessun
  prestito contenuto. Non va indebolito per fingere un commit congiunto.
- `x86_ap_resources_commit` rende permanenti arena e contesti AP, ma non
  considera blob e tabelle transitorie. Il suo seal per-CPU può arrestare
  il kernel dopo il commit dell'arena: non è una transazione reversibile.
- `x86_ap_unbind` usa Committed; il registro del prestito basso protegge
  indipendentemente il freeze. Serve una barriera comune anche per i
  percorsi di cancellazione che non hanno ancora il rispettivo flag finale.

## Contratto proposto

Un solo gruppo BSP di bootstrap, stessa identità owner e root/PAT/capacità
fisiche, tutte le risorse AP già predisposte. Nessuna nuova allocazione.
IRQ disabilitati, nessun lock posseduto, finestra fisica chiusa. Non è
sincronizzazione SMP né una transazione atomica hardware su tutti gli owner.

Prima fase reversibile: validare immagine, tabelle, lease pinned, registry
CPU, tutti i contesti e descrittori AP. Rifiutare owner copiati, alias,
token/identità discordi e commit già tentato. Un errore non cambia proprietà:
installazione cancellabile e budget invariato.

Seconda fase irreversibile: registrare il gruppo in una barriera di retention
prima di qualsiasi seal. Da quel momento tutti i cleanup del gruppo devono
rifiutare rilascio, ricostruzione o cancellazione, anche se un flag finale
locale è ancora zero o viene alterato. Non è difesa da scritture arbitrarie
ring 0: riguarda le API di ownership del bootstrap.

Poi rendere permanenti il prestito contenuto e quello basso, arena privata
e contesti; ricontrollare il payload in sola lettura e gli owner sigillati.
Un errore lascia il gruppo trattenuto, senza rollback parziale e senza AP
successivo. Soltanto il successo complessivo pubblica SEALED.

Schema concettuale, non codice:

    REVERSIBLE -> PREFLIGHT_OK -> RETAINED_SEALING -> SEALED
                                     errore -> FAILED_RETAINED

Le API di lettura distinguono installato reversibile e sigillato. Quelle
di scrittura continuano a rifiutare il freeze. `xt_verify` conserva i dati
zero come requisito storico; un controllo del payload sigillato è distinto.

Il diagnostico usa ancora una destinazione terminale mai lanciata. SEALED
non pubblica XS_COMMITTED e non autorizza INIT/SIPI: mancano ancora ingresso
runtime, panic/IST definitivo e ammissione AP. La futura pubblicazione Rust
richiederà anche quel gate; un sigillo di risorse non prova un entry completo.

## Scelta del comportamento dopo errore irreversibile

A, scelta dell'utente: fail-stop diagnostico con motivo e risorse trattenute. Nessun
ritorno ai chiamanti di bootstrap e nessun tentativo AP. È la base più
circoscritta per il profilo dedicato, coerente con gli attuali terminali.

B: ritorno al BSP con gruppo quarantinato e sistema ancora operativo.
Richiede progettare quali servizi possono continuare, propagazione dello
stato degradato e ulteriori verifiche dei chiamanti. Non permette comunque
di liberare risorse o riprovare l'avvio usando lo stesso gruppo.

La scelta richiesta da direttive-locali §7 è stata ottenuta prima del codice.

## Perimetro implementativo dopo la scelta

Un modulo funzionale `startup/commit/` per coordinamento e retention, con
soli adattatori necessari a installation, transition/content, lowmemory,
resources e cancellazione per-CPU; build e test centralizzati corrispondenti.
Nessuna modifica a scheduler, driver, paging generale o `reuse/`.
I profili diagnostici di commit congiunto non vanno mescolati con i commit
indipendenti TestApCommit/TestSmpPanic. L'immagine ordinaria non attiva il
nuovo percorso finché il diagnostico non passa i gate.

## Accettazione prevista, non risultati

- Host: errori in ogni preflight e seal, immagine corrotta, owner copiati,
  binding errati e tentativi ripetuti; prima della barriera rollback e budget
  completo, dopo la barriera nessun cleanup o retry e nessuna pubblicazione
  SEALED su errore. Guasto in ciascuna combinazione di seal parziale.
- ASan C/accounting e validator Rust reali dove coinvolti; distinguere I/O
  fisico simulato, callback native e prove guest. Coverage/Valgrind N/D.
- QEMU UEFI: dieci boot positivi con sigillo di tutte le risorse realmente
  ispezionato, negativi separati di seal parziale e regressione ordinaria.
  Nessun AP avviato e nessuna prova SMP dedotta dal numero di vCPU.
- Verifica finale di limiti sorgenti/cartelle, warning, cppcheck e hash delle
  immagini provate. Nessuna nuova istruzione privilegiata o registro hardware
  richiesto per il solo coordinamento del commit.
