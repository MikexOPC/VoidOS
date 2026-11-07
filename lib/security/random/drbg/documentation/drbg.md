# lib/security/random/drbg

Stato: pronto, solo test host. Non e ancora collegato al kernel ne a una
sorgente di entropia reale.

## Scopo

HMAC_DRBG con SHA-256 secondo
[NIST SP 800-90A Rev. 1](https://doi.org/10.6028/NIST.SP.800-90Ar1)
(giugno 2015), senza varianti. Trasforma entropia validata in numeri casuali
per canary, randomizzazione e chiavi. E l'ultimo anello della catena
`entropy` → SHA-256 → HMAC → DRBG.

Riferimenti: stato §10.1.2.1, Update §10.1.2.2, Instantiate §10.1.2.3,
Reseed §10.1.2.4, Generate §10.1.2.5, nonce §8.6.7, limiti nella Table 2.

## Interfaccia

Rust `no_std`: radice `rust/drbg.rs`, corpo `rust/engine.rs` (includibile
con `#[path]`). API Rust, niente export C per ora.

- `HmacDrbg::instantiate(entropy, nonce, personalization)`.
- `reseed(entropy, additional)`, `generate(output, additional)`.
- `with_reseed_interval(n)`: politica piu severa del limite standard.
- `reseed_counter()`, `input_length_allowed(len)`.

| Limite | Valore | Fonte |
|---|---|---|
| Forza di sicurezza | 256 bit | Table 2 (SHA-256) |
| Entropia minima | 32 byte | Table 2, = forza |
| Nonce minimo | 16 byte | §8.6.7, forza/2 |
| Ogni input | al massimo 2^32 byte (2^35 bit) | Table 2 |
| Una richiesta | al massimo 65 536 byte (2^19 bit) | Table 2 |
| Richieste tra due reseed | al massimo 2^48 | Table 2 |

Errori: `EntropyTooShort`, `NonceTooShort`, `InputTooLong`,
`RequestTooLong`, `InvalidReseedInterval`, `ReseedRequired`. Una richiesta
rifiutata non tocca ne l'output ne lo stato.

## Regole

- L'entropia deve venire da una sorgente che ha superato gli health test di
  `lib/security/entropy`: il DRBG non la valuta.
- Il nonce deve essere unico (§8.6.7), per esempio altra entropia.
- Nessuna prediction resistance: chi la vuole fa `reseed` prima di
  `generate`.
- Key e V (i valori segreti, §10.1.2.1) si azzerano alla distruzione; vale
  quanto detto in `memory.md` su copie e registri.

## Test

`scripts/test/run.ps1 -Suite security` (driver Rust
`test/lib/security/runner.rs`, test in `test/lib/security/random/drbg/rust/`).
Nove test in debug e release:

- **480 vettori ufficiali NIST CAVP** (CAVS 14.3, sezioni SHA-256 di
  `HMAC_DRBG.rsp`): 240 senza reseed e 240 con reseed, con e senza
  personalization e additional input. I file sono in
  `test/lib/security/random/drbg/vectors/`.
- entropia e nonce corti, limiti di input e richiesta, intervallo di reseed;
- uscite parziali come prefissi, ogni input cambia l'uscita;
- byte dell'oggetto tutti a zero dopo la distruzione.

Poi clippy `-D warnings` e build `no_std` con `panic=abort`. Quattordici
mutazioni manuali (separatori, Update nullo, V iniziale, additional input,
contatore e intervallo, limiti, ordine del seed, azzeramento): tutte fanno
fallire la suite. Il limite di 2^32 byte e provato sulla funzione di
controllo, non con un buffer reale da 4 GiB. QEMU non applicabile.

## Aperto

- Collegamento a una sorgente reale (`arch/x86/security/entropy`, dopo
  K4-C5) e primo uso: i canary del kernel.
