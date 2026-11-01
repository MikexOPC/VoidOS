# K3-I — installazione inattiva nelle lease, 1 novembre 2026

Sottopasso del [design approvato](k3i-design.md): installare il pacchetto
C/Rust nelle pagine fisiche già riservate, senza avviare AP o congelare
risorse a metà. Sono inclusi soltanto gli adattatori transizione/build/test.

## Invarianti prima del codice

Il BSP, con IRQ disabilitati e nessun lock posseduto, possiede in esclusiva
le pagine e la finestra. Nessun AP ha ricevuto INIT/SIPI. Il template è
incorporato dalla build, non arriva da input esterno. Record e indirizzi
devono corrispondere a code/data/tabelle/root/PAT dell'owner di transizione.
Il diagnostico ricava identità, area locale e descrittori dalle risorse AP
preparate, ma non rende quell'AP online e non pubblica COMMITTED.

Un owner di installazione statico conserva record, template e due immagini
attese da 4 KiB, con una pagina scratch per rilettura. Nessuna allocazione
nuova. L'owner deve essere vuoto e distinto da template, owner transizione,
pool e contesto lock. Copie dell'owner non acquisiscono ownership.
Un prestito esclusivo separato nell'owner transizione impedisce cancel,
build e freeze mentre l'installazione possiede il contenuto, anche se il
campo State viene alterato. Il prestito si chiude soltanto dopo cancellazione
e verifica; il registro è esclusivo del BSP di bootstrap, non un lock SMP.
Anche il rilascio del prestito ricontrolla che codice e dati siano zero:
azzerare soltanto i dati non autorizza il cleanup generico delle risorse.

Sequenza concettuale, non eseguibile:

    vuoto -> immagini verificate -> installazione -> rilettura -> installato
                                            errore -> sporco, risorse trattenute
    installato/sporco -> azzeramento e rilettura -> vuoto, rollback possibile

Le scritture passano dalla finestra esistente; il backend rifiuta una pagina
codice non appartenente alla lease o qualsiasi scrittura dopo congelamento.
Il verifier storico `xt_verify` conserva il requisito pagina dati zero.
Un verifier distinto `xt_verify_tables` controlla ownership, source e tutte
le tabelle, senza attestare il contenuto della pagina dati. La verifica
installazione aggiunge confronto integrale di codice/dati e pacchetto atteso.

Fallimento dopo una scrittura: nessun rilascio automatico. Prima del rollback
si devono azzerare entrambe le pagine e rileggere gli zeri; un errore conserva
l'owner sporco per diagnosi. Solo il successo ripristina `xt_verify` e consente
il cleanup esistente. Non si aggiunge una scrittura consentita dopo freeze.

## Profilo e prove previste

Solo diagnostico: costruzione record, installazione/readback, corruzione
rifiutata su host, cancellazione verificata e budget invariato. Una seconda
immagine diagnostica conserva il contenuto installato per ispezione fisica
dal monitor. Codice RO/X e dati RW/NX nella root inattiva; CR3 BSP invariato.

Host: reali allocator/lease e builder C/Rust, I/O fisico modellato, failure
injection per ogni trasferimento, retry cleanup, alias, token/range, owner
copiato, freeze e accounting. ASan sul C e cppcheck separati dalle prove guest.
QEMU UEFI: dieci boot finali del diagnostico positivo, rollback e regressione
del profilo ordinario. Risultati finali riportati sotto.

## Risultati finali, revisione v3

- Host: kernel/build/k3i-install-host-v3/ e relativo .log. Builder e validator
  Rust reali, C con MSVC /W4 /WX e ASan; 26 failure point d'installazione,
  22 di cancellazione, tutte le 8192 corruzioni dei byte fisici, alias,
  owner copiato, BSP/IRQ errati, registry freeze e alterazione State.
  Ogni ciclo reversibile termina col budget iniziale ripristinato usando
  l'allocatore reale su RAM modellata. Il caso freeze trattiene intenzionalmente
  sei pagine pinned in un processo separato, non viene descritto come cleanup.
- Regressione transizione: kernel/build/k3i-transition-regression-v3/ e .log.
  Due test Rust in debug/release, lifecycle con 13 errori I/O e freeze,
  ASan C e cppcheck senza errori bloccanti.
- Installato inattivo: kernel/build/ap-transition-firmware-eea47a2fc182432987bf1d05a3e9e431/,
  10/10 boot UEFI/OVMF consecutivi, immagine k3i-install-image-v3/kernel.elf.
  Ispezione fisica delle due pagine completa, record legato alle lease e
  all'identità AP preparata, fase/errore bassi zero, tabelle e CR3 BSP invariati.
- Cancellazione: kernel/build/ap-transition-firmware-1c455e37fc3a4223add02e82f3eefa6c/,
  10/10 boot consecutivi, immagine k3i-install-erase-v3/kernel.elf. Codice,
  dati e intero owner sono zero; budget e lease pinned iniziali conservati.
  Non equivale a liberare le risorse AP predisposte dal normale bootstrap.
- Ordinario: kernel/build/percpu-firmware-f2d17be598924d9ea0dec1a4f3b337c7/,
  10/10 boot consecutivi, immagine k3i-install-ordinary-v3/kernel.elf;
  alterna uno/quattro vCPU e 128/256 MiB, controllando contesto BSP e risorse
  AP predisposte. Nessun codice/template del diagnostico installazione
  compare nella mappa dell'immagine ordinaria.
- Backend/probe: kernel/build/k3i-install-static-v3/, cppcheck senza
  errori bloccanti. Restano segnalazioni style e informative, comprese
  proposte const, readback volatile segnalato come condizione nota e header
  standard non forniti; non si dichiara un report completamente vuoto.

kernel/build/k3i-installation-manifest.json lega immagini, sorgenti e risultati.
Le campagne v1/v2 restano storiche e non sono la prova della revisione finale.
Il primo tentativo ordinario v3 è fallito nel runner per --ordinary omesso:
aveva raggiunto KERNEL_READY senza panic, ma cercava marker di una diversa
immagine diagnostica. La campagna corretta sopra è stata ripetuta da zero.
Alcuni wrapper PowerShell del tool sono rimasti aperti dopo i report finali:
sono stati interrotti soltanto dopo risultati JSON completi, hash verificati
e VM già chiuse dal runner. Il codice di uscita dell'interruzione del wrapper
non è presentato come codice di uscita della suite o come un boot aggiuntivo.

Campagne riproducibili in scripts/test/campaigns.json: percpu-image e
percpu-image-erase. I profili richiedono almeno due identità CPU preparate;
il runner usa quattro vCPU, senza avviare AP del kernel. Il token 520 è
soltanto una fixture numerica: nessuna prenotazione del coordinatore viene
spacciata per un commit reale. Valgrind, leak detector guest, hardware reale
e coverage restano N/D; ASan strumenta il C, non Rust o accessi fisici guest.

## Limiti espliciti

Il blob rimane inattivo e l'entry diagnostica non è un runtime AP. Non si
collega ancora il prefisso alto: la sua continuazione/panic/IST è incompleta.
Nessun invio IPI, commit congiunto, riuso dati fra AP o sigillo eseguibile
definitivo. Il precedente freeze richiede dati zero e resta distinto: non
deve essere usato per fingere il commit dell'immagine installata.

Mapping e permission mantengono il riferimento Intel SDM 253668-078US §4.5/4.6
già verificato nel [protocollo](k3i-protocol.md). Questo blocco non introduce
nuovi accessi a registri CPU, porte o MMIO e non modifica la politica DMA.
