# Input PS/2 v0: struttura e criteri di completamento

9 ottobre 2026. Riorganizzazione autorizzata dall'utente. Un solo blocco
funzionale: pipeline PS/2 esistente, con i collegamenti di build e test.
Nessun nuovo driver, accesso hardware, algoritmo scheduler o linguaggio.
`reuse/` resta intatto. I contratti gia scelti sono conservati.

## Responsabilita e dipendenze

| Percorso | Responsabilita |
| --- | --- |
| `kernel/drivers/input/ps2/controller/` | Negoziazione, deadline, discovery FADT e start/stop |
| `kernel/drivers/input/ps2/rust/decoder/` | Parser puro scan set 2, nessun I/O o shell |
| `kernel/input/keyboard/` | Modificatori, layout italiano e codifica UTF-8 |
| `kernel/console/line/` | Buffer limitato, editor e validazione scalari |
| `kernel/shell/` | Comandi e FIFO delle righe |
| `arch/x86/kernel/drivers/input/ps2/` | Accesso porte, route IRQ e orchestrazione task |
| `kernel/input/integration/rust/ffi/ps2.rs` | Gateway ABI della pipeline |

Il gateway compone i nuclei Rust per il collegamento freestanding corrente.
E codice di integrazione: il driver non include editor, layout o shell.
Il backend x86 possiede la sessione e connette queste interfacce.
Non e ancora un framework generico per dispositivi o input multi-sorgente.
Le API `kernel_ps2_*` restano compatibili; una rinomina non aggiungerebbe
sicurezza a questo blocco e renderebbe piu difficile confrontare le prove.

C rimane nella cartella funzionale; Rust in `rust/<funzione>/`;
test nei rispettivi moduli, script in `scripts/`, artefatti in `kernel/build/`.
Le prove integrate si avviano da `kernel/drivers/input/ps2/scripts/test.ps1`.
Il controllo `structure.py` verifica dimensioni, cartelle, UTF-8/LF,
whitespace, include, attributi Rust e link documentali, salvando hash.

## Driver operativo v0

Il profilo esplicito `-Ps2Input` esegue discovery, selezione/query set 2,
start dopo ownership IRQ, raccolta limitata nell'handler, coda byte e
decoder nella task. Eventi con generazione evitano il risveglio perso.
Il testo italiano passa all'editor e alla task shell separata tramite FIFO.
`exit` o Esc arrestano la sessione, disabilitano sorgente/IRQ e verificano
rilascio dei binding, tre stack e budget pagine. Nessun heap nel controller.

I test del controller aggiungono 10.000 cicli prepare/start/stop, con
clock vicino al wrap, rifiuto del doppio start e di tutte le operazioni
sull'owner chiuso senza accesso alle porte. I test gia presenti verificano
failure di ogni callback, retry cleanup, timeout, clock fermo, risposte
errate, RESEND esaurito e readback discordante.
Queste sono prove host con hardware simulato, distinte dai boot QEMU.

Le fonti dei registri e le sequenze sono conservate in
[controller](controller.md) e nel [runtime x86](../../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md).
Nessun cambiamento di porte, bit, timeout, ack/EOI o Assembly in questo blocco.

## Limiti e lavoro successivo

Completamento v0 significa il profilo QEMU e i contratti sopra descritti.
Non significa compatibilita con hardware fisico o sessione ordinaria
illimitata. Mancano hotplug, riavvio della sessione dopo guasto, LED,
keypad completo e input USB. LOSS porta ad arresto pulito; non ricostruisce
i byte persi. Saturazione FIFO/task e fault del device sono verificati
nei mock; non dichiarati riprodotti sul device QEMU se non indotti.

ASan strumenta il C host, non il Rust o la memoria fisica del guest.
L'accounting del guest verifica il budget del profilo; non e una prova
universale di assenza di leak. Valgrind e coverage linee/branch: N/D.
Nessuna prova SMP o ring 3. C++ non introdotto senza un caso concreto.

## Riproduzione

```powershell
& kernel/drivers/input/ps2/scripts/test.ps1
& kernel/scripts/build.ps1 -Ps2Input -OutputDirectory D:/OS/kernel/build/ps2-structure-final
& arch/x86/kernel/drivers/input/ps2/scripts/firmware.ps1 -SkipBuild -KernelDirectory D:/OS/kernel/build/ps2-structure-final -Boots 10
& kernel/drivers/input/ps2/scripts/firmware.ps1 -Ordinary -Boots 10
```

## Snapshot delle prove prima del raggruppamento sotto kernel/drivers

- [Suite host finale](../../../../build/ps2-structure-host-final.log):
  24 test Rust sia debug sia release, warning come errori e Clippy;
  C/ASan, 526.725 controlli controller, 1.114.132 testo, 2.680.276 editor,
  220.038 shell, 7.989 discovery e 4.559 runtime. Bridge FIFO full/wait/retry,
  cancellazione e EOF passano con scheduler mock.
- [Analisi statica](../../../../build/ps2-decoder-bf6569b9e0764d8cae9232ff8e330096/cppcheck.xml):
  20 file, zero error/warning/performance/portability; diagnostiche style
  e information restano, incluso unusedFunction nell'analisi parziale.
- [Build](../../../../build/ps2-structure-build.log): immagine ELF
  verificata; MSVC /W4 /WX. I messaggi di canonicalizzazione percorsi Rust
  della build sandbox non sono warning C o test falliti.
- [Input QEMU 10/10](../../../../build/ps2-input-firmware-60936e58bf12496fabc54767f52a4c74/results.json):
  tasti, modificatori, righe, shell, IRQ durante task, idle e cleanup;
  marker KERNEL_READY, nessun fault inatteso, budget invariato.
- [Stress](../../../../build/ps2-input-firmware-61af231647bc4c979c37f3fd86abdbfd/results.json):
  stessa immagine, 612 pressioni, 610 release, 578 caratteri, 12 righe,
  12 comandi gestiti, un overflow rifiutato, 1.617 IRQ durante task e
  59 idle; budget 20.467 -> 20.467. Il rilascio finale puo arrivare dopo
  lo stop; Pause non emette break. Il gate usa il minimo motivato dal protocollo.
- [Regressione ordinaria 10/10](../../../../build/ps2-decoder-firmware-577ea4f924304c4594bd3a98f4e1da9a/results.json).
- [Controllo struttura e hash](../../../../build/ps2-structure-check.json):
  47 sorgenti/script, massimo 272 righe, massimo cinque file sorgente
  diretti, zero link/include mancanti o include circolari locali.
  UTF-8, LF e whitespace verificati anche sui file non tracciati.

SHA256 immagine input:
`CDEC40DDD9C82E54E137DC96533D0EA99AA6F289D40D988588874F24DF5120CD`.
Le prove precedenti nei report spostati restano snapshot delle loro immagini.

Durante la migrazione i gate hanno rilevato percorsi x86 alterati per
errore e stringhe UTF-8 ricodificate. Entrambi sono stati corretti prima
della suite finale e dei boot sull'immagine sopra. Nessun cambio semantico
del driver e stato necessario dopo la revisione del controller e del runtime.
La vecchia cartella vuota `kernel/input/ps2/` e stata rimossa solo dopo
aver verificato che non contenesse piu file.

Esito: blocco PS/2 v0 consolidato per il profilo dichiarato. Prossimo
modulo proposto: console framebuffer, con progetto e criteri propri.

## Convenzione definitiva dei driver

Correzione richiesta dall'utente: i driver appartengono a
`kernel/drivers/<categoria>/<dispositivo>/`. Il backend dell'architettura
segue `arch/x86/kernel/drivers/<categoria>/<dispositivo>/`. Il modulo
corrente e `input/ps2/`; storage, rete e altre categorie si aggiungono
quando esiste il modulo, senza accumulare cartelle segnaposto.
Convenzione registrata anche in direttive-locali.

Questo secondo spostamento aggiorna include, gateway Rust, build, harness,
test e link; non modifica sequenze hardware, ABI, buffer o algoritmi.
Il collegamento LAPIC al profilo PS/2 usa ora il nuovo header della sessione.
I report storici sopra conservano gli hash delle immagini gia verificate.

Verifica finale del raggruppamento:

- [Host](../../../../../kernel/build/ps2-grouping-host.log): 24 test Rust
  debug/release, C/ASan, Clippy, controller 526.725 controlli e bridge/runtime.
- [Cppcheck](../../../../../kernel/build/ps2-decoder-81b584690cc24e9e855eda24a7b2d5c3/cppcheck.xml):
  20 file, zero diagnostiche bloccanti; style/information restano.
- [Build](../../../../../kernel/build/ps2-grouping-build.log), immagine input
  `78461FB475344A18D5C796AACBDE96C9538DA3CECE4FE0CFEDF60BA76AA821B1`.
- [Input QEMU 10/10](../../../../../kernel/build/ps2-input-firmware-d604f35ac99b47e791e7359e4ba8dc27/results.json):
  tasti, testo, editor, shell, IRQ/idle e cleanup, senza fault inattesi.
- [Ordinario QEMU 10/10](../../../../../kernel/build/ps2-decoder-firmware-c39639d14ea740b496e4eb7b46081f2d/results.json).
- [Struttura/hash](../../../../../kernel/build/ps2-structure-check.json):
  47 sorgenti/script, massimo 272 righe, include/link locali validi,
  nessun ciclo locale, UTF-8/LF e whitespace verificati.

Lo stress del primo spostamento resta evidenza della sua immagine, non
un nuovo stress eseguito sul raggruppamento. Hardware fisico e misure
globali di leak/coverage restano non verificati come dichiarato sopra.
