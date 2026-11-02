# K4-A — Interrupt di servizio sugli AP

Stato: proposta del 2 novembre 2026, da discutere prima del codice.
K3 è chiusa nel commit `9f434b7`: gli AP sono avviati, ma restano
parcheggiati con IF=0. Questo blocco prepara la comunicazione IPI;
non introduce ancora TLB shootdown, preemption o task sugli AP.

## Riscontro nel codice

- `admission/assembly/entry/bridge.asm`: dopo l'ammissione termina in CLI/HLT.
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
  Il ciclo verifica il lavoro a IF=0 e usa STI/HLT contigui per dormire.
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

Questo documento non è evidenza di implementazione o di test runtime.
Per la presente proposta sono applicabili solo controlli di fonti, link e diff.
