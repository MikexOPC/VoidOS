# Inventario RAM/MMIO del bootstrap

L'inventario ordinario continua a rifiutare le foglie UC. Le due API
`x86_paging_inventory_capture_lapic` e `x86_paging_inventory_verify_lapic`
ammettono esclusivamente il mapping LAPIC dedicato, presentato tramite
un owner live esplicito. Non sono un ioremap generale.

## Contratto

La struttura occupa 3144 byte: mantiene il prefisso precedente di 3096
byte e aggiunge due contatori u64 e un record dispositivo di 32 byte.
`LeafPages = RamPages + MmioPages`; il percorso misto richiede esattamente
una foglia MMIO. RamPages conta le foglie ordinarie accettate dal contratto
di paging, non la RAM libera o i frame posseduti dall'allocatore.

Il record conserva indirizzo virtuale, pagina fisica, flag cache e
indirizzo dell'owner. Il verificatore riceve l'owner dal chiamante: non
dereferenzia un puntatore preso dai metadati dello snapshot.

Prima e dopo la scansione vengono verificati Self, Ready, CR3, guardie,
PTE, MAXPHYADDR, MSR APIC_BASE e PAT corrente. Si accetta solo BSP xAPIC,
IF=0, PAT[0]=WB e tipo UC all'indice selezionato da PWT/PCD/PAT.
La PTE LAPIC deve essere supervisor, RW, NX, senza altri flag; soltanto
Accessed e Dirty possono cambiare senza invalidare lo snapshot.

Rust ricostruisce l'indirizzo virtuale dalla vista ricorsiva della PT e
classifica le foglie. La pagina LAPIC e valida solo allo slot previsto;
qualsiasi alias fisico altrove viene rifiutato, anche WB. Restano vietati
alias delle pagine che contengono le tabelle di paging.

La pagina dispositivo non puo appartenere a RAM libera o lease live.
Se il firmware la descrive, deve essere interamente riservata/MMIO e
compatibile con UC. L'omissione dalla mappa e accettata solo grazie
all'identificazione corrente tramite APIC_BASE. Non si alloca o libera
la pagina fisica di un dispositivo.

## Pubblicazione e durata

La cattura costruisce una bozza locale e modifica l'output solo dopo
la scansione completa e la seconda verifica hardware. Un errore conserva
lo snapshot precedente. Le API verificano output disgiunto dagli input,
dall'owner, dalla vista ricorsiva e dalla regione LAPIC con le sue guardie.

La prova COM1 cattura l'inventario dopo il bind, prima di abilitare IER,
e lo verifica dopo aver chiuso UART e rotta. A KERNEL_READY il mapping
resta presente e IF resta zero. Il dispositivo supporta anche il primo
trasferimento TX delimitato; non implica IRQ permanenti o ricezione RX.
Il setup I/O APIC usa ancora la finestra temporanea, con IF=0.

## Validazione e limiti

Host: metadati e PTE alterati bit per bit, alias WB, UC non autorizzata,
riserve firmware incompatibili, collisione con RAM libera, owner
sovrapposto all'output e fallimento dell'autorita dopo la scansione.
Il modello APIC verifica anche la perdita di UC nel PAT corrente.
QEMU confronta owner, PTE, guardie e footer con il monitor indipendente.

Il contratto e UP e riguarda una sola pagina LAPIC. Nessun supporto SMP,
shootdown TLB, dispositivi generici o hardware fisico e attestato.

[Intel SDM Vol. 3A: paging, PAT e alias di memoria](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf).
