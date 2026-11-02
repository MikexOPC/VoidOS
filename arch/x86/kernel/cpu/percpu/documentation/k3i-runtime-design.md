# K3-I — AP parcheggiati durante il boot ordinario

## Stato e scelta acquisita

Scelta acquisita il 2 novembre 2026: AP parcheggiati nel boot ordinario.
Implementazione completata; prove e limiti nel
[rapporto di validazione](k3i-runtime-validation.md).
Il diagnostico sequenziale e il panic del gruppo hanno prove dedicate.
Il boot ordinario ora avvia gli AP e continua sul BSP; i diagnostici
precedenti conservano il proprio percorso isolato.

Scelta approvata: collegare l'avvio sequenziale al boot ordinario supportato,
continuando sul BSP dopo PARKED verificato su ciascun AP. Alternativa discussa e non scelta:
introdurre prima un profilo esplicito di avvio AP, lasciando il predefinito
BSP finché non termina una seconda campagna di integrazione.

In entrambi i casi, gli AP restano con IF=0 e non eseguono task. La maschera
del backend panic indica core ammessi al protocollo di arresto; non deve
essere presentata come maschera di CPU disponibili allo scheduler.

## Ricognizione e riferimenti

- Il collegamento attuale è in `test/.../startup/commit/probe/`: include
  mutazioni intenzionali, verifiche di rifiuto e un arresto diagnostico.
  Non va trasferito integralmente nel percorso ordinario.
- Le primitive riutilizzabili esistono: installazione/readback, commit
  congiunto, pubblicazione immutabile, chiusura dei getter, trasporto e
  riarmo limitato alla pagina dati dopo PARKED.
- Il punto di integrazione esistente è `x86_ap_transition_start`, chiamato
  dal bootstrap dei dispositivi. Deve restituire soltanto dopo il risultato
  completo, senza lasciare owner sullo stack del chiamante.
- [Linux, documentazione CPU hotplug, CPU maps](https://docs.kernel.org/core-api/cpu_hotplug.html#cpu-maps)
  distingue CPU presenti, possibili e online per scheduler/interrupt.
  Qui non si adotta il suo hotplug: si mantiene esplicita la distinzione
  tra identità scoperta, AP parcheggiato e futura CPU schedulabile.
- [Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
  §9.4.4.1 per INIT/SIPI, §11.6.1 per ICR e §6.7.1 per NMI/IRET:
  si riusa il protocollo già verificato, senza nuovi registri o istruzioni.
  Resta il limite documentato degli NMI prima della IVT iniziale.

## Ownership, sequenza e fallimenti

Owner persistenti e privati del modulo per immagine, commit e trasporto;
nessun puntatore a dati automatici trattenuto dagli AP. Restano invariati
arena, guardie, lease pinned, root condivisa e record privati per core.
Il BSP avvia un AP alla volta, con IF=0 e nessun lock posseduto.

Sequenza non eseguibile:

```text
controllare profilo, identità, risorse e disponibilità della pagina bassa
preparare record AP1 e installare/verificare immagine e dati
sigillare gruppo e risorse con la barriera congiunta esistente
preparare panic; pubblicare il descrittore; chiudere gli accessi preparatori
per ciascun AP: avviare, verificare PARKED; riarmare solo per il successivo
verificare gruppo finale; pubblicare conteggi scoperti/parcheggiati
restituire al bootstrap BSP; proseguire dispositivi, scheduler BSP e console
```

Prima della barriera: rollback completo delle sole risorse ancora
rilasciabili; nessun tentativo di avvio se un prerequisito manca. I casi
senza ACPI o con un solo core mantengono il percorso BSP esplicitamente
identificato. La politica per risorse basse indisponibili deve conservare
il comportamento dichiarato del profilo, senza dichiarare AP avviati.

Dopo la barriera: errore terminale, risorse trattenute, nessun riuso e nessun
falso successo parziale. Si conserva la scelta già approvata di arresto
diagnostico; non si introduce recovery BSP dopo un tentativo AP fallito.
Un panic successivo deve poter raggiungere tutti gli AP realmente ammessi.

La preparazione panic è unica: evitare il vecchio percorso che effettua un
secondo commit o tenta di riaprire i getter dopo la pubblicazione. Nessun
cleanup del display o del BSP può rilasciare le risorse CPU trattenute.

## Perimetro proposto

Modulo `arch/x86/kernel/cpu/percpu/`, con gli adattatori indispensabili nel
bootstrap, nella build e nei test centralizzati. Eventuali incompatibilità
con l'inventario paging vanno prima identificate: nessuna attenuazione dei
controlli su ownership, alias o timeout. Scheduler AP, IRQ ordinari sugli
AP, hotplug e driver nuovi restano fuori da questo blocco.

## Criteri di accettazione

- Unit test dei passaggi, precondizioni, rollback e fallimenti dopo sigillo;
  nessun tentativo AP duplicato e nessuna ammissione simulata.
- Dieci boot dell'immagine finale con quattro vCPU: tre AP realmente
  PARKED nei registri mentre il BSP supera il punto prima terminale e
  raggiunge il normale marcatore finale.
- Dieci boot con una vCPU e regressioni dei diagnostici esistenti coinvolti.
- Console interattiva: tastiera, mouse, risposta shell e chiusura mentre
  gli AP mantengono contesti/stack/IST e risorse trattenute.
- Panic intenzionale dopo la continuazione BSP, con conferme di tutti
  gli AP ammessi; failure injection prima/dopo barriera separata dai crash.
- Compilazione con warning come errori, ASan host pertinente, accounting
  guest, analisi statica e revisione del diff. Coverage non misurata N/D.

Il solo blocco d'integrazione precede la chiusura K3. La campagna di stress
seguente è ora verificata nel [rapporto](k3i-runtime-validation.md#chiusura-k3);
restano i limiti hardware dichiarati.

## Chiusura K3: campagna di stress

Si riusa il protocollo approvato, con VM possedute e senza avviare task AP.
Dieci prove per caso complesso: NMI esterno a tutti i core dopo READY,
secondo NMI dopo l'arresto, ACK omesso intenzionalmente da un follower e
UART con DLAB alterato. Gli hook guest omettono l'ACK o trattengono un
vero ticket lock nel diagnostico, dove si prova anche il NMI ricorsivo;
non modifica il comportamento delle immagini ordinarie. Il reporter può
essere qualsiasi CPU: verificare il vincitore reale, tutti i record e i
registri, mai assumere l'origine dal comando `cpu` del monitor.

QEMU v11.1.0: [x86_nmi](https://github.com/qemu/qemu/blob/v11.1.0/hw/i386/x86.c#L139)
itera tutte le CPU e ignora l'indice del monitor, ma
[apic_external_nmi](https://github.com/qemu/qemu/blob/v11.1.0/hw/intc/apic.c#L193)
passa attraverso LINT1: il mask degli AP impedisce ingressi indipendenti.
Il caso esterno prova origine BSP e arresto remoto, non quattro origini.
Il caso conteso usa invece quattro IPI NMI fisiche esplicite, AP prima e
BSP ultimo, dopo bootstrap completo. Una barriera solo diagnostica, con
atomiche Rust e attesa massima di un secondo/un milione di controlli,
richiede quattro catture prima dell'elezione. Timeout o ingresso duplicato
fermano la prova; nessun reset del coordinatore o modifica ai LVT.
Intel SDM 078US §11.6.1, figura 11-12 e tabella 11-3: ICR high +310h con
ID[31:24], low +300h=4400h, NMI fisico, edge/assert e nessuno shorthand.
Non si usa lo shorthand all-including-self per NMI, che la tabella esclude.
[UART serial.c](https://github.com/qemu/qemu/blob/v11.1.0/hw/char/serial.c#L38):
LCR base COM1 +3, DLAB bit7. Il caso negativo conserva la configurazione
8N1 e imposta DLAB; la prova primaria diventa la RAM, non la seriale.
Questo riproduce una configurazione UART inaccettabile, non un guasto fisico.

ACK mancante: il follower successivo al reporter si arresta senza conferma;
il reporter deve terminare entro deadline/poll budget e conservare missing
e outcome nonzero. Non è una simulazione di SMM o di CPU fisica bloccata.
Controllare record completi, owner unico, risorse pinned e nessun riuso.
I test concorrenti host completano le prove di elezione e ammissione; la
consegna NMI broadcast non garantisce da sola una particolare interleaving.

Console GOP: una sessione di 300 secondi con input mouse bidirezionale
periodico, poi confronto pixel, Esc, shell e cleanup del collaudo esistente.
Dieci boot ordinari finali, suite host pertinenti, accounting e audit degli
oggetti completano la revisione. Gli scenari di fault atteso non sono
conteggiati come boot ordinari positivi. TSan, hardware fisico, task AP e
hotplug restano fuori dalle garanzie di questa milestone.
