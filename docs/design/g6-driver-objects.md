# G6: driver come oggetti

25 ottobre 2026. Decisioni approvate: O1, O2-A, O3, O4, O5, O6. Si applicano le regole del §4 di
[design del consolidamento](consolidamento.md#oggetti-driver).

## Decisioni

| | Decisione |
|---|---|
| O1 | Ogni driver migrato è una struttura con un nome; ogni funzione riceve `self`; nei file del driver non resta nessuna variabile `static` di stato |
| O2-A | L'istanza vive in un solo posto per sottosistema, la "composition root", come storage statico passato per puntatore. L'heap (O2-B) servirà quando ci saranno più istanze, per esempio le finestre della GUI |
| O3 | La console non chiama più VirtIO: riceve un `KERNEL_DISPLAY` (`kernel/drivers/video/display.h`) con `Check`, `Poll`, `Busy`, `Redraw`, `Rect`. `Ops = 0` vuol dire GOP |
| O4 | Ogni oggetto dichiara nell'header il proprio contesto e ha un campo `Lock`, riservato a K2. Le dipendenze arrivano come parametri: la mappatura GPU alla sessione, il display alla console, il clock della present dal trasporto |
| O5 | Migrati subito present, sessione VT-d e console. Il dominio VT-d (13 `static`) verrà migrato quando lo si tocca |
| O6 | Test host della present (oggi possibile, prima no) e test di sessione della console contro l'oggetto collegato, non più incluso |

## Oggetti

| Oggetto | Header | Proprietario (composition root) | Contesto |
|---|---|---|---|
| `X86_VGPU_PRESENT` | `virtio/iommu/present/present.h` | dentro `X86_VGPU_SESSION` | open e close nell'avvio e nell'arresto della sessione (IF=0); il resto solo nel task della console |
| `X86_VGPU_SESSION` | `virtio/iommu/session.h` | `virtio/integration/kernel.c` (`x86_vgpu_kernel_session_start/stop`, `x86_vgpu_kernel_display`) | avvio e arresto prima e dopo i task; poll e display solo nel task della console |
| `X86_CONSOLE` | `framebuffer/console/runtime.h` | `input/ps2/runtime/session.c`, raggiunto da `X86_PS2_SESSION.Console` | apertura e chiusura nel setup e teardown; edit e submit nel task 0, output nel task 2, flush, pending e audit nel task 1, mouse nel task 3 |

Tutti gli oggetti hanno un solo owner su una sola CPU, quindi oggi non serve un lock. Con K2 il
campo `Lock` diventa uno spinlock e l'header dice quale.

Altri cambiamenti:
- La present trova il proprio oggetto a partire dal target incorporato (`offsetof`) e rifiuta le
  copie, cioè i target il cui `Self` non coincide.
- La vista CPU della superficie è un campo (`Pixels`), non una costante dentro il recupero.

## Prove

| Prova | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test host della present: merge con un solo aggiornamento in volo, SLOW una volta, timeout, recupero distanziato di 5 s e limite di 3 tentativi, fault non recuperabili, post fallito, geometria, controllo del target | 108 controlli, PASS | `vgpu-present-host` |
| Test di sessione della console contro l'oggetto collegato (non più incluso), tastiera e mouse | 8013 e 8046 controlli, PASS | `console-grid-host` |
| Suite host complete | 34/35 (virtio-discovery: `read.c` dell'utente) | `test-run-f3a70325e64c4c8ab81f9a0beb44d239`, più `ps2` rieseguita dopo la correzione degli spazi |
| V2c ×10, fallback ×2, stall ×5, plain ×3, absent ×2 | 22/22 boot | `qemu-run-23ca6c7a8fe648d4a80b3ea47accf2ed` |
| Smoke set (12 campagne, 3 in parallelo) | 12/12 | `qemu-run-2f4b5a3813424dcab7117c532d51fab5` |

Flush medio V2c su 10 boot: 7.440 tick PM con G6, contro 7.544 con G5 (12.291 flush, 41 audit).
Le chiamate tramite `KERNEL_DISPLAY` non costano in modo misurabile.

## Script di test toccati insieme a G6

- **cppcheck:** livello normale con `-j` di default e livello esaustivo con `OS_CPPCHECK_LEVEL=exhaustive`. Le invocazioni `& cppcheck ... 2> file` sono passate a `Invoke-CppcheckRaw` (`Start-Process`): in PowerShell 5.1 la forma diretta lasciava cppcheck fermo sulla pipe, ed era la causa delle esecuzioni da un'ora. La suite console passa da circa un'ora a 30 s; le suite host complete girano in 6,8 minuti.
- **`qemu.ps1 -Parallel N`:** la logica della singola campagna sta in `scripts/test/campaign.ps1`. Le campagne senza build propria girano da sole, e quelle segnate `heavy` (VirtIO/IOMMU) una alla volta.
- **Deadline V2b in `interactive.py`:** passa da 240 a 600 s, perché la stessa immagine ha superato i 240 s con la macchina più lenta (circa 300 s fino a `KERNEL_POINTER_READY`).
