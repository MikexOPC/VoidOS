# K3-G — riserva della pagina iniziale AP

30 ottobre 2026. Scelta approvata: riserva precoce nel kernel, nessuna
estensione del contratto boot. [Design e fonti](k3g-design.md).
Risultati riferiti alle immagini finali elencate nel manifesto locale.

## Comportamento e lifetime

kernel_bootstrap_memory_prepare prenota subito dopo initialize/validate
dell'allocatore, prima di window e regioni persistenti. La riserva è la
prima lease (Id=1), una sola pagina con base tra 0x1000 e 0xff000 inclusi.
Non è un indirizzo fisso. L'allocatore accetta soltanto RAM già ammessa
dalla mappa validata: conventional WB, pagina zero esclusa.
Il percorso diagnostico storico -TestBootstrapMemory resta separato e
non prenota questa pagina: non è un profilo abilitato all'avvio AP.

kernel_physical_reserve_startup_page richiede NextId=1, Generation=0,
budget interamente libero e output vuoto. Sotto il lock fisico prende
la prima pagina disponibile soltanto se nel range ammesso, poi applica
il pin prima di restituirla. Invarianti inattese dopo la mutazione sono
terminali. Nessuna finestra tra pubblicazione della lease e pin.
Risultati dell'API: 0 errore/precondizione, 1 riservata, 2 non disponibile.
La prima extent basta perché le extent sono ordinate e già escludono zero.

C possiede pool, lease e stato; Rust controlla la geometria numerica con
test su tutti i 255 vettori validi e sui limiti. Nessun blocco unsafe Rust;
l'eccezione all'attributo unsafe_code riguarda soltanto no_mangle per FFI.
Il proprietario statico conserva Self e non è copiabile. Sono rifiutati
alias con pool/contesto lock, owner sporco, doppia prenotazione e uso tardivo
dopo altre allocazioni, anche se poi rilasciate.

Se manca una pagina idonea, viene pubblicato KERNEL_AP_LOW_UNAVAILABLE
come WARN; il BSP continua senza lease per l'avvio. La prenotazione valida
pubblica KERNEL_AP_LOW_RESERVED. Entrambi precedono KERNEL_PHYSICAL_OK.
Il diagnostico -TestApLowAbsent forza il rifiuto della candidatura senza
mutare la mappa firmware: simula indisponibilità, non dimostra firmware
realmente privo di RAM bassa. I casi host usano mappe realmente prive di
RAM bassa idonea.

Nessuna scrittura nella pagina fisica, mapping eseguibile, alias basso,
nuova tabella o istruzione INIT/SIPI in questo blocco. La riserva resta
pinned durante il boot e non viene conteggiata come leak da correggere.
L'API di rilascio è solo per la preparazione inattiva; il proprietario
nativo non viene esposto come API operativa. Prima del futuro avvio AP
servirà un commit irreversibile anche di questa riserva e delle tabelle
di ingresso, coerente con K3-E/F. Nessun timeout autorizzerà il rilascio.

## Prove finali

Manifesto locale: kernel/build/k3g-final-validation.json. Contiene hash
SHA-256 di sorgenti/adattatori e delle tre immagini, confrontati con gli
hash effettivamente registrati dalle campagne. Artefatti non versionati.

| Verifica | Risultato | Evidenza in kernel/build/ |
| --- | --- | --- |
| Suite host dedicata | PASS, due test Rust in debug e release; dieci ripetizioni C ASan | test-run-a225c3ca589945c7af81d1ce80ed1ee6/results.json |
| Dettagli host | Allocatore reale, IRQ/contesto modellati, ownership/rollback/budget | ap-low-host-ba199844c65d43f683641485dc3c80ec/ |
| Riserva QEMU | 10/10 consecutivi, alternanza una/quattro vCPU | ap-low-firmware-c620ac32b7d545a182196fb84080c97d/results.json |
| Indisponibilità iniettata | 1/1, BSP pronto senza lease di avvio | ap-low-firmware-b632e3d8c9b14bd6a767c42be80ff94a/results.json |
| Boot ordinari | 10/10, controlli per-CPU/arena/guardie preesistenti | percpu-firmware-c0747f36619b4c8eacd11df3b68d2f32/results.json |
| Console VirtIO/VT-d | 1/1, pixel, input, cursore e cleanup | pointer-interactive-check-1a063c9e81a8403f9fdc8aff037748ee/results.json |
| Bootstrap host | PASS, inclusa posizione della nuova chiamata prima della finestra | test-run-32855490f8aa4c7aac50b9a9b2ae9d14/results.json |
| Allocatore host isolato | PASS, ASan e 2.000/4.000/8.000 cicli con 2/4/8 thread | test-run-72cabc8c51d44406b4dc9cfe6f1864c6/results.json |
| Kernel host | PASS | test-run-a784220388704bb2aa4814aa6317ee7c/results.json |
| Campagna registrata, smoke seriale | PASS 1/1, worker e wrapper conclusi con exit code zero | qemu-run-decb077c6cfd4fd68687296f0ab3b300/results.json |

Il monitor QEMU legge l'owner statico, i record reali Live/Pins e gli
intervalli liberi dell'allocatore; riscontra Id=1 e pagina non riutilizzabile.
Legge la mappa firmware conservata nell'arena e verifica che il range
sia coperto da un solo descrittore conventional WB. Legge tutte le 256
voci PML4 basse dal CR3 operativo: rimangono zero. Non basta il marker.
Una riserva ancora pinned a fine prova è intenzionale, non prova di rilascio.

Build MSVC/MASM/Rust con warning come errori; cppcheck del modulo e delle
suite senza diagnostica bloccante. L'analisi nativa in k3g-native-static/
ha sette information e uno style, zero error/warning/performance/portability.
Gli include di sistema mancanti restano un limite dichiarato dell'analisi.
Massimo sorgente/script modificato 374 righe; modulo nuovo sotto 120 righe
per file, massimo tre file diretti nella cartella funzionale. Registro
JSON delle campagne trattato come dati di configurazione, non sorgente C.
Controlli di whitespace, CR isolati, sintassi Python e link locali passati.

## Problemi di collaudo e correzioni

Una seconda esecuzione dello stress host, avviata in parallelo al QEMU
video pesante, ha superato i 60 secondi di WaitForMultipleObjects nel caso
a otto thread. Il fallimento è conservato in
test-run-7f051ffa44e34555bc1ddaa6855b7865; non è stato riclassificato PASS.
La prima esecuzione era passata, ma il wrapper era rimasto aperto dopo il
risultato: aggiunta uscita esplicita soltanto per successo standalone.

Correzione dell'orchestrazione: stress ripetuto dopo la chiusura di QEMU,
stesso workload e stessi timeout, wrapper terminato regolarmente con
exit code zero. Non vengono garantiti tempi sotto carico host arbitrario.
Non eseguire questo stress contemporaneamente a campagne QEMU pesanti;
il workflow resta suite host, poi QEMU. Nessuna soglia aumentata per
trasformare il fallimento in successo. I vecchi runner conclusi sono stati
terminati solo dopo verifica di identità e assenza di figli di calcolo.

Il primo comando di build console ometteva -Ps2Mouse ed è stato rifiutato
dal validatore del profilo, prima della compilazione. Corretto il comando;
non è un crash guest. Il silenzio prolungato nella fase VT-d della prova
finale non è stato considerato PASS: il test è proseguito entro i 600 secondi
già previsti e ha verificato input, screenshot e rilascio finale.

Suite riproducibile: ./scripts/test/run.ps1 -Suite percpu-low.
Campagne registrate: percpu-low e percpu-low-absent in scripts/test/qemu.ps1.
La prima include il probe lock esistente; i dieci boot finali del report
usano invece l'immagine ordinaria, priva di quel probe. Smoke non sostituisce
la prova consecutiva completa.

I test Rust sono centralizzati sotto test/arch/x86/kernel/cpu/percpu/lowmemory/.
L'oggetto di produzione dopo questo spostamento è identico byte per byte
a quello nelle immagini finali: SHA-256
20e619cf3438bea4f66930d04e3c1c19195389e7d6a61091729628a7933933cb.
Nessun codice di test rimasto nel sorgente Rust di produzione.

Lo smoke ha rilevato anche problemi del wrapper: il ramo PowerShell 5.1
restava aperto dopo la generazione dell'ELF; nel tentativo successivo il
worker Python aveva completato la prova ma il wrapper non terminava
l'attesa tramite Process.WaitForExit/Dispose. I due tentativi sono mantenuti
come falliti in qemu-run-7e1ae78ee92d44ee8b5be48cdd6b4957 e
qemu-run-c4e61cf582a148a387240cc1a5557580. Anche i tentativi intermedi
qemu-run-d03e52362deb4d8da95887f46d98e485,
qemu-run-1397d68cd14b4600b545d4cb5cce3937 e
qemu-run-f3fb01e8d2ee4158a34f019af21ff84a sono stati interrotti e conservati
come falliti; nessuno è un crash guest o una prova completa del wrapper.
Il solo cambio di versione o l'uscita esplicita della build non bastavano.

Il percorso seriale finale usa PowerShell 7 e invoca campagna e build
nello stesso host, con scope script separati. I parametri di build sono
convertiti in una hashtable validata, senza valutare stringhe come codice;
verificati contro il param block per tutte le campagne registrate.
Invocazione sincrona del runner, stdout/stderr distinti ed exit code
controllato: qemu-run-decb077c6cfd4fd68687296f0ab3b300 è passato interamente.
I runner legacy non Python conservano il loro eseguibile precedente.
Il percorso opzionale -Parallel > 1 non è ricertificato in questo blocco.
PowerShell 5.1 viene rifiutato con il comando del runtime 7 disponibile:
non viene presentato come percorso verificato. Nessuna diagnosi generale
sul runtime Windows è dedotta da questi episodi.

## Confini

Solo BSP OS attivo; una/quattro vCPU configurate non dimostrano SMP.
Valgrind e hardware fisico non eseguiti; coverage linee/branch N/D.
ASan riguarda il C host, non tutta la RAM fisica o i componenti Rust.
Il seguito è root di transizione, ingresso 16/32/64 bit e avvio AP, prima
delle prove NMI remote richieste da K3-F. Nessun commit/push o modifica a reuse/.
