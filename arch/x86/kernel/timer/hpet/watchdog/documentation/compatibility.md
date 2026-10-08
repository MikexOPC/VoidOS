# HPET watchdog — selezione di comparator e route alternative

Rapporto del blocco di selezione, precedente alla diagnostica persistente.
Per lo stato successivo e le relative immagini/test vedere [guasti](failures.md).

Task del 7 ottobre 2026, limitata al modulo watchdog. Il profilo resta BSP,
ring 0, IF=0, un IOAPIC con GSI base zero e 24 input, destinazione fisica <=15.
Non aggiunge supporto per IOAPIC multipli, SMP, FSB/MSI, NMI o hardware fisico.

## Decisione e invarianti

La disponibilita del primo comparator non basta: le sue route possono essere
occupate o riservate da MADT. Ora si cercano i comparator annunciati dall'HPET,
in ordine crescente; la politica Rust filtra esclusivamente pin non ISA 16..23.
La verifica C della route considera ISO/NMI, mask, delivery, remote IRR e bit
riservati. Il costo massimo della ricerca e 32 comparator per otto pin: O(T*P),
con T<=32 e P=8. Nessuna nuova allocazione; owner e ABI restano di 224 byte.

```text
capture del blocco fermo -> candidato con route supportate
    -> ricerca IOAPIC completata senza pin -> candidato successivo
    -> pin acquisito -> configurazione/registry/avvio esistenti
    -> errore di lettura/scrittura/readback -> rollback, nessun fallback
```

Tutti i comparator devono essere inattivi prima di acquisire il blocco.
La selezione legge soltanto HPET e aggiorna lo snapshot del candidato solo dopo
aver letto config, capabilities e comparator completo. Non modifica timer
scartati. I prestiti HPET per capture e avanzamento sono read-only.

La ricerca IOAPIC distingue un pin occupato da un accesso fallito: il ritorno
positivo indica una scansione completata, mentre RouteOwned indica acquisizione.
Una scrittura parziale conserva ownership prima della prima mutazione e segue
il rollback esistente; non autorizza la ricerca di un altro comparator.
Un errore durante la selezione successiva conserva lo snapshot precedente.

Nessuna garanzia per cambiamenti concorrenti del device o SMP: gli oggetti Env
sono kernel fidati e immutabili, come nel [contratto R-001](design.md).
Se il cleanup non puo essere verificato, si conserva l'owner persistente.
Il watchdog non usa DMA e non espone API a puntatori user.

## Fonti primarie e registri

- [Intel HPET 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf):
  §2.3.4 NUM_TIM_CAP[12:8]+1 limita i comparator a 32; §2.3.8 config
  0x100+0x20*n, capabilities route nella dword +4; §2.3.9 comparator +8/+12.
  Si mantengono le verifiche enable[2], periodic[3], value-set[6], FSB[14].
- [Intel 82093AA, 290566-001](https://pdos.csail.mit.edu/6.828/2010/readings/ia32/ioapic.pdf):
  §§3.1.1/3.1.2 selector/window 0x00/0x10; §3.2.4 redirection
  0x10+2*pin/+1, mask[16], delivery[12], remote IRR[14], destination[31:24].
  Letture fallite non vengono convertite in valori sentinella di registro.

## Riproduzione e limiti delle prove

```powershell
& arch/x86/kernel/timer/hpet/watchdog/scripts/test.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/kernel.ps1
& arch/x86/kernel/timer/hpet/watchdog/scripts/compatibility.ps1
```

I test C ASan usano modelli RAM di HPET/IOAPIC per verificare snapshot, ownership,
pin occupati, esclusioni MADT, accessi falliti, scritture parziali e timer 31.
L'adapter MMIO reale e provato separatamente su storage RAM; non misura ordering
hardware. Rust verifica tutti i bit route e gli indici fuori range.

Il profilo QEMU alternativo riserva i pin del candidato zero tramite una politica
di test, senza alterare registri hardware per simulare capabilities inesistenti.
Il candidato uno usa realmente il secondo comparator HPET emulato, il suo bit
status e un IRQ IOAPIC. L'immagine di test e isolata e non installabile tramite
il flag Install. La prova non dimostra compatibilita con un altro device fisico.

La regressione R-001 controlla anche loop continuo, perdita intenzionale LAPIC
e HPET assente. Una panic prevista nel profilo negativo non e un crash inatteso.
ASan e accounting non sono Valgrind, ne una misura di leak di tutta la RAM fisica.
Coverage linee/branch e latenza hardware: N/D. L'attesa rimane circa 100 ms;
questa task migliora la disponibilita, non la latenza o la tolleranza a guasti
simultanei HPET/controller/CPU. Il rapporto [R-001](validation.md) e storico.

## Risultati eseguiti sullo stato finale

Build MSVC /W4 /WX e Rust -D warnings: zero warning, tutte le sei immagini
normali/loop/fault/alternative generate e validate dal controllo ELF.

| Prova | Risultato | Evidenza in kernel/build/ |
|---|---|---|
| C ASan, regressione e 10 scenari aggiuntivi ripetuti 100 volte | PASS; almeno 10.000 wait della regressione, 1.000 scenari nuovi | watchdog-host-d4c3110684874e5bb51c4b335e04922c/ |
| Rust deadline e selezione route | 5/5 PASS | stesso host, tools.log |
| Adapter MMIO reale su storage RAM ASan | PASS, incluso timer 31 e whitelist | stesso host, adapter-tests.exe |
| QEMU finito, comparator 0 | 10/10 PASS | watchdog-matrix-0768c029034349888a4bfc5ffbf05d25/normal.log |
| QEMU loop, comparator 0 | 10/10 PASS, 172 eventi totali, stop/cleanup | stessa matrice, continuous-evidence.json e loop-1.log..loop-10.log |
| QEMU IRQ LAPIC perso, finito e loop | 20/20 esiti negativi previsti | stessa matrice, lost.log e loop-lost.log |
| QEMU HPET assente | 2/2 rifiuti previsti prima dell'attesa | stessa matrice, absent.log |
| QEMU finito, comparator 1 | 10/10 PASS | watchdog-normal-7d4dcae4672d4bef9294bd91489d4750/ |
| QEMU comparator 1, IRQ LAPIC perso | 10/10 timeout con cleanup previsto | watchdog-lost-bb8810c1de4443a3a7ec377bb72a2255/ |

Casi nuovi host: primo pin occupato, route ISA escluse, fallback dopo ISO/NMI,
tutti i pin occupati, comparator diverso attivo, lettura IOAPIC fallita,
scrittura della destinazione fallita, scrittura parziale della redirection,
lettura del candidato successivo fallita e ultimo timer 31. Nei round trip
si confrontano integralmente gli array registri HPET/IOAPIC prima e dopo,
oltre a token, ownership, finestra e budget fisico invariato. Un errore durante
la ricerca non viene mascherato scegliendo un candidato successivo.

Totale matrice di accettazione: 30 boot positivi, 30 fault LAPIC intenzionali
e due profili senza HPET; zero crash inattesi. Il positivo finito e quello
alternativo hanno ciascuno dieci avvii consecutivi. Sono controllati marker,
owner chiuso, interrupt 0x40/0x41, ripresa STI/HLT/CLI, IF e ISR/IRR vuoti.
Le panic dei profili negativi sono risultati attesi, non boot positivi.

Durante la review e stato corretto anche il salvataggio dei riepiloghi del
loop: Write-Host usa lo stream PowerShell 6, che Tee-Object non riceveva.
Ora kernel.ps1 lo acquisisce esplicitamente. Dopo questa modifica la matrice
completa da 42 profili e stata ripetuta usando le stesse immagini finali,
senza ricompilare. Il manifest della prova alternativa differisce soltanto
per questo script di orchestrazione, che compatibility.ps1 non invoca:
sorgenti eseguibili e harness alternativi sono rimasti identici.

Cppcheck 2.22, sette unita C: zero error/warning/performance;
sette style (API usate fuori dal perimetro analizzato e firme dei callback),
cinque information (header standard non caricati e report dei checker).
I suggerimenti sono stati riesaminati senza cambiare le API dei callback
con cast o rendere statiche funzioni realmente chiamate dal LAPIC.
Evidenza: kernel/build/watchdog-compatibility-cppcheck.xml.

Controlli finali: massimo 203 righe per sorgente nel modulo; cinque file diretti
nel root e negli script, nessuna cartella oltre cinque; zero trailing whitespace,
tab o separatori senza riga vuota; script PowerShell validi; header locali senza
cicli, link documentali validi. Nessun TODO/FIXME/HACK/XXX nei sorgenti del modulo.
Valgrind non disponibile su questa toolchain Windows; nessuna dichiarazione
di prova Valgrind o di assenza globale di leak fisici. Nessuna prova su PC reale.

SHA256 ELF positivo finito, copiato senza rebuild in kernel/build/kernel.elf
e nella ESP ordinaria dopo tutti i test:
`829DC9F69484AE3CE515928BB377B11A0066CA6FA11946B0C4DFF7833C9261D4`.
Il bootloader della ESP usato nelle prove ha SHA256
`7A69766ACBA8724AA6D304629CE2D3EE13129ED0BD8405CB688B47E39FDD8304`.

Immagini alternative isolate in
kernel/build/watchdog-compatibility-cd5fac9434fa47b99ed109662189ee8a/:
positivo `21500A3093EC56DCEB4F505A69BC86967DE93E4052FAE1B27CDA97FF4C8DB28E`,
lost-LAPIC `801701D4F665E4B02AB685CEF98DF7FBAA93A5104598D051A9AE95B0167F8D27`.
I manifest images.json/sources.json conservano gli hash e i percorsi delle prove.
