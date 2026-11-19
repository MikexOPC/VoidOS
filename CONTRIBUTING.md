# Contribuire a VoidOS

VoidOS è un sistema operativo sperimentale x86-64. I contributi seguono
la [roadmap](docs/roadmap.md) e le capacità dell'immagine Meson `ordinary`.
Lo [stato corrente](docs/status.md) distingue funzionalità provate,
profili diagnostici e limiti aperti.

## Segnalazioni e proposte

Per un bug, usa il template delle issue e indica commit, profilo,
ambiente, comandi e risultato osservato. Allega una riproduzione ridotta
e i log pertinenti, dopo aver rimosso dati personali e credenziali.
Per problemi di sicurezza segui la [policy dedicata](SECURITY.md).

Per una funzionalità ampia, apri prima una proposta con obiettivo,
ownership delle risorse, stati, errori e criteri di verifica. Mantieni
ogni contributo concentrato su un risultato consegnabile.

## Preparazione e struttura

Segui i prerequisiti e i comandi di compilazione e avvio nel
[README](README.md#compilazione-e-avvio). Usa `main` come base del tuo
branch e apri una pull request dal tuo fork o branch di lavoro.
La [mappa del codice](docs/architecture.md) indica i moduli e i relativi
documenti di design e validazione.

- C è il linguaggio principale, Rust affianca validazione e sicurezza,
  Assembly gestisce i percorsi che richiedono la CPU.
- Usa quattro spazi, nomi `snake_case`, costanti `UPPER_CASE` e il
  separatore di funzione già usato nel modulo. Evita spazi finali.
- Le interfacce comuni appartengono a `kernel/`. I sorgenti in `kernel/`
  e `lib/` accedono ai backend tramite queste interfacce.
- Documenta ownership, quiescenza e rilascio delle risorse. Valida
  lunghezze, intervalli, overflow e lifetime degli input esterni.
- Per registri, porte e MMIO, cita specifica ufficiale e sezione,
  verifica le capability e rispetta i bit riservati.
- I test stanno in `test/`, gli artefatti nelle directory `build/`.
  Usa Meson/Ninja e Cargo dove previsti. Mantieni le prove separate
  dal percorso di produzione e conserva le attribuzioni delle dipendenze.

## Verifica delle modifiche

Esegui le prove pertinenti al rischio della modifica. La
[guida ai test](test/README.md) e la [guida agli script](scripts/README.md)
descrivono suite host, campagne QEMU e directory delle evidenze.

| Modifica | Verifica attesa |
| --- | --- |
| Documentazione o template | Contenuti, link, sintassi dei template e `git diff --check`. |
| Logica, parser o strutture dati | Test host con casi positivi, confini ed errori. |
| Nuovo modulo o percorso di boot | Test host e tre boot dell'immagine di produzione. |
| SMP, interrupt, paging, DMA o privilegi | Test host, dieci boot e stress proporzionato al percorso. |
| Chiusura di milestone | Dieci boot e scenario completo della capacità consegnata. |

I test devono rilevare un difetto reale. Quando usi un mock, dichiara
quale componente sostituisce. Per QEMU riporta profilo, hash dell'immagine,
numero di vCPU, comandi, marker attesi, conteggi ed esiti. Indica
se hai eseguito prove su hardware fisico. Una VM silenziosa richiede
ulteriori verifiche prima di attribuirle un esito positivo.

## Pull request

Descrivi obiettivo, comportamento ottenuto, prove eseguite e limiti rimasti.
Collega l'issue pertinente e aggiorna i documenti del modulo quando cambia
un contratto. Aggiorna `docs/status.md` se cambia una capacità dell'immagine
ordinaria. Risolvi i fallimenti delle prove applicabili prima di presentare
la modifica come pronta.

Rileggi il diff completo ed esegui `git diff --check`. Mantieni i commit
focalizzati e gli artefatti di build nelle directory ignorate. Il codice
del progetto segue la [GPL-3.0-only](LICENSE), con le licenze dei componenti
terzi riportate nelle rispettive cartelle.

Le interazioni nel progetto seguono il [codice di condotta](CODE_OF_CONDUCT.md).
