# test/tooling/entropy — stima della min-entropia

Stato: pronto, strumento host. Serve per decidere se il jitter RDTSC e i
tempi degli interrupt possono essere accreditati come entropia
([design della sicurezza](../../../../docs/design/sicurezza.md#entropia-e-canary)). Oggi non e usato dal kernel.

## Scopo

Calcola la min-entropia per campione di una sorgente con i 10 estimatori
non-IID di [NIST SP 800-90B](https://doi.org/10.6028/NIST.SP.800-90B)
(gennaio 2018), §6.3, e li combina come chiede §3.1.3:

- `H_original`: minimo degli estimatori applicabili ai campioni (§6.2;
  Collision, Markov e Compression solo per dati binari);
- `H_bitstring`: tutti i 10 estimatori sui campioni visti come bit, dal piu
  significativo, primi 1 000 000 bit;
- risultato: `min(H_original, n × H_bitstring)` per campioni da n bit.

Esegue anche i **test di riavvio** di §3.1.4 su una matrice di 1000
riavvii × 1000 campioni:

- controllo di sanita (§3.1.4.3): il valore piu frequente in una riga o in
  una colonna, `X_max`, non deve essere improbabile dato `H_I`: test
  binomiale `P(X ≥ X_max) ≥ 0,000005` con `p = 2^-H_I`;
- `H_r`, `H_c`: estimatori di §6.2 sul dataset per righe e per colonne;
- esito (§3.1.4.2): nessuna stima se il controllo fallisce o se
  `min(H_r, H_c) < H_I / 2`; altrimenti `min(H_r, H_c, H_I)`.

## Uso

```
assessment.exe <file> <bit per campione 1..8>
assessment.exe restart <file 1000x1000> <bit per campione 1..8> <H_I>
```

Il file contiene un byte per campione; i valori devono stare negli n bit
dichiarati. Campioni piu larghi vanno ridotti prima (§6.4). Con meno di
1 000 000 di campioni (§3.1.1) il programma avvisa. Per i riavvii il file
contiene la matrice per righe (riavvio 1, poi riavvio 2, ...) e `H_I` e la
stima iniziale. Il programma esce con errore se la validazione fallisce.
Da root, eseguire `./lib/security/scripts/test.ps1 -Module assessment` e
cercare `assessment.exe` nella cartella `kernel/build/security-host-*/`
indicata all'avvio della suite.

## Struttura

Rust std, in `rust/`: `entropy.rs` (programma), `track.rs` (combinazione
§3.1.3), `restart.rs` (riavvii §3.1.4). In `rust/estimators/`:
`statistics.rs` (limiti di confidenza, prestazioni dei predittori,
bisezione), `counting.rs` (MCV, t-Tuple, LRS), `suffix.rs` (suffix array e
LCP), `binary.rs` (Collision, Markov, Compression), `predictors.rs`
(MultiMCW, Lag, MultiMMC, LZ78Y).

Scelte di implementazione:

- Collision: Γ(3, z) ha forma chiusa, quindi F(q) = q + 2q² + 2q³ e
  esatta, senza la frazione continua dell'Appendice G.1.1.
- t-Tuple e LRS: un solo suffix array con LCP da i conteggi di tutte le
  lunghezze in O(L log L); il conteggio diretto con tabelle hash diventa
  O(L³) su dati ripetitivi.
- Predittori: l'alfabeto k e il numero di simboli diversi osservati.
- z del 99 % = 2,5758293035489 (quantile esatto, come lo strumento NIST);
  lo standard lo scrive arrotondato a 2,576.

## Test

`scripts/test/run.ps1 -Suite security` (modulo `assessment` del driver
`test/lib/security/runner.rs`, test in `rust/tests/`). 28 test in release, 27 in debug:

- gli **esempi numerici ufficiali** di tutti e 10 gli estimatori (§6.3.1 -
  §6.3.10) e i valori intermedi dei predittori;
- 7 valori della **Table 3** (Appendice G.2) per la prestazione locale;
- profilo delle tuple confrontato con il conteggio diretto su 300 sequenze;
- dati costanti (circa 0 bit), periodici (meno di 0,1), uniformi (oltre 5
  bit su 8), catena 00…0 di Markov, pareggi di MultiMCW, bit MSB-first;
- **confronto con lo strumento di riferimento NIST** (solo release, circa
  45 s): 7 dataset deterministici da 100 000 campioni (`rust/tests/datasets.rs`:
  uniformi a 1, 2 e 8 bit, sbilanciato, correlato, periodico con rumore, a
  deriva), 105 valori, tutti uguali entro 1,5e-6. I valori NIST sono in
  `vectors/nist_non_iid.tsv`, con commit e opzioni usati (`-i -t`: bitstring
  limitato a 1 000 000 di bit come qui). Lo strumento NIST e stato compilato
  una volta con MSYS2 fuori dal repo; libdivsufsort, non disponibile, e stata
  sostituita da un suffix array semplice che da lo stesso ordinamento.
- **riavvii**: trasposizione, coda binomiale contro somme esatte, conteggi
  di righe e colonne; esiti valido, sanita fallita (riavvii identici,
  entropia dichiarata troppo alta), regola H_I/2 con un caso fra H_I/4 e
  H_I/2.

Poi clippy `-D warnings` e build del binario.

Riavvii su matrici 1000 × 1000: dati uniformi validati (6,64 bit), riavvii
identici e entropia dichiarata troppo alta respinti dal controllo di sanita,
dati prevedibili respinti dalla regola H_I/2. `H_r` e `H_c` coincidono con
`ea_non_iid` di NIST su righe e colonne di due matrici (28 valori su 28).
`ea_restart` di NIST, compilato su Windows, si interrompe nella sua
simulazione e non e stato confrontato.

Prova su 1 000 000 di byte casuali (generatore di sistema): `H_original`
fra 7,35 e 7,95 bit, risultato 6,53 bit su 8 per via di `8 × H_bitstring`
(Compression 0,82); 28 s. Su 1 000 000 di bit casuali: 0,85 bit; 15 s.

Mutazioni manuali: 14 su 17 fanno fallire la suite, compreso il pareggio
fra contesti di LZ78Y (scoperto dal confronto NIST). Le altre tre sono
equivalenti (iterazioni della ricorrenza entro 1e-4, una condizione ridondante
in Lag, Kasai senza riuso del prefisso).

## Limiti noti

- Nell'esempio di §6.3.1 lo standard scrive −log2(0,6822): il valore
  corretto, coerente con il risultato 0,5363, e 0,6895.
- Mancano i test IID (§5): la valutazione usa sempre la traccia non-IID, che
  e la piu prudente.
- Controllo di sanita dei riavvii: si segue lo standard (test binomiale
  sul simbolo piu probabile, α = 0,000005). Lo strumento NIST usa invece una
  soglia simulata sul massimo fra tutti i simboli, un po' piu permissiva:
  questo strumento puo quindi respingere qualche caso al limite che NIST
  accetta, mai il contrario.
- I dati dei riavvii veri arriveranno con la sorgente nel kernel; per ora i
  test usano matrici sintetiche.
