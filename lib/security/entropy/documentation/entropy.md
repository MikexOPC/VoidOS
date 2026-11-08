# lib/security/entropy

Stato: health test pronti, solo test host. La sorgente CPU (RDRAND/RDSEED)
arrivera in `arch/x86/security/entropy/`; nulla e ancora collegato al kernel.

## Scopo

Verificare in continuo che una sorgente di entropia non sia guasta, prima di
usarne i campioni. Si usano i due health test approvati da
[NIST SP 800-90B](https://doi.org/10.6028/NIST.SP.800-90B) (gennaio 2018),
senza varianti proprie:

- **Repetition Count Test** (§4.4.1): guasto se lo stesso campione si ripete
  `C` volte di fila. `C = 1 + ceil(k / H)`, con falso allarme `α = 2^-k` e
  min-entropia `H` bit per campione.
- **Adaptive Proportion Test** (§4.4.2): in una finestra di `W` campioni
  (1024 per sorgenti binarie, 512 per le altre) guasto se il primo campione
  della finestra compare `C` volte.

## Interfaccia

Rust `no_std` in `rust/health.rs`, API Rust sicura, niente export C per ora.

- `HealthConfig`: `min_entropy_millibits` (H in millesimi di bit, 1..=64000;
  al massimo 1000 se binaria), `false_positive_exponent` (k, 20..=64),
  `binary`, `proportion_cutoff` (C dell'APT, 2..=W).
- `HealthMonitor::new(config)` rifiuta configurazioni invalide (`None`).
- `feed(sample)` restituisce lo stato: `Startup`, `Healthy`,
  `RepetitionFailure`, `ProportionFailure`, `InvalidSample` (valore diverso
  da 0/1 su sorgente binaria).
- `restart()` azzera tutto e rifa l'avvio: e il test on-demand (§4.3, punto 5).

## Regole

- Avvio: servono 1024 campioni consecutivi senza guasti (§4.3, punto 4).
  I campioni dell'avvio vanno scartati dal chiamante.
- Un campione è utilizzabile solo se lo stato prima di `feed` e quello
  restituito sono entrambi `Healthy`. Anche il 1024º campione va scartato.
- `Healthy` indica che i controlli non hanno rilevato un guasto. La
  min-entropia va stimata separatamente sulla sorgente: anche una sequenza
  binaria alternata e prevedibile può superare entrambi i controlli.
- Un guasto e persistente: lo stato non cambia finche non si chiama
  `restart()`, e resta il primo tipo di guasto rilevato.
- I test lavorano sui campioni grezzi, prima di ogni condizionamento (§4.3,
  punto 6).
- Il cutoff APT lo fornisce il chiamante: si calcola con
  `1 + CRITBINOM(W, 2^-H, 1-α)` (nota 10 di §4.4.2). Il calcolo resta nei
  test host perche richiede logaritmi non disponibili in `no_std`.
- La stima di H e il valore di α spettano al passo che sceglie la sorgente.

## Test

`scripts/test/run.ps1 -Suite security` (driver Rust
`test/lib/security/runner.rs`, test in `test/lib/security/entropy/rust/`).
Esegue 17 test in debug e release:

- formula RCT sugli esempi del testo e su tutti gli H;
- calcolo CRITBINOM confrontato con i 10 valori della Table 2;
- avvio a 1024 campioni esatti;
- sorgenti casuali sane, sorgenti bloccate, periodiche e sbilanciate;
- sequenza binaria prevedibile che supera entrambi i controlli;
- confini della finestra APT;
- guasto persistente, riavvio e configurazioni invalide.

Poi esegue clippy `-D warnings` e la build `no_std` con `panic=abort`.
Otto mutazioni manuali: sette fanno fallire la suite; l'ottava (azzerare il
conteggio APT nel riavvio) non cambia il comportamento.
QEMU non applicabile.

## Aperto

- Sorgente x86 con verifica CPUID e scelta di H e α.
- Condizionamento e DRBG standard (SP 800-90A), con vettori ufficiali.
- Riduzione dell'alfabeto per campioni a 64 bit (§6.4), se serve.
