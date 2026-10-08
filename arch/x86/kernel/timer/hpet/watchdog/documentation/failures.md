# Watchdog — diagnostica persistente delle fasi fallite

Rapporto del blocco diagnostico precedente; la politica di stop/retry e le
immagini successive sono descritte nel [rapporto recovery](recovery.md).

Blocco del 7 ottobre 2026, successivo alla [selezione alternativa](compatibility.md).
Perimetro: stesso watchdog BSP, stessi timer, routing e rollback. Nessuna nuova
architettura di recovery, nessun nuovo registro o backend hardware.

## Contratto

Due codici conservano la prima fase operativa fallita e la prima fase di cleanup
fallita. Sono metadata statici nel core che possiede gia il watchdog: otto byte,
nessuna allocazione. Owner e ABI di 224 byte, Count/Error, token e offset del
monitor restano identici. Le stringhe delle fasi sono costanti read-only.

`x86_watchdog_failure()` e `x86_watchdog_cleanup_failure()` restituiscono lo
storico della sessione. Zero indica nessun guasto registrato in quella categoria.
Un retry di close riuscito conserva lo storico: lo stato Closed/token/ownership
indica la situazione corrente, i codici raccontano cosa era fallito.
L'apertura di una sessione non posseduta azzera i codici; un tentativo di riaprire
un owner ancora presente non altera la diagnosi o le sue risorse.

I caller rispettano BSP/IF=0. L'IRQ memorizza soltanto la fase; stampa e formattazione
restano fuori dall'handler. Nessuna sincronizzazione SMP viene dichiarata.
Un codice di fase non identifica automaticamente la causa hardware: CAPTURE
puo significare blocco occupato, capability non ammessa o lettura fallita;
SELECTION puo significare candidati esauriti oppure un errore di accesso.
L'assenza di HPET puo essere rifiutata da un prerequisito del caller prima di
raggiungere questa API: non viene attribuita una diagnosi inventata al watchdog.

## Codici stabili

| Codice | Nome | Verifica o operazione fallita |
|---|---|---|
| 0 | NONE | Nessuna fase fallita registrata |
| 1 | ENVIRONMENT | Ambiente, puntatori kernel fidati, prerequisiti e profilo iniziale |
| 2 | PROFILE | Coerenza e ownership del controller IOAPIC |
| 3 | CAPTURE | Snapshot del blocco fermo e candidato iniziale |
| 4 | ROUTE | Ricerca/acquisizione della route |
| 5 | SELECTION | Avanzamento a un comparator successivo |
| 6 | CONFIGURE | Configurazione e prova di progresso del counter |
| 7 | BIND | Registrazione del callback e quiescenza richiesta |
| 8 | UNMASK | Attivazione della route acquisita |
| 9 | OUTPUT | Annuncio dell'avvio |
| 10 | WAIT_STATE | Stato owner, token, counter e prerequisiti della wait |
| 11 | SOURCE | Range/alias/goal o contenuto della source/stop |
| 12 | ARM | Programmazione e verifica della deadline |
| 13 | IRQ | Stato IRQ, ack del comparator o EOI |
| 14 | WAKE | Ritorno dal backend, count, finestra o quiet LAPIC |
| 15 | CLASSIFY | Classificazione finale FAULT della politica Rust |
| 16 | DEADLINE | Source richiesta non completata al risveglio del watchdog |
| 17 | CLOSE_STATE | Prerequisiti di close, Armed o Error |
| 18 | MASK | Mascheramento della route prima di detach |
| 19 | UNBIND | Rimozione sicura del token |
| 20 | RESTORE_DEVICE | Ripristino del blocco HPET |
| 21 | RESTORE_ROUTE | Ripristino della redirection IOAPIC |
| 22 | VERIFY | Finestra, inventory o stato LAPIC dopo ripristino |
| 23 | LOG | Risorse gia chiuse, stampa finale non completata |

I codici 17..23 descrivono le fasi di cleanup. Il setter interno della fase
operativa rifiuta zero e valori fuori tabella; non fornisce controllo hardware.
La tabella nomi ha un assert di dimensione e un accesso protetto dai bounds.
Un errore IRQ seguito da wake/close falliti conserva IRQ come prima fase;
il fallimento di close compare separatamente. Un timeout con rollback riuscito
riporta DEADLINE e cleanup NONE, non un errore di cleanup.

## Log e gestione del guasto

Esempio di formato, non risultato di una prova:

```text
KERNEL_WATCHDOG_FAILURE: 0x0000000000000007
KERNEL_WATCHDOG_CLEANUP_FAILURE: 0x0000000000000014
KERNEL_WATCHDOG_FAILURE_PHASE: BIND
KERNEL_WATCHDOG_CLEANUP_PHASE: RESTORE_DEVICE
```

Si tenta il report anche se il cleanup non puo proseguire. Se la console non
funziona, i getter restano disponibili; nessun log viene dichiarato consegnato.
LOG descrive un errore di reporting dopo il rilascio verificato: non una perdita
di memoria. La diagnostica non converte guasti in successo, non forza l'unbind
di handler vivi e non scavalca ownership. Quando il cleanup non e verificabile,
owner e risorse necessarie restano persistenti, come nel [contratto](design.md).

Nessun cambio agli accessi hardware documentati: HPET 1.0a §§2.3.4..2.3.9,
§2.4.6 e Intel 82093AA §§3.1.1/3.1.2/3.2.4, con fonti nel contratto R-001.
L'handler mantiene il backend Assembly e l'ordine ack/EOI gia verificati.

## Prove e riproduzione

```powershell
& arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/kernel.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/compatibility.ps1
```

La suite host aggiunge 24 gruppi di verifiche ripetuti cento volte: reset,
codici fuori range, positivo, errori di ambiente/profile/capture/route/selection,
configurazione/bind, arm, timeout, IRQ, source, cleanup fallito, storico dopo
retry, due guasti concatenati, failure della console dopo il rilascio e stop.
Il confronto con i test preesistenti conserva le prove di rollback e budget.

Il validatore QEMU richiede anche codici e nomi coerenti: NONE/NONE nel positivo
e DEADLINE/NONE con IRQ LAPIC perso, oltre agli snapshot e alle tracce originali.
ASan usa mock dei device; l'adapter reale e testato separatamente su storage RAM.
Nessuna nuova allocazione nel codice diagnostico; accounting fisico distinto
dal sanitizer. Valgrind e hardware fisico non sono verificati su Windows.
Coverage linee/branch e latenza su PC reale: N/D.

## Risultati finali eseguiti

| Gate | Risultato | Evidenza in kernel/build/ |
|---|---|---|
| Build MSVC /W4 /WX e Rust -D warnings | Zero warning, sei immagini ELF verificate | build log delle due matrici sotto |
| C ASan: suite esistente, compatibilita e diagnostica | PASS; 100 ripetizioni, 10.000 wait della regressione, 1.000 scenari compatibilita e 2.400 gruppi diagnostici | watchdog-host-76f7b1eaced94d88809eeb5e9e895ef5/ |
| Rust deadline/route | 5/5 PASS | stesso host, tools.log |
| Adapter MMIO reale su RAM ASan | PASS | stesso host, adapter-tests.exe |
| QEMU comparator 0 finito | 10/10 positivi consecutivi | watchdog-matrix-1cb6b1359eb0470983fc82c951e05915/normal.log |
| QEMU loop continuo | 10/10 positivi, 172 eventi, stop e cleanup | stessa matrice, loop-1.log..loop-10.log, continuous-evidence.json |
| QEMU IRQ LAPIC perso, finito e loop | 20/20 timeout e cleanup previsti | stessa matrice, lost.log e loop-lost.log |
| QEMU HPET assente | 2/2 rifiuti prima dell'attesa | stessa matrice, absent.log |
| QEMU comparator 1 positivo e IRQ LAPIC perso | 10 positivi consecutivi +10 timeout previsti | watchdog-compatibility-82e43a8f2650407cbbcd517e0f87200c/ |

Totale: 30 boot positivi, 30 fault LAPIC intenzionali, due profili senza HPET;
nessun crash inatteso o risultato flaky osservato. I positivi richiedono
NONE/NONE, i timeout DEADLINE/NONE, codici e nomi coerenti. Verificati inoltre
Count/token/Closed, budget e finestra, IRQ 0x40/0x41, IF, STI/HLT/CLI e ISR/IRR
vuoti dopo cleanup. Le panic dei fault intenzionali restano separate dai successi.

L'hardware QEMU e q35/CPU max/TCG, 128 MiB, OVMF ed ESP reali. Nei test
alternativi una politica di test riserva il candidato zero: il comparator uno
e il suo IRQ sono reali nel device emulato; non e una prova su un diverso PC.
I guasti concatenati e della console sono mock host, non iniezioni hardware.

Cppcheck 2.22, sette unita C: zero error/warning/performance, nove style
(sette precedenti piu i due getter diagnostici usati dai test), cinque information
per header standard e checker report. Le API pubbliche sono mantenute per caller
e debugger; i callback rispettano le firme supportate, senza cast per silenziare
suggerimenti. Evidenza: kernel/build/watchdog-diagnostics-cppcheck.xml.

Review finale: nessun nuovo accesso hardware, nessun rilascio anticipato o retry
automatico aggiunto; IRQ registra soltanto metadata; i getter non mutano stato.
Massimo sorgente 241 righe, massimo cinque file diretti per cartella, zero
trailing whitespace/tab/separatori senza riga vuota. Script PowerShell validi,
header locali senza cicli, link documentali validi, nessun TODO/FIXME/HACK/XXX
nei sorgenti. Copertura quantitativa non misurata, Valgrind non disponibile.

Hash SHA256 dell'ELF positivo testato, copiato senza rebuild in
kernel/build/kernel.elf e arch/x86/boot/uefi/build/esp/EFI/BOOT/kernel.elf:
`8195AFFB10F1D8B1B0BDAF4B963123B4F455D0CEAF733FB5604EF1E0D53C2FE0`.
Il bootloader della ESP usato nei test rimane
`7A69766ACBA8724AA6D304629CE2D3EE13129ED0BD8405CB688B47E39FDD8304`.
I manifest sources.json/images.json delle matrici conservano sorgenti e immagini;
installed-images.json registra gli hash dopo la copia dell'immagine positiva.
Le immagini alternative e con fault restano isolate dalle immagini ordinarie.
