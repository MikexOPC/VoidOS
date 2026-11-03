# K4-A — Interrupt di servizio sugli AP

Stato: contratto del 2 novembre 2026, approvato dall'utente per un diagnostico
separato. Protocollo RAM, attesa e collegamento hardware implementati;
le campagne applicabili sono riportate nel documento di validazione.
Il servizio accetta gruppi di 2–64 CPU; il limite del formato non equivale
a una prova hardware su 64 CPU. Le configurazioni collaudate sono elencate
nella validazione, con la relativa revisione delle immagini.
K3 è chiusa nel commit `9f434b7`: nel boot ordinario gli AP sono avviati,
ma restano parcheggiati con IF=0. Questo blocco prepara la comunicazione IPI;
non introduce ancora TLB shootdown, preemption o task sugli AP.

## Riscontro nel codice

- `admission/assembly/entry/bridge.asm`: nel boot ordinario termina in CLI/HLT;
  il diagnostico passa al servizio dopo l'ammissione e il ritorno privato.
- `admission/entry/entry.c`: verifica e carica la IDT condivisa delle 32 eccezioni.
- `startup/assembly/private/resume.asm`: verifica quel contratto prima di IRETQ.
- `interrupts/return/table.c`, rispetto ad `arch/x86/kernel/`: dispone già di
  IDT completa, ma dispatcher, ownership e contatori sono specifici del BSP.
- `interrupts/registry/registry.c`: tre subscription, vettori 0x40–0x42.
  0xf0 è il test software di ritorno; 0xff è lo spurious LAPIC.

Il dispatcher BSP non diventa SMP semplicemente caricandone la IDT sugli AP.

## Contratto proposto

Modulo: servizio IPI per-CPU sotto `arch/x86/kernel/cpu/percpu/`.
Adattatori necessari: entry/return e ammissione AP, IDT/vettori e accesso LAPIC,
bootstrap, build e test centralizzati corrispondenti. Nessuna modifica ai
driver input, alla politica dello scheduler o alle API dell'allocatore.

Primo profilo: diagnostico separato. Il boot ordinario mantiene il contratto
K3 fino al superamento dei test e alla successiva integrazione concordata.
Alternativa: abilitare subito il servizio nel boot ordinario, includendo
anche le regressioni della console e dei profili dipendenti nello stesso blocco.

### Risorse e stati

- Il gruppo CPU resta immutabile. Gli stati del servizio sono separati dai
  vecchi stati di startup: PREPARED → READY → FAILED, senza ritorno da FAILED.
- IDT completa del servizio preparata prima dell'attivazione, con gate delle
  eccezioni/NMI coerenti con il panic SMP e stack privati già posseduti.
- Ogni CPU carica la propria IDTR. La tabella del servizio può essere condivisa
  se identica e immutabile; nessun aggiornamento mentre viene usata.
- L'ingresso normale usa lo stack privato corrente; NMI e fault mantengono
  gli IST dedicati. Niente riuso dello stack NMI per gli IPI ordinari.
- Prima di READY: verificare contesto GS, stack disponibile, IDTR, ambiente
  SIMD, LAPIC locale abilitato, priorità e sorgenti locali mascherate.
- Solo dopo queste verifiche l'AP pubblica READY e può attendere con IF=1.
  Il ciclo prepara a IF=0 lo stato da controllare al risveglio e usa
  STI/HLT contigui per dormire; ogni richiesta pubblicata è seguita da un IPI.
  Attendere senza lavoro è normale; ogni richiesta ha invece attesa limitata.

### Richieste e sincronizzazione

Prima operazione: PING, senza callback o puntatori a funzioni arbitrari.
Un mailbox per destinatario, memoria permanente e massimo una richiesta
in volo per mailbox. Il solo BSP pubblica; ciascun AP consuma il proprio.
Generazione a 64 bit senza wrap: release alla pubblicazione, acquire alla
lettura; ACK della stessa generazione pubblicato solo dopo completamento
ed EOI. Il mittente acquisisce l'ACK prima di riutilizzare il mailbox.
L'IPI è una notifica: il numero di interrupt non conta le richieste.

Pseudocodice non eseguibile:

```text
BSP: verifica READY e mailbox libero → pubblica PING(g) → invia IPI
AP:  salva contesto → acquisisce richiesta → valida g → completa PING
     → EOI → pubblica ACK(g) → ripristina contesto → IRETQ
BSP: acquisisce ACK(g), oppure termina alla deadline/limite di controlli
```

L'handler non acquisisce lock, non alloca, non scrive log e non disegna.
Salva GPR e stato SIMD secondo l'ABI corrente prima del C; Rust verifica
stati/generazioni, C gestisce ownership/MMIO, Assembly gestisce entry/return.
Le operazioni MMIO seguono l'ordering xAPIC documentato, distinto dalle
atomiche della RAM. NMI resta ricevibile e mantiene il percorso di emergenza.
Le scritture ICR normali non possono intercalarsi con un invio ordinario
sulla stessa CPU; un NMI di panic interrompe definitivamente tale percorso.

Timeout sorvegliati dal clock ACPI PM già verificato e da un limite di poll.
Valori e budget vanno dichiarati nel diagnostico prima della campagna.
Clock fermo, ACK incoerente o timeout portano ad arresto diagnostico e
risorse trattenute. Nessun riuso dopo un timeout; nessun rilascio dipendente
dalla sola consegna ICR. Token stantii e generazioni esaurite sono rifiutati.

## Hardware e riferimenti

Fonte: [Intel SDM 253668-078US, Vol. 3A](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
§6.14 (entry a 64 bit), §11.6.1–11.6.3 (ICR e destinazione),
§11.8.3–11.8.5 (priorità, accettazione ed EOI), §11.9 (spurious).
Profilo iniziale: QEMU/OVMF xAPIC, non x2APIC o hardware fisico certificato.

| Registro relativo alla base LAPIC verificata | Campo | Motivo |
|---|---|---|
| ICR high +0x310 | destinazione fisica 31:24 | Unicast all'identità AP verificata |
| ICR low +0x300 | vettore 7:0; delivery 10:8=000; destination 11=0; trigger 15=0; shorthand 19:18=00 | IPI fixed, fisico ed edge |
| ICR low +0x300 | delivery status 12 | Attesa limitata della disponibilità ICR, distinta dall'ACK software |
| EOI +0xb0 | scrittura zero | Completa il servizio fixed prima del ritorno |
| SVR +0xf0 | enable 8; vettore 7:0 | Abilitazione locale e spurious valido senza EOI |

Il vettore di servizio verrà riservato esplicitamente nel registro dei
vettori, senza collisioni con eccezioni, 0x40–0x42, 0xf0 e 0xff.
Nessun offset nuovo viene aggiunto all'allowlist del trasporto di panic.

Confronto studiato: [Linux v6.12, arch/x86/kernel/smp.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/arch/x86/kernel/smp.c),
`sysvec_call_function*`: ingresso IPI, EOI e servizio delle richieste separati.
È un riferimento versionato, non codice copiato: VoidOS limita produttore,
operazioni e concorrenza per rendere verificabile il primo percorso.

## Accettazione e seguito

- Host C/Rust: stati, generazioni, destinatari, duplicati, timeout, clock
  fermo, overflow, ordering e ownership; warning come errori e sanitizer applicabili.
- QEMU: 10 boot consecutivi del diagnostico su quattro CPU realmente avviate;
  PING ripetuti ad ogni AP, ACK distinti e progresso verificato.
- Controllo GPR/SIMD, GS, stack/guardie, IDTR, IF e ISR dopo EOI;
  spurious senza EOI e nessuna scrittura nei metadati di startup sigillati.
- Negativi: richiesta non valida e ACK mancante con arresto previsto;
  panic NMI mentre gli AP attendono o gestiscono il servizio.
- Regressioni K3 su una e quattro CPU; accounting e risorse pinned invariati.
- Dopo K4-A: integrazione ordinaria, quindi shootdown K4-B con protocollo
  dedicato; nessuna garanzia TLB deriva dal semplice successo del PING.

Il design non sostituisce le evidenze: i test hardware elencati sopra
restano criteri da soddisfare dopo il collegamento del servizio al kernel.

## Primo incremento: protocollo RAM

Implementato in `service/`, rispetto a `percpu/`: stato atomico Rust senza
allocazioni, header C e attesa sorvegliata C. La generazione zero significa
assenza di lavoro, UINT64_MAX errore: l'ultima generazione valida è 2^64-2.
Nessun reset del servizio; dopo FAILED le risorse restano trattenute.
Un ACK concorrente al timeout può ancora essere pubblicato, ma FAILED
resta terminale e vieta ogni richiesta successiva.

Gli indici caller sono un contratto interno, non credenziali: il futuro
adattatore C dovrà derivarli dal contesto CPU verificato. Analogamente,
`xis_complete` va chiamata solo dopo il vero servizio ed EOI; i test host
non dimostrano né l'identità hardware né la consegna di un interrupt.
Le query sono letture singole, non uno snapshot atomico di tutti i campi.
L'attesa ha un solo chiamante BSP; non ammette pubblicazioni concorrenti
sullo stesso mailbox mentre il BSP attende la conferma.

Il primo collegamento era limitato al profilo diagnostico. Evidenze e limiti in
[k4a-validation.md](k4a-validation.md).

## Collegamento hardware del diagnostico

Il flag di build `-TestApService` accetta `ping`, `missing-ack`, `stale-ack`,
`panic-idle` e `panic-handler`. È incompatibile con altri profili di prova.
Ricava il numero di CPU dal gruppo avviato, lo verifica contro quello del
panic e accetta 2–64 CPU. La sonda e le iniezioni sono esclusive
di questi profili.

- Registro privato del servizio: vettore fixed **0xe1**, spurious **0xff**.
  La IDT completa copia i 32 gate fault/NMI già verificati; gli altri ingressi
  sono terminali se non assegnati al servizio. Il registro IRQ BSP resta separato.
- Destinazione ICR high `ApicId << 24`, low **0x40e1**: fixed, fisico,
  assert, edge, senza shorthand. Mapping UC permanente già posseduto da K3.
- Ogni AP verifica GS, CR3, stack, capacità FXSAVE e IDTR, maschera i LVT
  presenti secondo il registro versione, azzera TPR e imposta SVR=0x1ff.
  Nessuna rotta I/O APIC viene spostata dal BSP.
- Stato condiviso residente: mailbox Rust e `XIS_NATIVE`, senza allocazioni.
  Il primo occupa circa 4 KiB, il secondo 8.256 byte; nessun
  oggetto viene rilasciato dopo l'ammissione degli AP.
- L'handler verifica il solo bit ISR del vettore, consuma la richiesta,
  scrive EOI, ne controlla l'effetto e pubblica l'ACK. Il BSP attende anche
  la verifica dei registri effettuata dall'AP dopo IRETQ.
- Tutte le attese di richiesta/ICR hanno deadline ACPI PM di un secondo
  e massimo 1.000.000 di controlli. L'idle senza lavoro usa STI/HLT.

Il ciclo diagnostico controlla 15 GPR con valori distinti, DF, XMM0–15,
MXCSR, controllo/tag/status x87 e ST0=1. L'ingresso altera appositamente
x87 e XMM dopo FXSAVE64, prima di FXRSTOR64. Fonte formato:
[Intel SDM 253666-089US, Vol. 2A, FXSAVE, tabella 3-48](https://cdrdv2-public.intel.com/868140/253666-089-sdm-vol-2a.pdf).

`stale-ack` è il nome del profilo che inietta un ACK di generazione errata
(g+1); il rifiuto di ACK precedenti è coperto anche dai test Rust.
`missing-ack` omette soltanto la conferma dopo EOI; gli altri due profili
provocano il panic coordinato dal BSP con AP in idle o AP1 dentro l'handler.
L'IDT è immutabile tramite queste API, ma resta nei dati RW del kernel:
questo blocco non introduce una nuova protezione PTE in sola lettura.

## Preparazione K4 e correzioni della review

Le formule C per stack, frame, IST e metadati usano `resources/geometry.h`.
Rust e Assembly mantengono i propri vincoli di layout: gli assert C non
sostituiscono i test che ne verificano l'accordo.

`interrupts/apic/icr/x86_icr_send` centralizza l'ordine destinazione→comando.
Accessi ai registri, budget, validazione del comando e serializzazione locale
restano responsabilità del chiamante. Wake e servizio usano ESR e attesa
post-invio; il panic conserva NMI senza ESR né attesa post-invio e controlla
le conferme software. L'helper fa parte dell'audit di emergenza.

Il runner passa il numero di CPU anche all'attesa dello stop coordinato,
mantenendo quattro come default per i chiamanti storici. La raccolta dei
registri dopo un errore usa lo stesso numero; se fallisce, aggiunge una nota
senza sostituire l'errore che ha provocato il fallimento del test.

## Integrazione nel boot ordinario

Riutilizzare la IDT del servizio, la mailbox e la verifica EOI già
collaudate. Pubblicare il servizio prima di avviare il primo AP, dopo
la preparazione del panic coordinato. Con una sola CPU il servizio
resta inattivo. Ogni AP installa la IDT del servizio dopo l'ammissione
privata, verifica il LAPIC e pubblica READY prima dell'attesa STI/HLT.

Nel profilo ordinario l'ingresso conserva e ripristina il contesto.
Il ciclo con registri sentinella e l'iniezione di errori appartengono
ai profili di prova. Driver e scheduler restano sul BSP. Gli AP
accedono solo a stack, contesto, dati del servizio e mapping LAPIC
stabili. Le modifiche alle traduzioni condivise usate dagli AP
richiedono il protocollo di invalidazione K4-B.

Decisione approvata: il BSP invia un PING per AP nel boot ordinario
prima di proseguire. Il PING
prova consegna e ACK dopo EOI; non prova da solo il ritorno IRETQ,
che resta oggetto del diagnostico. Ogni attesa usa il budget esistente.
Un errore dopo l'avvio degli AP attiva il panic coordinato.

Accettazione: suite host e audit di emergenza, dieci boot per
configurazione ordinaria a 1, 2, 4 e 8 CPU, regressione dei profili
IPI e panic, e console operativa con gli AP in attesa.
