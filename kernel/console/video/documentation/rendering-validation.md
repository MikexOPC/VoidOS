# Verifica del gateway console — 10 ottobre 2026

Il [contratto](rendering.md) collega la griglia al framebuffer nel profilo
diagnostico. La shell ordinaria resta seriale. Questo checkpoint non dimostra
ancora latenza accettabile con tastiera e watchdog attivi durante il disegno.

## Immagini finali

| Profilo | Immagine in kernel/build/ | SHA256 |
|---|---|---|
| Diagnostico | console-render-diagnostic-final/kernel.elf | A35ADBD2AE50E9B0EA163FAB6073E5C3C3DC570E6D58088B0F6DB72C1CCC42A1 |
| Ordinario | console-render-ordinary/kernel.elf | 2D7F3EE05E2A20968201FF0BE04445B6BDB662C1AE288DE581E6825EED7F39BF |

Loader invariato: EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7.
Build con toolchain MSVC/MASM supportata e warning come errori. Nessuna copia
nella ESP o push in questo blocco.

## Test host e revisione

- console-render-host.log: 374635 CHECK del nucleo C con ASan e 10000 riusi;
  test del flusher su budget, retry, dirty conservato, rientro, owner copiato,
  wrap della scansione e overflow. Quattro casi Rust debug/release e Clippy.
- Gateway: 2228226 casi scalar nei due stati IF, UTF-8 e font reali;
  clock, renderer e interrupt sono mock. Verifica restore dopo successo e
  fallimento, bounds, colori, assenza TSC e argomenti invalidi.
- console-render-harness-host.log: cinque test Python del wrapper, con
  provider simulato e controllo dei marker, hash, conteggi e durata positiva.
- console-render-framebuffer-host.log: regressione framebuffer, 16 casi Rust
  per profilo, Clippy, sette gruppi lifecycle x100, tre gruppi pixel e quattro
  gruppi drawing x100 C/ASan/accounting.
- cppcheck su nove translation unit console/gateway: nessun error, warning,
  performance o portability; le note style/information non sono zero.
- console-render-native-disassembly.txt: serializzazione CPUID/RDTSC/CPUID,
  salvataggio flag, CLI e restore condizionale STI/CLI verificati.
- console-render-review.json: controlli di struttura/stile; sorgente massimo
  152 righe, cartelle funzionali entro cinque sorgenti.

La revisione statica ha individuato una lettura del probe prima del controllo
del limite. Corretta con lunghezza verificata prima del ciclo e sizeof del
literal, poi ricompilata l'immagine e ripetuti i dieci boot diagnostici finali.
Le precedenti prove diagnostiche non sostituiscono queste.

ASan strumenta il C host, non Rust, Assembly o RAM fisica. Il nuovo modulo non
alloca heap; la griglia diagnostica occupa 66360 byte statici e il flusher 56.
Accounting dei lease coperto dalla regressione; Valgrind non disponibile su
questa configurazione Windows. Coverage linee/branch non misurata: N/D.

## QEMU finale: trenta boot

Percorsi relativi a kernel/build/. Ogni campagna ha verificato hash immagine,
KERNEL_READY, assenza di fault inattesi e chiusura della propria VM.

| Campagna | Risultato | Evidenze |
|---|---|---|
| Diagnostico | 10/10 consecutivi | framebuffer-live-a48d10163dfc4ef78a5e15b7714af4ab/ |
| Ordinario | 10/10 consecutivi | framebuffer-live-d846b962d74f48698f239a0769e80c37/ |
| Senza display | 10/10 consecutivi | framebuffer-live-a0211dd4c7104164a917ce3f49c2f42a/ |

Ogni diagnostico aggiorna 72 celle, 56 con IF iniziale a uno; richiede restore
IF e misura positiva. Confronto di 26053 pixel sia nella memoria fisica sia
nel PPM, inclusi 9216 pixel della nuova griglia: accenti, euro, Backspace,
scroll e fallback visibile. L'oracolo usa il convertitore TTF del progetto:
confronto indipendente dagli store del kernel, non una seconda implementazione
indipendente della rasterizzazione tipografica. Inventario delle 768 foglie,
guardie e rollback restano verificati dal provider framebuffer.

Massimi TSC per boot: 9014751, 25236275, 9764402, 10603935, 8470009,
9700310, 8714306, 15870814, 10492498, 16531764. Intervallo dei massimi:
8470009–25236275 tick. Include overhead e possibili IRQ ai bordi; nessuna
conversione in millisecondi o misura esatta del tempo IF=0.

## Limite per il prossimo blocco

Le sorgenti hardware sono ferme durante il probe. IF=1 e restore verificati
non provano progresso degli IRQ, assenza di byte persi o deadline del watchdog
durante il rendering. Prima dell'output shell: clock appropriato, carico con
PS/2 e watchdog attivi, accounting e budget motivato. Hardware fisico non
testato. Nessun avanzamento alle funzionalita dipendenti da quel gate.
