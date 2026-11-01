# K3-I — ritorno IRETQ e panic dopo il sigillo, 1 novembre 2026

Due avanzamenti nello stesso perimetro ingresso/per-CPU: piano C/Rust e
helper Assembly per l'ultimo IRETQ, più preparazione del panic sulle risorse
già sigillate. Il primo è collaudato su CPU isolata, il secondo nel kernel
UEFI. Non sono ancora collegati all'ammissione runtime di un AP.

## Piano e helper del ritorno

`XS_RETURN_PLAN` è un ABI di 64 byte, memorizzato a offset 736 nel frame
privato già esistente di 1024 byte. Il builder C verifica allineamento e
assenza di wrap del puntatore output e usa il validator Rust safe: binding a token, root,
area locale e frame; destinazione e operando IDTR canonici, senza overflow
o alias con stack, descrittori, area locale o fra loro. Nessuna lettura della
memoria puntata dall'IDTR viene eseguita da Rust. È validazione numerica,
non prova di mapping, ownership o inizializzazione di un handler.

La continuazione C futura dovrà aver già validato il contesto, installato
il panic e pubblicato ACTIVE. Il nuovo simbolo `xs_private_resume` è un
indirizzo Assembly, non una funzione C ordinaria. Ripete i binding critici,
CR3, base GS/self/indice/ACTIVE, GDT/TR e TSS busy, RSP0 e tre IST secondo
la geometria AP. Rifiuta un contesto ancora PREPARED prima di sbloccare NMI.

Controlla inoltre limite IDTR, allineamento/base della tabella, selector,
tipo, bit riservati, indirizzi canonici e IST di tutte le 32 gate: IST1 per
le eccezioni ordinarie, IST2 per NMI e IST3 per double fault. L'IDT deve essere
già stabile e autorizzata dal chiamante; i controlli non autenticando codice
kernel arbitrario non sostituiscono il preflight nativo ancora mancante.

Solo dopo carica la nuova IDT e costruisce il frame IRETQ: SS=0x10, RSP al
frame alto, RFLAGS=2, CS=8 e destinazione alta. IF, DF, IOPL e NT restano
azzerati. RCX/R8 identificano il frame privato, RDX il token, R15 conserva
il frame. La destinazione è ancora un ingresso Assembly, senza return address
o shadow space C. Il frame non può essere rilasciato finché viene usato.

Prima di LIDT/IRET restano attivi gli handler terminali provvisori; dopo,
deve essere valido il percorso finale anche se IRET stesso genera un fault.
Nessuna nuova allocazione o modifica al formato del record basso.

## Fonti hardware verificate

[Intel SDM Vol.3A 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§6.7.1, sblocco NMI anche su IRET faulting; §§6.14.1 e 6.14.3–5,
gate, frame e IST in IA-32e; §§2.4 e 8.2.4/8.7, GDTR/TR/TSS;
§3.4.4 e Vol.4 335592-078US tabella 2-2 per IA32_GS_BASE C0000101h.
Le coordinate del frame e i campi per-CPU sono ABI software verificati da
static assert C, non offset hardware ricavati dal manuale.

## Prove del ritorno: host e CPU isolata

- Host finale: `kernel/build/k3i-return-host-final/` e relativo `.log`.
  Quattro casi Rust in debug/release: binding, alias, limiti canonici e
  overflow, record invalido senza panic aritmetico. C/Rust con ASan sul C,
  ABI e output invariato sugli errori. Cppcheck senza diagnostiche bloccanti.
- Il probe C freestanding /GS- /Zl ha una sola dipendenza prevista,
  xs_return_valid; nessuna dipendenza runtime aggiuntiva. Oggetti Assembly
  COFF e link alto /NODEFAULTLIB /WX verificati, disassemblaggio conservato.
- CPU finale: `kernel/build/ap-return-cpu-0471914cd9c3426b97e079cbd93a59f3/`,
  17/17 casi: dieci consecutivi con secondo NMI pendente, alternando due
  posizioni del blob; un controllo senza NMI pendente e sei negativi.

I negativi rifiutano token, gate IST e TSS alterati o contesto non ACTIVE
prima del ritorno. La destinazione NX produce #PF sull'IST1. Per il caso
IRET faulting, il debugger si ferma sull'istruzione reale e altera il RIP
nel suo operando a non canonico: #GP sull'IST1, con NMI pendente comunque
consegnato sull'IST2. Le prove richiedono trace, contatore e indirizzi di
stack attesi. Un altro NMI dopo il ritorno verifica che il blocco sia cessato.

La fixture include lo stesso codice Assembly del prodotto. Usa il piano
generato dal C/Rust reale e pacchetti del builder già verificato. Hash del
piano host finale identico a quello della campagna CPU, non solo stessi
parametri dichiarati. Verificati root operativa con metà bassa vuota,
record copiato, codice basso invariato e IDT finale attiva.

È una CPU BSP SeaBIOS/TCG, senza INIT/SIPI e senza kernel UEFI. L'harness
predispone memoria, IDT, handler diagnostici e intestazione GS ACTIVE: non
esegue la promozione per-CPU o il panic nativo. Non valida Locks/clock del
contesto reale, SMP, delivery LAPIC o l'entry del kernel dopo INIT. I suoi
contatori GS sono solo dati della fixture, non campi runtime del kernel.

## Preparazione panic dopo il commit congiunto

Il vecchio `xp_panic_native_prepare` effettuava un proprio commit delle
risorse private. Ora, prima di inizializzare i record, riconosce un gruppo
già committed e trattenuto dal registro congiunto, ricontrollandolo tramite
`x86_ap_resources_joint_ready`. In quel caso non ripete il commit; il percorso
precedente resta per i profili che preparano il panic senza il nuovo gruppo.

Il getter è un preflight prima di scrivere i record: usa la verifica dei
contenuti inizialmente zero. Non è un test di salute da richiamare dopo
l'uso dell'arena. Sigillare il lifetime dell'arena non rende i suoi dati
immutabili: inizializzare i record panic nelle pagine pinned è previsto.
Il payload basso sigillato viene comunque ricontrollato con xb_sealed.

Il nuovo profilo `joint-panic` dimostra ordine SEALED -> PANIC_PREPARED,
record privati inizializzati, IDT/IST AP predisposti e maschera di ammissione
pari al solo BSP. Non dichiara gli AP online e non installa quella IDT su AP
in esecuzione. Il rollback interno dei record non rilascia le risorse del
gruppo; il chiamante di bootstrap tratta il fallimento come terminale.

Prove finali di questo collegamento:

- `kernel/build/ap-joint-firmware-5f19e1cb4f454812b3bf2633513d0d7d/`, 10/10
  UEFI consecutivi, immagine `kernel/build/k3i-return-panic-v1/kernel.elf`.
- Regressione precedente preparazione panic:
  `kernel/build/panic-firmware-80a01520004d4361886e01a0cb2d7820/`, 10/10,
  immagine `kernel/build/k3i-return-panic-legacy/kernel.elf`, quattro vCPU
  configurate ma solo BSP ammesso. Le immagini non avviano AP.
- Regressioni host panic: `kernel/build/k3i-return-panic-host/`; protocollo
  startup/record C/Rust: `kernel/build/k3i-return-protocol-regression/`,
  16 casi in debug/release e due eseguibili C/Rust. ASan C e cppcheck passati.
- Analisi statica native/probe: `kernel/build/k3i-return-static/`, senza
  errori bloccanti; style/information non equivalgono a report vuoto.

Campagna ordinaria finale: `kernel/build/percpu-firmware-a67292590bf94ec0abb29bad6d2a1261/`,
10/10 boot con `kernel/build/k3i-return-ordinary/kernel.elf`, alternando
uno/quattro vCPU e 128/256 MiB. Totale finale 30/30 UEFI tra nuovo panic,
panic precedente e ordinario, distinti dai 17 esperimenti CPU del ritorno.
Manifest finale: `kernel/build/k3i-return-manifest.json`. Campagna riproducibile nuova:
percpu-joint-panic in scripts/test/campaigns.json.

## Debug e lavoro ancora aperto

La prima fixture usava un salto che NASM accorciava: gli indirizzi nell'header
erano letti all'offset sbagliato. Il controllo degli handler ha rifiutato il
valore non canonico prima di IRET. Corretto con salto strict e layout fisso.
L'avviso NASM sui tre indirizzi assoluti dell'header flat è limitato a quei
campi della fixture; il prodotto conserva il controllo dei warning.

Restano l'ammissione C degli AP, l'accessor contesto/clock non più BSP-only,
il collegamento xs_private_arrive -> validazione nativa -> piano -> resume,
la pubblicazione Rust PRIVATE/PARKED e l'adattatore INIT/SIPI/NMI reale.
Non è ancora un ingresso AP completo. Nessun AP avviato o prova su hardware
fisico, Valgrind, coverage e leak detector guest. ASan non strumenta Assembly
né Rust; i test di ritorno non allocano pagine fisiche del kernel.

Il collegamento C dovrà caricare e validare l'IDT finale prima di chiamare
xp_panic_native_admit, che ne verifica già il valore. L'helper può ricaricare
lo stesso IDTR prima di IRET: non bisogna attendere quel LIDT per tentare
l'ammissione, né considerare il solo campo ACTIVE una prova sufficiente.
