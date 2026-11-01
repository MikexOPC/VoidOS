# K3-I — prerequisiti dell'ammissione, 1 novembre 2026

Correzione dei validatori esistenti e progetto del confine di pubblicazione
del contesto AP. L'ammissione runtime non è ancora implementata. Nessun AP
avviato; non si chiude K3 e non si passa a K4.

Aggiornamento successivo: scelta A acquisita e snapshot pubblicato nel
diagnostico; vedere [il report del gruppo CPU](k3i-group-validation.md).
I paragrafi seguenti conservano le prove del precedente blocco.

## Difetto riprodotto e corretto

Il record di ingresso escludeva soltanto lo stack principale di 64 KiB
dalle altre regioni virtuali. Il piano di ritorno faceva lo stesso per
destinazione e operando IDTR. Non venivano esclusi i tre IST e le guardie
intermedie; mancava inoltre il controllo di overflow dell'ultimo top IST.
I due nuovi test emergency_stack_ranges ed emergency_stack_aliases fallivano
prima della correzione: indirizzi nell'area di emergenza erano accettati.

Ora i due validator safe Rust condividono la dimensione dell'intervallo
privato: stack principale più estensione fino al top del terzo IST.
Sono rifiutati alias di entry, descrittori, area locale e piano di ritorno;
il top finale deve essere rappresentabile senza wrap. ABI, lease, geometria
e timeout restano invariati. Sono controlli numerici, non prove di mapping:
un indirizzo appena fuori dall'intervallo non diventa autorizzato dal test.

Non è stata osservata una corruzione runtime provocata da questo difetto.
Il builder C genera già la geometria fissa e il preflight nativo controlla
le lease. La lacuna era nella validazione di record/piani alterati prima
del futuro ingresso AP; non costituisce da sola un exploit user/kernel.

Il test C attraversa tutti i 63 indici AP e le 18 pagine dell'estensione:
entry e Local alterati sono rifiutati senza modificare l'output. Il builder
del ritorno è provato separatamente con alias di entry e IDTR, conservando
l'output. Sono coperti anche estremi di intervallo e overflow.

## Prove finali di questa modifica

- Protocollo/record: kernel/build/k3i-admission-record-v2/, 10 casi Rust
  protocollo e 7 record in debug/release; due eseguibili C/Rust, ASan sul C,
  compilazione /W4 /WX e cppcheck senza diagnostiche bloccanti.
- Ritorno: kernel/build/k3i-admission-return-v2/, 5 casi Rust in entrambe
  le modalità, C/Rust con ASan sul C e dipendenze freestanding controllate.
  Il contatore del report del runner è aggiornato da 4 a 5.
- Packer: kernel/build/k3i-admission-blob-final/, 3 casi Rust debug/release,
  C/Rust ASan, due rilocazioni e 8192 corruzioni byte del pacchetto.
- Joint/rollback: kernel/build/k3i-admission-joint-host/ e relativo .log,
  53 invocazioni host finali, compresi 48 punti di guasto I/O; gli arresti
  terminali intenzionali sono distinti dai successi e dalle failure inattese.
- Ritorno CPU: kernel/build/ap-return-cpu-5bfd8fbe1ffa466b910fd91bcc4405ea/,
  17/17: dieci positivi consecutivi, controllo senza NMI pendente e sei
  negativi. Pacchetti e piano sono quelli dei builder aggiornati. Piano
  v2 e piano della campagna hanno hash uguale. Questo verifica il percorso
  Assembly esistente con input accettati, non esegue Rust sulla CPU guest.
- Joint + panic: kernel/build/ap-joint-firmware-7aaa7c9041cc44d4a539db033e9b243e/,
  10/10 UEFI consecutivi con kernel/build/k3i-admission-joint/kernel.elf.
  Sigillo, budget trattenuto, record panic e IST predisposti verificati;
  soltanto il BSP ammesso, nessun INIT/SIPI.
- Ordinario: kernel/build/percpu-firmware-1eddaa0f4f654101bf772bba7eebd920/,
  10/10 UEFI con kernel/build/k3i-admission-ordinary/kernel.elf.

Totale: dieci boot per ciascuno dei due profili UEFI, distinti dai 17
esperimenti SeaBIOS sul BSP. Questi ultimi non sono boot del kernel e
scrivono ACTIVE nella fixture; non dimostrano promozione nativa o SMP.
Manifest con hash: kernel/build/k3i-admission-manifest.json.
Audit sorgenti: kernel/build/k3i-admission-source-audit.json, massimo
139 righe nei sette file modificati; nessuna violazione whitespace,
CR isolato o limite dei cinque file diretti nelle cartelle interessate.

ASan strumenta il C host, non Rust/Assembly o RAM fisica del guest.
Valgrind, leak detector guest, coverage e hardware reale N/D. Nessuna nuova
allocazione introdotta dal fix. Cppcheck conserva messaggi informativi
sugli include di sistema; non viene dichiarato un report vuoto.

Quattro wrapper PowerShell rimasti aperti dopo il marker finale sono stati
interrotti soltanto dopo la verifica di log, risultati e assenza di VM
attive possedute. L'exit code 1 dell'interruzione non è un exit code PASS
del wrapper e non è contato come test negativo: i risultati sopra sono
quelli degli eseguibili e delle campagne completate. I runner CPU e UEFI
hanno terminato con exit code 0.

## Collegamento successivo

Il [progetto di ammissione](k3i-admission-design.md) descrive i vincoli
verificati nelle interfacce reali: getter e provider ancora BSP-only,
pubblicazione release/acquire, stato AP osservabile atomicamente, IDT
finale prima del panic admit, nessuna lettura remota dei lock mutabili.
La scelta fra descrittore unico immutabile e pubblicazioni per AP è stata
presentata all'utente secondo direttive-locali §7; non è implementata in questo
blocco. Restano poi il collegamento C/Assembly e il trasporto hardware
nativo, conferme PRIVATE/PARKED, dieci avvii AP reali e panic remoto.
