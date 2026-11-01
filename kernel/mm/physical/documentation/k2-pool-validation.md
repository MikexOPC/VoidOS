# K2-B: lock del pool e lettori coerenti

Snapshot del 28 ottobre 2026: blocco allocatore verificato prima della
chiusura K2. [Gate finale del 29 ottobre](../../../sync/documentation/k2-integration.md).
Heap, holdings e lifecycle dei mapper conservano i limiti descritti sotto.

## Modifica

Facade pubblica con un lock per owner, rango foglia 300, contesto CPU e
irqsave; core privato senza riacquisizione. Query piccole approvate
in k2-design.md; nessun callback o accesso hardware sotto il lock.
Contatori del lock separati nei confronti di rollback dei metadati.

Migrati i lettori di budget/generazione, Free/Live/Pins nei chiamanti di paging,
bootstrap, stack, ACPI, timer e driver. Le identità immutabili restano
leggibili solo su owner pubblicato e vivo. Capacità di rilascio rivalidata
nella transazione unpin/release dopo la revoca dei mapping; fallimento
per saturazione conserva lease e pin invece di rendere la pagina riusabile.

## Prove e confini

Le suite host reali collegano facade/core/query e atomiche Rust; il backend
IRQ/CPU è simulato. I test concorrenti usano contesti TLS distinti e veri
thread Windows, non gli AP del guest. Gli adattatori che sostituiscono
l’allocatore usano reader_model.c, dichiaratamente un modello: non sono
prove di lock, atomicità o memoria fisica.

L’ASan copre core/facade/query/guard e relativi test, non gli oggetti C del
supporto sync costruiti dal helper; la suite sync misura separatamente i
propri sorgenti. Accounting del pool distinto dal rilevamento di leak host.
Valgrind non eseguito; coverage linee/branch N/D; nessuna prova hardware.

## Problemi trovati e corretti nel blocco

- Il confronto di rollback includeva i ticket del lock: ora esclude solo
  Gate, continuando a confrontare tutti i metadati dell’allocatore.
- Due access.c generavano lo stesso nome oggetto nel compilatore MSVC:
  il nuovo adapter fisico si chiama guard.c.
- La migrazione del budget in un helper void richiedeva propagare errore:
  adopt_plan ora restituisce esito e il bind lo controlla prima delle risorse.
- I test di componenti con allocatore simulato necessitano delle nuove
  query simulate; non si collega il modello nelle suite con pool reale.
- Il controllo statico della variabile di output segnalava un percorso
  interprocedurale ambiguo: temporanei inizializzati, senza ignorare warning.

## Limiti intenzionali

Pool concorrente host non significa kernel SMP: il provider nativo accetta
solo BSP verificato. Nel bootstrap non c’è clock di attesa collegato:
si ammette solo percorso non conteso, contesa dopo ticket resta terminale.
Niente uso NMI/fault, yield o trasferimento del guard fra task. Holdings,
mapper e policy di budget globale restano BSP; heap da trattare dopo questo
blocco. La diagnosi esatta del vecchio panic LAPIC non è retroattivamente
attribuita a questo lavoro; vedere il report timer.

## Risultati verificati

- Host: 39/39 suite verificate in aggregato. Campagna completa
  `test-run-0d589d4ad49e45d68a9c2ebb39efcce9`: 36 PASS, tre errori di
  link nei mock di scheduler/PS2/console, corretti e rieseguiti. Non è un
  singolo run completamente verde. Riepilogo con percorsi di tutte le
  prove: `kernel/build/k2-pool-host-summary.json`.
- Scheduler ripetuto: `test-run-5340bcd92a3c4e65a8367284d3e9c1f5`;
  PS/2: `test-run-c840d28374c2481090c5a43b383df497`.
- Sull’ultima query della generazione: physical
  `test-run-11b054618e7643d0b4bc1605ea0f815b`, console
  `test-run-5a60d64565a746d396bb75c6704bde59`, framebuffer
  `test-run-ad849029d99d475783c35ff5f7e42527`. Tutte PASS, compilatore
  con warning come errori e nessuna diagnostica statica bloccante.
- Allocatore finale: dieci ripetizioni ASan dei gruppi di unit test;
  2/4/8 thread host con 2.000/4.000/8.000 cicli lease, 14.000 totali;
  claim atomici e payload senza sovrapposizioni, pin e budget ripristinati,
  ticket Next=Serving e handle Windows posseduti chiusi.
  `physical-host-91384c769ba540f4bbadbb33f7e1fe77`.
- Bootstrap: 490 check, 22 failure API iniettate, ASan; allocatore e mapper
  sono mock (`bootstrap-host-9c41a32f03e04053b459b8398a27d1b6`).
- Prima dell’ultima query di generazione: dieci boot lock e due negativi
  attesi, `qemu-run-14310607f3ab43058798a2efbfa8a6c9`; dieci ordinari,
  `sync-firmware-a5e8e803baa14afda7f37ad7d30c2951`. Queste prove non
  sostituiscono le campagne dell’immagine finale.
- Prima dell’ultima query di generazione: GOP e VirtIO/IOMMU, una sessione
  ciascuna, `qemu-run-9fbeed6229254be4a9d839a55b2d65cc`, PASS. Immagini
  e screenshot isolati, movimento, comandi e budget dopo cleanup verificati.

Review dei sorgenti: nessun sorgente modificato oltre 400 righe e nessuna
cartella funzionale interessata oltre cinque file diretti. In physical il
core più lungo è 216 righe. Manifest e controlli in
`kernel/build/k2-pool-final-review.json`; whitespace preesistente nei file
utente fuori dalle modifiche di questo blocco conservato.

La review ha trovato anche le due letture dirette di Generation nel
renderer: sostituite con query scalare O(1) sincronizzata, provata con
alias vietato, contesto assente, progressione e letture concorrenti.
Il contatore rimane un tag diagnostico a 32 bit, non una prova di lifetime.

## Immagine finale

Dopo l’aggiunta di generation_read e la ripetizione delle suite interessate,
la campagna GOP finale passa 10/10 boot consecutivi con input e verifica
pixel; `qemu-run-e581da745cf245b08e05e2e0c319907a`, evidenze dettagliate
`pointer-interactive-check-64f89a9b736b478899dfa77df372c66a`.
Anche la sessione VirtIO/IOMMU finale passa 1/1:
`qemu-run-97ad12d43b5d4655b9ca51271118af43`, dettaglio
`pointer-interactive-check-875e081e1aa24498947ea4b5b9043bf9`.
Verificati pixel, input, traduzione DMA e cleanup con budget ripristinato.
Nessuna modifica ai sorgenti eseguibili dopo il manifest finale; hash delle
immagini nei results.json dei runner, corrispondenti agli ELF di campagna.

Esito: blocco allocatore pronto per la fase heap nel profilo BSP attuale;
nessuna attestazione di SMP guest o di intero K2 completato.
