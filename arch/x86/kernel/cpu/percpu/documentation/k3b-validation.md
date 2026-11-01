# K3-B — validazione del clock dei lock BSP

29 ottobre 2026. Scelta utente: timer ACPI PM esistente; AP fermi.
[Contratto e fonte primaria](k3b-design.md).

## Risultato funzionale

Il bootstrap cattura il timer dalla FADT tramite il lettore esistente e
collega il contesto GS solo dopo aver verificato la finestra ACPI inattiva.
Il modulo copia porta, maschera e metadati; nessun puntatore alla struttura
temporanea rimane nel callback. La lettura usa solo la copia persistente,
la validazione Rust della porta e IN a 32 bit: niente lock, allocazioni,
GS o dipendenza dagli IRQ. Prima della pubblicazione verifica avanzamento
con IF=0. Un errore ritira lo slot preparato; il bind riuscito è unico.

L’ABI GS di K3-A resta invariata. La tabella dei clock occupa 4 KiB BSS
persistenti (64 slot da 64 byte); solo il BSP è collegato. Nessuna nuova
allocazione, risorsa DMA, mapping o sorgente IRQ. Questo non dimostra SMP.

Policy di bring-up: 357.955 tick (100 ms arrotondati per eccesso), al più
1.000.000 controlli; il primo limite termina l’attesa. Probe iniziale
limitato a 4.096 controlli. Non sono misure di prestazioni né timeout
universali per i driver. Il backend supporta attualmente PM_TMR I/O; il
fallback a TSC/HPET/MMIO non è implementato. Con ACPI assente resta BSP
non conteso; con ACPI presente ma sorgente non valida l’init termina.

## Host finale

Nuova suite percpu-clock: **50/50 esecuzioni**, dieci ripetizioni di cinque
modalità: 24 bit, 32 bit, IF0, scadenza e contatore fermo. Evidenze:
`kernel/build/test-run-9f7d17942d894f71bfa143641d121e71/percpu-clock.log`.

Usa provider GS, primitiva lock e clock comuni reali; I/O, IF e MSR sono
modellati. Verifica sorgenti invalide/assenti, alias, doppio bind, contesto
occupato, CPU non ammessa, rollback dopo probe fermo/salto ambiguo, copia
indipendente dalla sorgente originale, wrap 24/32 bit, IF conservato,
corruzione di Source/Wait, lettura fallita e distacco/ripristino diagnostico.
Nei due negativi il ticket prenotato resta contabilizzato e il percorso
termina con LOCK_WAIT_FAILED: non viene abbandonato. longjmp solo host.

MSVC /W4 /WX, C strumentato con ASan e validatori/atomiche Rust reali.
Regressioni passate, senza affermare che equivalgano a tutta la suite OS:

| Suite | Evidenza in kernel/build/ |
|---|---|
| percpu (50 esecuzioni K3-A) | test-run-3d290516085c45b7b10516cefb60cd62 |
| sync | test-run-9f4399eef94e4d6d82fbcc03894841fd |
| clock | test-run-aa7ad9f112df4a1a9a8514e0921d39d2 |
| bootstrap | test-run-8e55ce489f16413f94ff785cce0df95b |

Analisi statica di tutti e tre i nuovi C di produzione:
`kernel/build/k3b-static/cppcheck.xml`. Zero error/warning/performance/
portability. Restano tre suggerimenti style: due constParameterCallback
non applicati perché la firma callback preesistente è void*, e un invito
ad usare memset al posto del ciclo di azzeramento freestanding. Undici
informazioni missingIncludeSystem non sono diagnostiche del compilatore;
la compilazione MSVC usa gli header effettivi. Nessuna soppressione globale.

## QEMU finale

QEMU 11.1.0, q35, TCG, OVMF; alternanza 128/256 MiB e 1/4 vCPU configurate.
Una sola CPU OS attiva, BSP. I runner verificano immagine/copie ESP,
deadline, marker e cleanup della propria VM; nessun PASS dal silenzio.

| Profilo | Risultato | Evidenza in kernel/build/ |
|---|---|---|
| Diagnostico con lock probe | 10/10 consecutivi | percpu-firmware-ae4f1cf1547142e9b083ba0bbd5ea2c8/results.json |
| Ordinario senza probe sync | 10/10 consecutivi | percpu-firmware-9d1541a2fccd49f58f165b91a4f7ba35/results.json |
| Timeout con Wait PM reale e IRQ attivi illegalmente al release | 2/2 negativi attesi | qemu-run-bf80adb8eea04616a8505921bd0e2412/results.json |
| Console VirtIO/VT-d | 1/1 PASS | qemu-run-f982587b2b994ebfa892039c231213ef/results.json |

ELF diagnostico SHA-256:
`2B22C36AC44E9D77A5DB770623485E447C10E5E78FA53E292AE39378BFAA93B2`.
ELF ordinario SHA-256:
`E73159A1BBE67D4F91B2A47E14D6216C10EC14FAC9A26213A4A3EB9E28128989`.
Loader SHA-256 comune:
`B6C055AB17CC5CA1E0F017A35E4E9722273EB7B49CB0191F5E759CA84CC23AA1`.

Dopo KERNEL_READY il monitor legge il registro GS e la memoria guest:
owner ACTIVE, Self e contesto, vecchi 416 byte azzerati, NextToken conservato,
nessun guard residuo, Wait e Source dentro l’immagine kernel, callback nel
kernel, porta/maschera FADT, frequenza, tick e limite dei controlli coerenti
con il report. Verifica anche heap e le due revoche della metà bassa.

Nel negativo wait il probe assegna al waiter il descrittore PM runtime,
segnala KERNEL_PERCPU_WAIT_TIMEOUT_ARMED e verifica LOCK_WAIT_FAILED senza
raggiungere KERNEL_READY. Non è una prova di contesa fra core: il precedente
ticket è trattenuto intenzionalmente nello stesso BSP. Il test non misura
quale dei due limiti temporale/poll scatti per primo sulla macchina host.

La regressione video conserva anche screenshot e risultati di input,
movimento, pixel e rilascio delle risorse in
`kernel/build/pointer-interactive-check-7cf4efe402164fa89613752310f19079/`.
È una sessione video completa, non dieci sessioni video.

## Review e limiti

Manifest: `kernel/build/k3b-source-review.json`, massimo 128 righe nei
sorgenti/test del modulo, massimo cinque sorgenti diretti per cartella.
Diff verificato senza riformattare modifiche utente estranee. Nessun codice
del vecchio OS modificato. Il test sync ripristina il binding dopo le sue
prove temporanee; non lascia il kernel ordinario senza clock.

ASan riguarda il C host, non l’IN privilegiato o l’intero heap fisico.
Valgrind non eseguito su Windows; coverage linee/branch N/D. Nessuna prova
su hardware reale. La lettura PM non può diagnosticare da sola ogni guasto
firmware o un’intercettazione SMM: il limite finito di controlli rimane.

K3-B non autorizza ancora l’uso dei servizi da AP: mancano identità,
contesti completi, risorse e protocollo di ingresso/avvio/pubblicazione AP.
NMI/fault non devono usare questo accessor. Mapper e lifecycle restano BSP.
