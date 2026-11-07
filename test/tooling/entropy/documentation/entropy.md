# test/tooling/entropy — stima della min-entropia

Stato: pronto, strumento host. Serve per decidere se il jitter RDTSC e i
tempi degli interrupt possono essere accreditati come entropia
(`docs/design/security-entropy.md`). Oggi non e usato dal kernel.

## Scopo

Calcola la min-entropia per campione di una sorgente con i 10 estimatori
non-IID di [NIST SP 800-90B](https://doi.org/10.6028/NIST.SP.800-90B)
(gennaio 2018), §6.3, e li combina come chiede §3.1.3:

- `H_original`: minimo degli estimatori applicabili ai campioni (§6.2;
  Collision, Markov e Compression solo per dati binari);
- `H_bitstring`: tutti i 10 estimatori sui campioni visti come bit, dal piu
  significativo, primi 1 000 000 bit;
- risultato: `min(H_original, n × H_bitstring)` per campioni da n bit.

## Uso

```
assessment.exe <file> <bit per campione 1..8>
```

Il file contiene un byte per campione; i valori devono stare negli n bit
dichiarati. Campioni piu larghi vanno ridotti prima (§6.4). Con meno di
1 000 000 di campioni (§3.1.1) il programma avvisa. Il binario viene
costruito dalla suite in `kernel/build/security-host/assessment.exe`.

## Struttura

Rust std, in `rust/`: `statistics.rs` (limiti di confidenza, prestazioni
globali e locali dei predittori, bisezione), `counting.rs` (MCV, t-Tuple,
LRS), `suffix.rs` (suffix array e LCP per contare le tuple), `binary.rs`
(Collision, Markov, Compression), `predictors.rs` (MultiMCW, Lag, MultiMMC,
LZ78Y), `track.rs` (combinazione), `entropy.rs` (programma).

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
`test/lib/security/runner.rs`, test in `rust/tests/`). 20 test in release, 19 in debug:

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

Poi clippy `-D warnings` e build del binario.

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
- Mancano i test IID (§5) e i test di riavvio (§3.1.4): la valutazione usa
  sempre la traccia non-IID, che e la piu prudente.
