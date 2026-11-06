# lib/security/memory

Stato: pronto, solo test host. Usato da `lib/security/hash/sha256`; non
ancora collegato al kernel.

## Scopo

Due primitive per i segreti (chiavi, stato di hash e DRBG):

- **azzeramento** che il compilatore non puo eliminare;
- **confronto** di byte senza uscita anticipata, per tag e MAC.

## Interfaccia

Rust `no_std`: radice `rust/memory.rs`, funzioni in `rust/wipe.rs` e
`rust/compare.rs`. API Rust, niente export C per ora.

- `wipe(&mut valore)`: azzera interi (`u8`..`u128`, `usize`), array e slice
  con scritture `volatile`, seguite da una `compiler_fence`.
- `Wiping::new(valore)`: contenitore che da accesso al valore e lo azzera
  quando viene distrutto.
- `equal(a, b)`: `true` solo con lunghezze e contenuti uguali. Il tempo
  dipende dalla lunghezza, che e considerata pubblica, non dal contenuto.

Un altro modulo usa l'azzeramento includendo `wipe.rs` con `#[path]`, come
fa SHA-256. `wipe.rs` non ha attributi di crate proprio per questo.

## Limiti noti

- L'azzeramento copre solo la memoria del valore. Copie fatte prima, registri
  e stack di funzioni gia terminate non sono coperti: chi gestisce segreti
  deve evitare copie (SHA-256 ora lavora sul buffer senza copiarlo).
- Rust e LLVM non garantiscono formalmente il tempo costante. `equal` non ha
  uscite anticipate e usa `black_box` per scoraggiare ottimizzazioni con
  rami; e un "best effort", senza misure di tempo.

## Test

`scripts/test/run.ps1 -Suite security` (driver Rust
`test/lib/security/runner.rs`, test in `test/lib/security/memory/rust/`).

- 8 test in debug e release: azzeramento di ogni tipo e solo della slice
  data; `Wiping` azzerato alla distruzione, con controllo che senza
  contenitore i byte restano; `equal` su ogni bit cambiato, lunghezze
  diverse e 20 000 coppie generate.
- Sonda sull'assembly (`probe.rs`, `-C opt-level=2`): l'azzeramento
  `volatile` resta (8 store di zero), quello normale viene eliminato (0). Se
  il volatile manca la sonda fallisce.
- SHA-256 ha un test che legge i byte dell'oggetto dopo la distruzione:
  tutti zero.
- Mutazioni manuali: 9 su 10 fanno fallire la suite; l'azzeramento della
  schedule locale di SHA-256 non e osservabile da un test e resta coperto
  dal meccanismo verificato con la sonda.

Poi clippy `-D warnings` e build `no_std` con `panic=abort`. QEMU non
applicabile.
