# lib/security/bounds

Stato: pronto, solo test host. Non e ancora collegato al kernel.

## Scopo

Primitive comuni per evitare overflow e accessi fuori dai limiti: aritmetica
controllata, allineamento, range dentro un contenitore, indici di tabella e
disgiunzione di intervalli. Nessun accesso hardware.

## Interfaccia

Rust `no_std` in `rust/bounds.rs`; funzioni `extern "C"` con soli scalari,
senza puntatori. Le funzioni che calcolano un valore restituiscono
`OsBoundsValue { value: u64, valid: u32 }` (16 byte): `valid = 0` significa
errore e allora `value = 0`. Le altre restituiscono `1` (ok) o `0`.

| Funzione | Risultato valido se |
|---|---|
| `os_bounds_add`, `os_bounds_mul` | il risultato sta in `u64` |
| `os_bounds_align_up`, `os_bounds_align_down` | allineamento potenza di due, nessun overflow |
| `os_bounds_is_aligned` | allineamento potenza di due e valore multiplo |
| `os_bounds_range_end` | `base + length` sta in `u64` (fine esclusiva) |
| `os_bounds_range_within` | `[base, base+len)` dentro `[outer, outer+outer_len)` |
| `os_bounds_slice_within` | `offset + len <= container` |
| `os_bounds_index_valid` | `index < count` |
| `os_bounds_array_bytes` | `count * size <= container`, `size != 0` |
| `os_bounds_element_offset` | `index < count`, `size != 0`, l'elemento finisce in `u64` |
| `os_bounds_ranges_disjoint` | entrambi i range validi e senza byte in comune |

## Regole

- Range semiaperti `[base, base+len)`. La fine esclusiva deve stare in `u64`,
  quindi un range non puo includere l'ultimo byte `u64::MAX`.
- Lunghezza zero: valida se il punto sta dentro il contenitore, estremi
  inclusi; non si sovrappone mai a nulla.
- Fail-closed: un range che va in overflow rende falsa ogni verifica,
  compresa la disgiunzione.
- Controlli solo numerici: non dicono se un indirizzo e mappato o accessibile.
  Ogni modulo resta responsabile di ownership e permessi.

## Test

`scripts/test/run.ps1 -Suite security` (driver Rust
`test/lib/security/runner.rs`, test in `test/lib/security/bounds/rust/`). Esegue 14 test in debug e release con
overflow checks: oracolo a 128 bit su griglia di valori limite, casi di
policy, 200 000 input ostili generati, layout ABI. Poi clippy `-D warnings`
e build dell'oggetto `no_std` per `x86_64-pc-windows-msvc` con
`panic=abort`. Mutazioni manuali dei confronti (`<=` → `<`) fanno fallire
la suite. QEMU non applicabile finche il modulo non entra nell'immagine.

## Aperto

- Header C quando arriva il primo consumatore C.
- Collegamento a mm, driver e ACPI, che oggi hanno controlli propri.
