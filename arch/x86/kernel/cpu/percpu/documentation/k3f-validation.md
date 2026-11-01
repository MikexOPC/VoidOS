# K3-F — panic coordinato: implementazione e prove BSP

30 ottobre 2026. Contratto approvato: sistema completo, NMI diretti con
attesa limitata. [Progetto](k3f-smp-design.md). Il backend è implementato;
il gate SMP resta aperto perché nessun AP esegue ancora codice OS.

## Comportamento implementato

Rust gestisce elezione one-shot, ammissione, pubblicazione dei record,
ACK e snapshot dei mancanti. Le transizioni condivise sono atomiche:
IF=0 e volatile non sostituiscono la sincronizzazione tra core. Un solo
reporter; ACK tardivi non modificano la maschera finale dei mancanti.

C possiede record allineati da 512 byte: statico per il BSP, nella pagina
metadati privata per ogni AP, a offset 256 dopo GDT/TSS. Nessuna nuova
lease per i record. Registrazione prima dell'attivazione, rifiuto degli
alias, rollback dei binding parziali e risorse pinned dopo il commit.
Un'origine registrata può segnalare un fault prima dell'ammissione online;
soltanto i core ammessi diventano destinatari dell'arresto remoto.

Assembly salva i registri prima di C; fault/NMI usano gli IST verificati.
L'identificazione non dipende da GS. Ingresso volontario e fault convergono
sul protocollo. Ricorsione del vincitore conserva il primo record e termina
senza logger. Il trasporto usa solo il mapping LAPIC permanente validato:
destinazioni fisiche esplicite, nessun broadcast ai core firmware.

L'attesa globale ha budget PM di 100 ms e massimo un milione di controlli.
L'output COM1 polled è separato dal logger: massimo 2.048 byte, 250 ms e
200.000 controlli. Nessuna allocazione, lock ordinario, rendering o lettura
della stringa indicata dal chiamante. Il puntatore al messaggio è un dato
numerico. Clock fermo, UART bloccata e ICR occupato hanno esiti distinti.
Il panic non arresta implicitamente DMA o dispositivi.

L'attivazione nativa è per ora nei diagnostici isolati. Il kernel ordinario
conserva il percorso locale precedente; compilare il backend non equivale
ad abilitarlo nel boot normale. L'API di ammissione AP verifica descrittori,
contesto, xAPIC e PAT, ma il suo ramo AP non è ancora provato su un AP vivo.

## Evidenze finali

Gli artefatti sono locali in `D:/OS/kernel/build/`, esclusi dal sorgente
versionato. Il manifesto `k3f-final-validation.json` contiene hash SHA-256,
conteggi dei sorgenti e riferimenti alle campagne seguenti.

| Verifica | Risultato | Evidenza in kernel/build/ |
| --- | --- | --- |
| Suite host panic | PASS | test-run-5171bcd6b0cd4689a8cf3fcce42c720c/results.json |
| Host dettagliato | 12 esecuzioni Rust, 10 processi record C, 11 casi trasporto, 10 output | percpu-panic-host-9d511360c7824fc288da6282c9e8e864/ |
| Diagnostico finale | 21/21 | panic-matrix-3876d94f5bf54ff594d3a09308536744/results.json |
| Boot ordinario finale | 10/10, alternanza una/quattro vCPU configurate | percpu-firmware-f4ed3b1cf7f44f7089a3e7580acd5176/results.json |
| Console VirtIO/VT-d | 1/1, pixel, input, movimento e cleanup | pointer-interactive-check-319ab4f5238c44648619ad422445f644/results.json |
| Analisi nativa cppcheck | zero error/warning/performance/portability; 22 information e 1 style | k3f-native-static/cppcheck.xml |
| Disassemblato emergenza | 11 oggetti senza dipendenze FP/SIMD rilevate | k3f-confirmed-diagnostic/*.obj.asm.txt |

I 21 scenari comprendono dieci preparazioni consecutive con quattro vCPU
configurate, preparazione BSP-only, rollback e ripreparazione, e nove
terminazioni intenzionali: panic volontario, #UD, #PF, NMI locale reale,
#DF, CR0.TS con fault, ricorsione, lock detenuto, CR0.TS con kernel_panic.
I fault attesi sono verificati attraverso record, memoria e stato CPU;
non sono conteggiati come boot ordinari riusciti. In ogni scenario esegue
il sistema operativo soltanto il BSP.

Le varianti modificano esclusivamente il selettore diagnostico di otto
byte in un segmento scrivibile non eseguibile; codice eseguibile identico,
hash di ogni variante nei risultati. Base diagnostica:
`F54487565892065884ECE03ED443CFC6C7301AD11A4A64D8DFC9A5B555856AD8`.
L'hash dell'immagine ordinaria e della console è nel manifesto.

Regressioni host kernel, bootstrap e risorse AP passate rispettivamente in
`test-run-25788ca0329942b09b65c7dd2f78e65a`,
`test-run-a64a6d354e7c43a183f46ec3b613ff32` e
`test-run-56d3628a156d4d2cb93fb167651fc019`.

Il C host usa ASan; Rust e RAM fisica non sono coperti da quel sanitizer.
I test controllano ownership, rollback e budget pertinenti, senza dichiarare
una misura globale dei leak. Valgrind non eseguito; coverage linee/branch N/D.
Cppcheck segnala header di sistema mancanti e limiti dell'analisi dei rami:
non costituisce una prova formale di assenza di difetti.

## Difetti trovati e corretti durante il blocco

- La preparazione parziale lasciava binding registrati: aggiunto rollback
  e prova QEMU di rifiuto, pulizia e successiva preparazione riuscita.
- Il compilatore poteva introdurre SIMD nell'emergenza: unità C interessate
  compilate senza ottimizzazione, scritture esplicite e audit del codice
  generato. `/Qvec-` non supportato è stato rimosso; prove reali CR0.TS
  separate per ingresso fault e API pubblica. Non basta una sola flag.
- L'harness leggeva uno snapshot seriale precedente all'ultimo messaggio:
  aggiunta attesa host limitata per drenaggio e nuova lettura. La campagna
  esplorativa fallita resta fallita; la campagna finale è stata rieseguita.
- Il wrapper host rimaneva attivo dopo il PASS del figlio: uscita esplicita
  solo per esecuzione standalone riuscita; verificato exit code zero.
- Accessi testuali ambigui per cppcheck sostituiti con lunghezze letterali;
  azzeramento del record reso esplicito con helper tipizzato, senza soppressioni.

Controllo finale del modulo e test: 24 file sorgente/script, massimo 160
righe, massimo cinque file diretti per cartella, nessuno spazio finale o
CR isolato. Il diff globale segnala cinque trailing whitespace preesistenti
in kernel/main.c:17–20 e kernel/startup/startup.h:26, fuori dal blocco;
non sono stati modificati. Nessun commit/push o modifica a reuse/.

## Riproduzione e gate ancora aperto

Suite host: `./scripts/test/run.ps1 -Suite percpu-panic`.
Campagna registrata: `./scripts/test/qemu.ps1 -Campaign percpu-panic`.
La modalità smoke riduce le ripetizioni, non chiude il gate dei dieci boot.

Restano riserva bassa, trampolino e ingresso AP, poi ammissione reale e
campagna con almeno quattro core OS: fault BSP/AP, NMI remoti, competizione
tra reporter, IF=0, lock detenuto e core senza ACK. Elezione concorrente,
timeout e ACK tardivi sono oggi provati sul modello host; non su SMP guest.
Non si avanza a funzionalità che assumano già questa garanzia.

Nessuna prova su hardware fisico, x2APIC, hotplug o arresto DMA. I riferimenti
Intel SDM, Linux v6.12 e UART QEMU v11.1.0 e i campi hardware esatti sono
nel progetto collegato; nessun codice Linux copiato. K3-F non è ancora
dichiarabile completo per SMP operativo.
