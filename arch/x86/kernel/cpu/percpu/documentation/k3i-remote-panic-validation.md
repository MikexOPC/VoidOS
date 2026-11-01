# K3-I — panic remoto sul primo AP, 1 novembre 2026

## Risultato

Il BSP provoca un panic volontario dopo l'ammissione del primo AP.
Il backend reale invia NMI fisico unicast all'AP, che entra tramite
IDT definitiva e IST2 privato, pubblica il record remoto e conferma
l'arresto. Owner BSP, online 3, targets=ACK=2, missing=outcome=0.
I due AP mai avviati conservano record vuoti e contesti PREPARED.

È una prova reale BSP/AP con interrupt ordinari disabilitati, non un
simulatore di ACK. Non aggiunge scheduler SMP o avvio AP ordinario.
Il protocollo di produzione C/Rust/Assembly non è stato cambiato:
sono aggiunti il profilo diagnostico e le verifiche necessarie.

## Implementazione e debug

`-TestApRemotePanic` richiede launch positivo e rifiuta NoNmi. Il probe
usa `xp_panic_voluntary` soltanto dopo PARKED/provider/online verificati;
la ragione numerica è `0x41505f50414e4943`. Nessun messaggio arbitrario
viene dereferenziato e nessun nuovo accesso hardware è introdotto.

Il primo esperimento ha prodotto ACK corretto, ma il runner non passava
il panic atteso al controllo condiviso della memoria bassa. Corretto
inoltrando la lista esatta dei FAIL ammessi tramite il parametro già
esistente: soltanto `KERNEL_SMP_PANIC`, non un filtro generico sui FAIL.
La prova fallita rimane in
`kernel/build/first-ap-firmware-8ee8990917714751bbee910d66d65546/`.
Non è contata fra le dieci prove finali.

## Prove finali

- Panic remoto: **10/10 consecutivi**, immagine
  `kernel/build/k3i-remote-panic-v1/kernel.elf`; risultati e hash in
  `kernel/build/first-ap-firmware-ee0714b54c53436786da304991fc08c7/results.json`.
- Regressione launch senza panic: **10/10 consecutivi**, immagine
  `kernel/build/k3i-remote-launch-regression/kernel.elf`; risultati in
  `kernel/build/first-ap-firmware-471f31f863264bf9b1ef4c261e952f14/results.json`.
- Suite panic host, exit 0: **6 test Rust debug e 6 release**, dieci
  esecuzioni C dei record con otto produttori concorrenti ciascuna,
  **11 scenari trasporto** e **10 scenari output**. C con ASan, /W4 /WX;
  Rust -D warnings. Log `kernel/build/k3i-remote-panic-host.log`.
  I/O, clock e partecipanti delle prove host sono modellati.
- Cppcheck 2.22.0: suite host e probe remoto senza diagnostiche bloccanti;
  informazioni sugli include e suggerimenti di stile conservati negli XML.
  Probe: `kernel/build/k3i-remote-panic-static/cppcheck.xml`.
- Audit disassemblato degli undici oggetti d'emergenza: nessuna dipendenza
  FP/SIMD rilevata. Log `kernel/build/k3i-remote-panic-objects.log` e listing
  accanto agli oggetti dell'immagine diagnostica.

Il runner legge stato atomico, due record completi, stack NMI e registri
reali di entrambi i core. Richiede terminali BSP DONE e AP STOP, IF=0,
stesso CR3, GS/GDT/TR/IDT privati AP, frame NMI in IST2 e RSP interrotto
nel suo stack principale. Verifica anche record non usati, pinning, budget,
guardie, tabelle, codice e dati immutabili della transizione.
Il rapporto seriale deve concordare con la memoria e arrivare completo
prima che il runner fermi la VM posseduta.

I runner finali, suite host e audit oggetti hanno exit 0. I wrapper delle
due build, rimasti aperti dopo la generazione degli ELF, sono stati chiusi
dopo le rispettive campagne: il loro exit d'interruzione non è dichiarato
build PASS. Le immagini eseguite e gli oggetti sono comunque prodotti
dalla compilazione /WX e dal link controllati dallo script.

Manifest di immagini, prove e sorgenti:
`kernel/build/k3i-remote-panic-manifest.json`.
Audit sorgenti: `kernel/build/k3i-remote-panic-source-audit.json`.
Massimo osservato 385 righe; il nuovo tetto utente è 1000, senza obbligo
di allungare i file. Nessuno spazio finale o tab nei file interessati.

## Limiti e seguito

Nessuna nuova allocazione nel probe; accounting e lease guest verificati,
senza dichiarare un leak detector fisico. ASan strumenta solo il C host.
Coverage, TSan, Valgrind e hardware fisico N/D. Non ripetute in questo blocco
le campagne ordinarie e il negativo senza NMI del checkpoint primo AP.

Il panic è originato soltanto dal BSP, con un AP online. Restano origine
panic da AP, gruppo completo, gestione del riuso della transizione e
avvio sequenziale degli altri AP prima dell'integrazione ordinaria.
K3 rimane aperto.

Fonti del protocollo invariato:
[Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
§6.7.1 NMI e blocco fino a IRET, §6.14 IDT/IST, §11.6.1 ICR NMI unicast.
Il [design](k3i-remote-panic-design.md) applica le scelte SMP già approvate.

## Verifica del checkpoint Git

Prima del commit cumulativo K2/K3: 50/50 suite host passate in
`kernel/build/test-run-6d25a0e85d984aae9b3b0ec1de7674d2/results.json`;
10/10 AP/panic con `kernel/build/precommit-ap/kernel.elf`, evidenze in
`kernel/build/first-ap-firmware-d178ff203d1f4aab92c0d44a6f63fda1/`;
10/10 ordinari con `kernel/build/precommit-ordinary/kernel.elf`, evidenze
in `kernel/build/percpu-firmware-6504f73f792e4a46877a719345f8831c/`.

Il runner registra le suite host AP mancanti e mantiene separata la prova
CPU privata che richiede pacchetti preparati. L'attesa del processo usa
una deadline host di dieci minuti, invece della variante indefinita che
ha bloccato campagne preliminari dopo l'output positivo delle suite.
Le campagne interrotte non sono conteggiate come PASS.
L'esclusione degli artefatti preserva ora gli script sorgenti sotto
`kernel/scripts/build/`. Nessuna chiusura di K3 o prova hardware fisica.
