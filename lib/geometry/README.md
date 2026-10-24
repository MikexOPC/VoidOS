# lib/geometry e lib/containers (G1)

24 ottobre 2026. Prima tappa del piano G0–G8 approvato: librerie comuni senza allocazione, con
test host, per togliere i duplicati e preparare la GUI.

## Contenuto

- `lib/geometry/rect.h`:
  - `OS_RECT` e operazioni valid, empty, area, contains, union, intersect, clip;
  - rettangolo vuoto = larghezza o altezza 0;
  - nessun overflow di `X + Width` e `Y + Height`.
- `lib/geometry/damage.h`:
  - `OS_DAMAGE` fino a 4 rettangoli; oltre `Limit` si fonde la coppia la cui unione spreca meno
    area;
  - `Limit 1` equivale all'inviluppo unico di oggi;
  - questa politica di fusione serve alla futura present a più rettangoli (FW-011).
- `lib/containers/ring.h`:
  - `OS_RING`, coda di byte con capacità potenza di due e indici liberi;
  - in overflow conta il byte perso senza cancellare i byte già nella coda;
  - non è sincronizzata.
- `lib/containers/bitmap.h`: `OS_BITMAP` con set, clear, test e next, scansione per parola.

## Migrazione

Il danno della console (`framebuffer/console/runtime.c`) e quello della present VirtIO
(`virtio/iommu/present.c`) usano `OS_DAMAGE` con `Limit 1`: comportamento identico a prima.
PS/2 e la bitmap della console passeranno a `OS_RING`/`OS_BITMAP` nei rispettivi moduli.

## Prove

| Prova | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| `lib/scripts/test.ps1` (ASan, cppcheck): oracolo per pixel 6x6 di tutte le coppie di rettangoli, confini, 8000 sequenze casuali di danno con Limit 1..4 | PASS, 36.337.196 controlli | `lib-host` |
| Anello (riempimento, svuotamento, indici vicino a 2^32, overflow) e bitmap 1..300 bit | PASS, 118.316 controlli | `lib-host` |
| Suite host `console`, `x86-virtio`, `x86-framebuffer`, `kernel` | PASS | `run.ps1` |
| V2c | 10/10 | `pointer-interactive-check-b7f482258652452b90f9c18d59b36ccb` |
| Fallback iniettato | 2/2 | `...-994270ba8fe8428b9b0a1b9068f9d844` |
| Stallo e recupero | 2/2 | `...-c285d2d03df947baa050dbdaec69bc9e` |
