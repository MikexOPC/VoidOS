# Watchdog — stop e retry controllato del cleanup

Blocco del 7 ottobre 2026, successivo alla [diagnostica](failures.md).
Perimetro: solo watchdog, con i suoi test e le interfacce di build/validazione.
La scelta autorizzata e fermare il servizio e recuperare soltanto il cleanup
verificabile. Non riavvia LAPIC, non riapre HPET, non aggiunge fallback timer,
scheduler, SMP, NMI o supporto a nuovi controller.

## Politica e stati

Rust classifica uno snapshot scalare; nessun pointer, unsafe block, heap o I/O
entra nella politica. C valida il proprietario canonico, i flag booleani,
token, CPU/IF=0, budget e mapping. La query e passiva: non ripara lo stato e non
azzera diagnosi. READY con Count esaurito richiede stop, non viene detto RUNNING.

| Codice | Stato | Azione del caller |
|---|---|---|
| 0 | NOT_STARTED | Nessuna sessione inizializzata |
| 1 | REJECTED | Apertura rifiutata prima di acquisire risorse; consultare la fase |
| 2 | RUNNING | Nessun guasto registrato, sessione attiva; valgono i prerequisiti della wait |
| 3 | STOP_REQUIRED | Fermare il servizio e chiamare close; non riattivare il timer guasto |
| 4 | RETRY_CANDIDATE | Cleanup incompleto in una fase ammessa; serve nuovo preflight reale |
| 5 | RETAIN | Stato incoerente, IRQ/wake guasto, Armed/Error o quiescenza non dimostrata |
| 6 | RELEASED | Risorse rilasciate e chiusura verificata; storico degli errori preservato |
| 7 | EXHAUSTED | Tre tentativi di recovery consumati, nessun ulteriore retry pubblico |

Una fase non identifica da sola la causa hardware: REJECTED non significa
automaticamente hardware incompatibile; ENVIRONMENT puo anche essere un errore
del caller. RETRY_CANDIDATE non garantisce la riuscita del preflight. RELEASED
non significa servizio sano o riavviabile: un timeout LAPIC resta un timeout.

```text
RUNNING -> wait fallita -> STOP_REQUIRED -> close iniziale
    -> ripristino completo -> RELEASED, diagnostica preservata
    -> cleanup MASK/UNBIND/RESTORE_DEVICE/RESTORE_ROUTE/VERIFY fallito
       -> RETRY_CANDIDATE -> claim di un tentativo -> preflight
          -> quiet dimostrato -> una ripresa del cleanup -> RELEASED oppure RETAIN
          -> quiet non dimostrato -> RETAIN, ownership conservata
          -> terzo tentativo fallito -> EXHAUSTED
Armed/Error, guasto IRQ/wake, stato o prerequisiti invalidi -> RETAIN
```

Il massimo tre e una scelta di budget software v0, non un numero imposto dal
datasheet. Ogni chiamata di `x86_watchdog_recover()` esegue al massimo un tentativo,
senza loop, attese HLT o arm. Anche una prova di preflight fallita consuma il
budget. Dopo il primo cleanup fallito, i retry di `x86_watchdog_close()` usano la
stessa politica e lo stesso contatore: non possono aggirare il limite.
La prima close conserva il flusso gia verificato. Il backend interno
`x86_watchdog_finish_cleanup()` e riservato alla close iniziale e al recovery
che ha gia validato il preflight: non e un'interfaccia per altri driver.

L'owner non viene azzerato per superare il limite. Armed, Error o storia IRQ/wake
non vengono cancellati per autorizzare un retry. La sessione attuale non offre
reopen dopo Closed; il riavvio di un servizio resta una task distinta.

## Preflight e ripristino

Prima della ripresa devono essere validi CPU/IF, token eventuale, inventory,
budget e finestra inattiva, con LAPIC ISR/IRR quieti. Se restano risorse device,
si rilegge HPET: ID e periodo originali, configurazione globale ammessa,
configurazione del comparator salvata o posseduta, enable IRQ disabilitato,
nessun interrupt pending. Se la route resta posseduta, si verifica una entry
mascherata e riconosciuta, senza delivery/remote IRR e con destinazione ammessa.
Ogni prestito revoca il mapping prima del successivo controllo; preflight HPET
read-only, IOAPIC scrive soltanto selector e suo ripristino.

Il cleanup esistente conserva readback, restore e quiescenza finale. Un errore
non libera un handler vivo. La query RELEASED non dereferenzia Env: i suoi
pointees possono terminare la propria durata dopo close, mentre l'owner statico
conserva il record verificato di rilascio. Layout e ABI dell'owner restano 224 byte.
Il budget aggiunto e un contatore statico di 32 bit; stringhe/code Rust sono parte
dell'immagine, non allocazioni dinamiche.

Corretto un bug nel retry dopo restore IOAPIC parziale: se una scrittura della
entry originale riusciva ma l'accesso riportava failure, la route restava
posseduta; il successivo mask richiedeva ancora la entry del watchdog e bloccava
il ripristino. Dopo restore HPET confermato, il retry verifica la entry gia
mascherata e riprende il restore senza richiedere di riconfigurarla come owned.
La prova host scrive realmente nel modello la entry originale prima di fallire.

## Fonti primarie

- [Intel HPET 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf),
  §§2.3.4/2.3.5/2.3.6/2.3.8: ID/periodo 0x00/0x04, config globale 0x10,
  status W1C 0x20, timer config 0x100+0x20*n e enable IRQ bit2.
  Il preflight legge lo status, non lo cancella; non forza un acknowledgement.
- [Intel 82093AA, 290566-001](https://pdos.csail.mit.edu/6.828/2010/readings/ia32/ioapic.pdf),
  §§3.1.1/3.1.2/3.2.4: selector/window 0x00/0x10, redirection 0x10+2*pin/+1,
  mask bit16, delivery bit12, remote IRR bit14. Mask non e cancellazione di un IRQ
  gia accettato: servono anche source quiet e controllo LAPIC.

Il preflight non aggiunge nuove scritture hardware; la ripresa usa il restore
esistente. Sincronizzazione valida soltanto sul BSP/IF=0. Niente DMA o API user.

## Test e limiti

```powershell
& arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/kernel.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/compatibility.ps1
```

Host: politica Rust con tutti i 256 snapshot di flag, tutte le fasi diagnostiche,
indici/budget invalidi; test C ASan con retry transiente/persistente, limite anche
tramite close, pending HPET, remote IRR, identita cambiata, source invalida,
timeout, IRQ guasto, flag incoerenti, Count esaurito e lifetime di Env dopo close.
Le prove di quiescenza e failure host usano mock: non dimostrano ordering fisico.

Le immagini `TestWatchdogCleanupRecovery` falliscono una volta prima del restore
HPET, dopo mask/unbind reali, poi invocano il recovery. E un'iniezione software:
non simula un bus fisico guasto. Preflight, restore e readback usano i device
QEMU reali. La variante con IRQ LAPIC perso deve continuare a terminare con il
timeout/panic previsto, pur recuperando il cleanup: non diventa KERNEL_READY.
Il flag test e escluso da Install e dalle immagini ordinarie.

I log aggiungono stato di recovery e contatore. Con report intermedi, il
validatore legge l'ultimo stato per confrontarlo con l'owner chiuso; lo storico
dei due errori resta invariato. Coverage linee/branch e latenza fisica: N/D.
Valgrind non disponibile sulla toolchain Windows; ASan e accounting fisico sono
prove distinte. Nessuna prova su hardware fisico, nessuna garanzia se entrambi
gli IRQ o il controller falliscono. Nessun reset automatico del servizio.

## Risultati finali del blocco

| Gate | Esito | Evidenza in kernel/build/ |
|---|---|---|
| Build MSVC /W4 /WX, Rust -D warnings, controllo ELF | PASS, zero warning, otto immagini distinte | log di build delle matrici sotto |
| C ASan: regressione, compatibilita, diagnostica e recovery | PASS, 100 ripetizioni: almeno 10.000 wait della regressione, 1.000 scenari compatibilita, 2.400 verifiche diagnostiche, 2.300 recovery | watchdog-host-c0cfa2e873c94fe99aa39f126acee48d/ |
| Rust deadline, route e recovery | 8/8 PASS | stesso host, tools.log |
| Adapter MMIO reale su storage RAM ASan | PASS | stesso host, adapter-tests.exe |
| QEMU positivo finito e loop, comparator 0 | 20/20 positivi, dieci consecutivi per profilo | watchdog-matrix-40673f41ec5240a3b47513c8a31cad25/ |
| QEMU lost-LAPIC finito e loop | 20/20 timeout con cleanup previsti | stessa matrice, lost.log e loop-lost.log |
| QEMU HPET assente | 2/2 rifiuti prima dell'attesa | stessa matrice, absent.log |
| QEMU cleanup recovery, finito e lost-LAPIC | 10 positivi +10 timeout previsti; un tentativo reale per VM | stessa matrice, recovery.log e recovery-lost.log |
| QEMU comparator 1, positivo e lost-LAPIC | 10 positivi +10 timeout previsti | watchdog-compatibility-9e55c64dce3c49a986098d29c052382a/ |

Totale accettazione finale: 40 boot positivi, 40 fault LAPIC intenzionali,
due profili senza HPET; nessun crash inatteso o risultato flaky osservato.
Il loop completo ha gestito 171 eventi prima di stop e cleanup; i riepiloghi e
percorsi grezzi sono in continuous-evidence.json della matrice finale.
Le prove finali sono state ripetute dopo l'ultima correzione const del sorgente;
i report non sostituiscono gli esiti con quelli delle immagini precedenti.

La recovery nativa passa da RETRY_CANDIDATE/attempts=0 a RELEASED/attempts=1,
con prima fase di cleanup RESTORE_DEVICE preservata. Nel positivo la prima fase
operativa resta NONE; nel lost-LAPIC resta DEADLINE e il kernel continua al panic
previsto. Verificati token rimossi, owner chiuso, Count, source fermate, finestra
revocata, budget, ISR/IRR vuoti, IF, RIP di ripresa e sequenza STI/HLT/CLI.
Il controller e il comparator del profilo QEMU sono verificati dagli snapshot,
readback del codice e tracce IRQ; non si deduce funzionamento da una VM silenziosa.

Cppcheck 2.22, otto unita C: zero error/warning/performance; sette style,
cinque information. Style residui: firme callback richieste dall'API e funzioni
chiamate fuori dal perimetro analizzato; nessun suggerimento const nuovo lasciato.
Evidenza: kernel/build/watchdog-recovery-cppcheck-final.xml.

Review finale: max sorgente 291 righe, max cinque file diretti, inclusi i cinque
documenti e i cinque script; zero trailing whitespace, tab o separatori senza
riga vuota. Parser PowerShell, link documentali e header locali senza cicli: PASS.
Nessun nuovo accesso senza fonte; preflight non scrive status, comparator o
redirection. Zero nuove allocazioni dinamiche; budget e rollback provati con
accounting distinto da ASan. Valgrind e hardware fisico restano non verificati.

ELF positivo finito copiato senza rebuild in kernel/build/kernel.elf e nella
ESP ordinaria, SHA256:
`ED0D57A4367272D5AC1EFD5687EE4451E0EED1A692DED01B7B8AA177DBD9D286`.
Bootloader EFI della ESP usato nelle prove:
`7A69766ACBA8724AA6D304629CE2D3EE13129ED0BD8405CB688B47E39FDD8304`.
images.json/sources.json delle matrici conservano i manifest finali;
installed-images.json registra gli hash dopo la copia dell'immagine positiva.
Immagini con injection o route alternative restano isolate dalla ESP ordinaria.
