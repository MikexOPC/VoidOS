# lib/security/mac/hmac

Stato: pronto, solo test host. Non e ancora collegato al kernel.

## Scopo

HMAC-SHA-256 standard secondo
[FIPS 198-1](https://doi.org/10.6028/NIST.FIPS.198-1) (luglio 2008), senza
varianti. Serve per autenticare dati e come base di HMAC_DRBG (SP 800-90A),
il prossimo anello della catena dei numeri casuali.

Riferimenti: definizione e passi 1-9 in §4 (chiave piu lunga del blocco
prima hashata, piu corta completata con zeri; `ipad = 0x36`,
`opad = 0x5c`); valori intermedi precalcolati da proteggere come la chiave
in §6.

## Interfaccia

Rust `no_std`: radice `rust/hmac.rs`, corpo `rust/engine.rs` (senza
attributi di crate, cosi altri moduli lo includono con `#[path]`). API Rust,
niente export C per ora.

- `mac(key, data)` calcola il tag di 32 byte in un colpo.
- `HmacSha256::new(key)`, `update(data)` (a pezzi), `finalize()`.
- `verify(tag)` accetta solo il tag completo di 32 byte, confrontato con
  `equal` di `lib/security/memory`. Il troncamento (§5) non e supportato.
- Gli errori sono quelli di SHA-256 (`MessageTooLong`).

## Protezione dei segreti

- Il blocco chiave `K0` e i pad sono in `Wiping`, azzerati appena usati.
- Lo stato precalcolato (due SHA-256) si azzera alla distruzione, come
  chiede §6. Anche il digest interno viene azzerato dopo l'uso.
- Valgono i limiti di `memory.md` su copie, registri e stack.

## Test

`scripts/test/run.ps1 -Suite security` (driver Rust
`test/lib/security/runner.rs`, test in `test/lib/security/mac/hmac/rust/`).
Otto test in debug e release:

- i 7 casi di [RFC 4231](https://www.rfc-editor.org/rfc/rfc4231) (il 5 sui
  primi 128 bit), anche confrontati con Windows CNG;
- ogni lunghezza di chiave da 1 a 200 byte e 1500 coppie casuali contro CNG;
- chiavi corte equivalenti al completamento con zeri, chiave lunga
  equivalente al suo hash;
- aggiornamenti a pezzi e cloni;
- `verify` rifiuta ogni bit cambiato, tag corti o lunghi, chiave diversa;
- byte dell'oggetto tutti a zero dopo la distruzione.

Poi clippy `-D warnings` e build `no_std` con `panic=abort`. Nove mutazioni
manuali (pad, soglia della chiave lunga, ordine dei pad, digest interno,
verifica, azzeramento, confronto): tutte fanno fallire la suite. QEMU non
applicabile.
