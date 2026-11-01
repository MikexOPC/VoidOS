# K3-A — validazione del contesto BSP tramite GS

29 ottobre 2026. Modulo: `arch/x86/kernel/cpu/percpu/` con i soli
adattatori provider, bootstrap, build e test concordati. Scelta utente:
passaggio graduale dopo GDT, con tutti i lock rilasciati.

## Implementazione e review

Il bootstrap mantiene il contesto early fino alla GDT e al ritiro dello
stack iniziale. La promozione richiede BSP, prerequisiti CPU, appartenenza
all’immagine kernel, contesto valido e quiescente. Preserva NextToken e
il binding Wait; installa e verifica IA32_GS_BASE, azzera il contesto
ritirato e attiva il nuovo provider. IF viene conservato. Dopo una scrittura
GS non verificabile il fail-stop conserva l’owner preparato, senza retry
o fallback. Il reader confronta la base con l’owner conosciuto prima di
leggere GS:[0]. Gli AP sono esclusi prima della lettura dello stato C.

La review ha aggiunto il rifiuto di Clock.Context dentro il contesto
ritirato o il nuovo owner. Per oggetti esterni, lifetime e dimensione del
puntatore opaco restano responsabilità del chiamante. Il contatore dei
token non viene azzerato neppure all’esaurimento. Il getter protegge la
validazione del contesto con IRQ locali disabilitati.

Il contenitore statico ha 64 record allineati a 64 byte, 448 byte ciascuno:
28 KiB BSS persistenti, senza allocazioni dinamiche. Solo slot zero è attivo;
indice logico zero non significa APIC ID zero. Nessun AP avviato, SWAPGS,
FSGSBASE o protocollo user mode introdotto. Fonti e sezioni Intel, MSR e
alternative sono nel [design](k3-design.md) e nel backend Assembly.

## Test host finali

- `kernel/build/test-run-4920c69028d4412d8ef27217a87ab36d/percpu.log`:
  50 esecuzioni (10 ripetizioni di cinque modalità), tutte passate.
- Provider e primitiva reali, operazioni privilegiate simulate; transizioni
  IF0/IF1, alias, lifetime, doppia inizializzazione, contesto ritirato,
  token esaurito, owner/base/Self corrotti, AP escluso e fallimenti post-write.
  I due arresti attesi usano longjmp solo sull’host.
- MSVC /W4 /WX e ASan, atomiche Rust reali; cppcheck senza diagnostiche
  bloccanti sui sorgenti analizzati dalla suite.
- Regressione sync: `kernel/build/test-run-56e90f1e63644415b16b7518868892eb`.
- Regressione bootstrap: `kernel/build/test-run-f2af482d2ed94918915dd1fe48ae52ac`.

ASan riguarda il C host strumentato, non Assembly privilegiato o leak di
RAM fisica. Valgrind non eseguito su Windows; nessuna affermazione di leak
zero dell’intero OS. Coverage linee/branch non misurata.

## QEMU finale

QEMU 11.1.0, q35/TCG/OVMF, 128/256 MiB, alternanza di una e quattro vCPU
configurate. In tutti i casi un solo contesto OS pronto: BSP.

- Diagnostico: **10/10 consecutivi**, immagine finale e copie ESP verificate.
  Evidenze: `kernel/build/percpu-firmware-5cae2ecfd3e84971a40367b9c7968655/results.json`.
  ELF SHA-256: `F4C3C419006F8451BFD18537A8C9F2742FA8487687316EFB26CCF0041A71A293`.
- Ordinario: **10/10 consecutivi**, senza probe sync diagnostico.
  Evidenze: `kernel/build/percpu-firmware-6bb6fdd5fdf54ee2bb73f8273acc28ba/results.json`.
  ELF SHA-256: `4432E5EAD5A45FC223DB8FF876A1D949D6AEC3A7C8A43C88934E7EB853BF42A9`.
- Negativi: **2/2 attesi**, sync-wait e sync-irq, evidenze
  `kernel/build/qemu-run-ee393c5e7bc645c6ad87484593559392/results.json`.

Il runner arresta la propria VM dopo KERNEL_READY e legge registro GS e
memoria virtuale dal monitor: base uguale all’owner, offset Self/Locks,
owner ACTIVE, indice zero, vecchi 416 byte azzerati, lock utilizzabili,
nessun guard residuo. Verifica anche i due controlli lower-half e budget
heap. Hash del loader comune: `B6C055AB17CC5CA1E0F017A35E4E9722273EB7B49CB0191F5E759CA84CC23AA1`.

La prima esecuzione del nuovo harness falliva perché chiedeva una sola
stampa KERNEL_LOWER_HALF_EMPTY; il kernel ne emette legittimamente due.
La VM aveva raggiunto KERNEL_READY senza panic. Corretto il conteggio nel
runner, mantenendo unici i marker di promozione, si è ripetuta l’intera
campagna finale di dieci boot. Non è un crash kernel corretto né un PASS
ricavato da un’immagine precedente.

## Regressione console e nota sull’esecuzione

Console VirtIO/VT-d: **1/1 PASS**, con input, pixel/puntatore, cleanup e
budget verificati dal runner interattivo esistente. Evidenze:
`kernel/build/qemu-run-b369823cddbc495d99db668e04e4846b/results.json` e
`kernel/build/pointer-interactive-check-96f08a2f5af544ae942972f666e74755/`.
Questa è una regressione breve del profilo video, non dieci sessioni video.

Durante il controllo è stato lanciato per errore un secondo runner mentre
il primo era ancora attivo: il controllo processi non elevato non mostrava
la VM. La sola VM duplicata è stata identificata dal suo percorso univoco
e terminata. Il relativo FAIL in `kernel/build/k3a-video-qemu-final.log`
è un’interruzione intenzionale, non un panic kernel e non viene contato
come test passato. Il primo runner ha completato con PASS; la sovrapposizione
impedisce di usare questa sessione come misura delle prestazioni.

## Limiti e seguito

I nuovi sorgenti/test hanno massimo 121 righe; cinque sorgenti diretti nel
modulo, nessuna cartella funzionale nuova oltre cinque. Manifest con hash:
`kernel/build/k3a-source-review.json`. Nessun codice copiato da Linux.

K3-A prepara il provider BSP; **K3 non è completo**. Restano clock di
contesa e risorse per CPU, ingresso AP, avvio e prova di core realmente
operativi. Niente contesa SMP guest o hardware fisico verificati. I percorsi
NMI/fault non possono usare questo accessor. Mapper e lifecycle mantengono
i vincoli BSP già dichiarati da K2.
