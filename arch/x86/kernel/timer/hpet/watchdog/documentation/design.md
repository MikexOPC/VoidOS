# R-001 — watchdog indipendente HPET, versione iniziale

Implementazione del 7 ottobre 2026. Scelta dell'utente: seconda sorgente hardware
e HLT. Il componente protegge le attese LAPIC finite e il ciclo continuo del BSP.
Non implementa scheduler, SMP, watchdog NMI o recovery dopo guasto del controller.

## Contratto e profilo ammesso

Il caller e ring 0/BSP, IF=0; GDT/IDT, backend IRQ Assembly e registry sono gia
validi. Context, pool, page window, inventory, mapping LAPIC, topology, profile,
capabilities e descrizione HPET sono oggetti kernel fidati, persistenti e
immutabili per la durata della sessione. Env viene copiato; non si conserva il
puntatore alla struttura temporanea del caller. Il source contiene Count/Error
adiacenti e resta vivo per tutta l'attesa. StopRequest e opzionale e persistente.
I controlli numerici/alias non sono una API user-copy o recupero da page fault.

La versione iniziale accetta un IOAPIC base GSI zero con 24 input, ID firmware e
hardware coerenti, destinazione fisica BSP <=15, HPET con ID/periodo verificati,
counter globale fermo, legacy replacement disabilitato, nessun pending e nessun
comparator enabled/periodic/value-set/FSB. Si cercano comparator con route 16..23
e pin masked, senza delivery/remote IRR, non occupati da ISO o NMI MADT.
Se il candidato non ha un pin libero, si prova il successivo senza modificarlo.
Errori MMIO o scritture parziali interrompono la ricerca e avviano il rollback.
La ricerca resta limitata ai timer annunciati, al massimo 32. Vedere il
[rapporto di compatibilita](compatibility.md) per prove e limiti del nuovo percorso.
La [diagnostica delle fasi fallite](failures.md) conserva separatamente il primo
guasto operativo e il primo errore di cleanup, senza modificare ownership.
La [politica di recovery](recovery.md) limita e verifica i retry del cleanup;
non riavvia i timer dopo un guasto.

Non si assume il base HPET o IOAPIC di QEMU nel codice di produzione. Gli indirizzi
provengono da ACPI e capabilities. La validazione esclude RAM, lease free/live,
pagine delle tabelle e alias esistenti; verifica l'inventory con il LAPIC live.
La page window presta una pagina UC, supervisor, NX. PTE e TLB vengono revocati
prima di HLT e dopo ogni callback. Budget fisico invariato: nessuna allocazione
nuova nel watchdog. La protezione delle copie utente e SMP restano fuori profilo.

## Ownership e transizioni

```text
UNOWNED -> capture halted HPET -> claim masked IOAPIC pin
        -> configure level one-shot, enable/prove counter progress
        -> bind vector 0x40 -> unmask pin -> READY
READY   -> program target -> enable IRQ -> verify arm margin -> ARMED
ARMED   -> existing STI/HLT/CLI backend, on independent count
IRQ     -> validate status/deadline -> disable comparator -> W1C own status
        -> LAPIC EOI 0x40 -> increment watchdog Count -> READY
READY   -> classify LAPIC Count/Error/Stop -> EVENT / TIMEOUT / STOP / FAULT
CLOSE   -> mask route -> prove quiescence/unbind -> halt/restore HPET
        -> restore IOAPIC entry -> verify window/inventory/ISR/IRR -> CLOSED
```

Il vettore 0x40 viene riutilizzato soltanto dopo che UART ha rimosso la propria
subscription. Un vettore ancora occupato fa fallire bind; non si scavalca il
registry. LAPIC usa 0x41. Gli owner e i callback sono statici; nessun handler
conserva stack, buffer temporanei o pointer a un oggetto liberato.

La deadline HPET e 100 ms per questo profilo. Rust converte fs -> ticks con
ceil e limite inferiore a mezzo range 32 bit. Dal fix del9 ottobre il counter
posseduto viene fermato durante l'armamento; il comparatore e verificato e
abilitato prima di riavviarlo. Il controllo del margine avviene mentre fermo,
non dopo il riavvio: un IRQ level gia pendente resta da consumare, senza
riclassificarlo come errore di programmazione. Il counter fermo deve restare
identico a Start; un avanzamento non conforme blocca l'armamento. Counter
progresso e verificato entro 2000 letture, senza interpretarle come millisecondi.
Il handler accetta soltanto il pending del comparator posseduto e un delta
>=Ticks, <2^31. La politica Rust distingue errore, stop, evento e timeout;
un errore non diventa stop riuscito.

Si attende l'acknowledgement HPET anche quando LAPIC arriva prima. Questo evita
di cancellare una sorgente con IRQ gia accettato/in volo. Costo deliberato v0:
la consegna al caller e circa 100 ms anche per LAPIC calibrato a circa 10 ms;
il ciclo non offre quindi scheduling a 100 Hz. In futuro serve un protocollo
di cancellazione/drain prima di anticipare il ritorno o condividere HPET.

Il counter globale non viene azzerato all'avvio. Si acquisisce soltanto il blocco
fermato senza altre sorgenti attive e si salvano counter/config/comparator.
Al termine si ferma il counter, si ripristina la size originale del comparator,
poi comparator e counter completo; ogni scrittura e confermata tramite readback.
Il selettore IOAPIC viene ripristinato in ciascun prestito.

## Failure e disponibilita

IRQ LAPIC mancante con HPET, IOAPIC, LAPIC delivery, BSP e IF funzionanti:
HPET risveglia HLT, risultato TIMEOUT, entrambe le sorgenti vengono fermate e
ripristinate, token rimossi, mapping temporanei revocati. Il caller attuale
sceglie panic controllato LAPIC_TIMER_INVALID dopo TIMER_WATCHDOG_TIMEOUT_CLEAN;
non c'e un retry arbitrario del timer o un successo falso.

Stop durante wait viene valutato al risveglio HPET. Un evento gia consegnato si
consuma una sola volta; se manca anche quello, il timer viene fermato senza
inventare un evento. Nessuna latenza wall-clock hard-real-time e dimostrata.

HPET assente/occupato/non conforme o route indisponibile: nessuna attesa LAPIC
con HLT, failure con rollback delle risorse gia acquisite. IRQ/status/EOI o
cleanup non verificabile: l'owner resta persistente e conserva l'ownership;
non si rimuove un handler vivo ne si dichiara il modulo chiuso.

Non si garantisce progresso se HPET si ferma dopo arm, se entrambi gli IRQ sono
persi, se il controller o CPU falliscono, oppure IF viene tenuto a zero da codice
esterno. Le API generiche x86_irq_sleep/x86_irq_wait continuano a richiedere un
contratto di wake: il loro budget conta risvegli, non tempo. R-001 riguarda i
call site timer protetti, non un timeout universale di qualunque driver.

## Registri, bit e fonti primarie

| Operazione | Offset/bit | Fonte e motivo |
|---|---|---|
| HPET ID/periodo | 0x00/0x04, NUM_TIM_CAP[12:8], COUNT_SIZE_CAP[13] | HPET 1.0a §2.3.4; limite timer e conversione fs |
| Config globale | 0x10, enable[0], legacy[1] | §2.3.5; ownership del blocco fermo, niente legacy |
| Interrupt status | 0x20, bit n W1C | §2.3.6/§2.4.6; ack soltanto della source posseduta prima di EOI |
| Counter | 0xf0/0xf4 | §2.3.7; scrivere soltanto mentre fermo |
| Timer config/cap | 0x100+0x20*n, route mask nella dword +4 | §2.3.8; usare solo timer/route implementati |
| Timer one-shot | type[1], enable[2], periodic[3]=0, size cap[5], 32mode[8], route[13:9], FSB[14]=0 | §2.3.8; level active-high su IOAPIC senza FSB |
| Comparator | 0x108+0x20*n | §2.3.9/§2.3.9.2.1; deadline futura con margine verificato |
| IOAPIC selector/window | 0x00/0x10 | 82093AA §§3.1.1/3.1.2; accesso indiretto dword e selector readback |
| IOAPIC redirection | indice 0x10+2*pin/+1; mask[16], level[15], polarity[13]=0, fixed/physical, vector0x40, destination[31:24] | §3.2.4; routing ammesso, delivery[12]/remoteIRR[14] RO devono essere zero |
| LAPIC timer stop/injection | LVT0x320 mask[16], Initial Count0x380=0, Current Count0x390 | SDM Vol.3A §12.5.4; stop e perdita intenzionale della sola delivery |

Fonti: [Intel HPET 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf),
[Intel 82093AA, 290566-001 (copia del manuale Intel)](https://pdos.csail.mit.edu/6.828/2010/readings/ia32/ioapic.pdf),
[Intel SDM dicembre 2024](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf).
UC usa PAT gia verificato, SDM §§13.12.2/13.12.3; ordering tramite mapping UC e
readback, non tramite volatile come sincronizzazione fra core. La versione
82093AA e un riferimento del programming model; non dimostra un device fisico
identico a quello emulato da QEMU. Nessuna nuova istruzione Assembly aggiunta.

## Riproduzione

Da D:\OS, PowerShell:

```powershell
& arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1
& arch/x86/kernel/timer/hpet/description/scripts/test.ps1
& test/kernel/timer/lapic/scripts/asan.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/kernel.ps1
```

kernel.ps1 crea immagini fault isolate, verifica 10 boot finiti positivi,
10 loop con almeno 16 eventi prima dello stop GDB, 10 lost-LAPIC finiti,
10 lost-LAPIC nel loop e due profili senza HPET. I test usano OVMF/ESP reali,
deadline host, snapshot owner nel monitor, tracce IRQ, IF, ISR/IRR e opcodes
STI/HLT/CLI. La VM terminata e soltanto quella avviata dal test. I valori
0xfed00000, comparator0, pin16, ticks10000000 sono aspettative del profilo
QEMU q35, non constant hardware universali.
