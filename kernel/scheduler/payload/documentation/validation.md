# Coda payload IRQ: implementazione e verifica

Task iniziata il 2026-10-08; verifica finale il 2026-10-09.
Scope: scheduler, backend x86, interfacce e test/script del modulo.
[Progetto e ricerca](design.md). Politica scelta dall'utente: rifiuto dei nuovi
byte e perdita osservabile, 256 byte utilizzabili, un consumer per canale.

## Implementazione e review

- `rust/payload/bytes.rs`: FIFO bounded, nessun heap, cella consumata azzerata,
  contatore perdite u32 saturante. LOSS viene dopo tutti i dati precedenti;
  durante la barriera si rifiutano anche push con uno slot libero.
- `rust/payload/channel.rs`: consumer designato e generazione della task
  verificata dal backend; nessuna cessione automatica a una task ricreata.
  Close richiede coda vuota e perdita gia osservata, poi azzera lo stato.
- `rust/ffi/payload.rs`: ABI scalare senza puntatori buffer, stesso guard del
  registro binding. Callback prima del borrow, IRQ senza Owner/heap/callback.
  Bounds/source/token precedono ogni indice; operazioni rientranti rifiutate.
- `payload.h`: risultati distinti per DATA, EMPTY, LOSS, DENIED, BUSY;
  push distingue byte accettato, perdita registrata e richiesta rifiutata.
- Backend: open prima dell'arm, pop dalla task corrente con indice/generazione
  autenticati, hardware quiescente prima di close/unbind. Raw post rifiutato
  mentre il payload e abilitato; unbind non elimina implicitamente un canale.
- Tre IRQ LAPIC consegnano byte diagnostici: uno durante una task IF1, due
  durante HLT. Event3 viene riutilizzato con un nuovo token. Il secondo waiter
  continua a svegliarsi ma il suo pop viene rifiutato.

Push/pop sono O(1), con capacita fissa. Budget verificato da assert compilato:
ogni Channel occupa al massimo 288 byte, quindi otto Channel al massimo 2304
byte, oltre a registro/guard/pending gia presenti. Nessuna nuova allocazione.
Non e un benchmark di latenza IRQ o di throughput di un device input.

Un errore intermedio del test Rust (range inferito i32 passato a u64::from)
e stato corretto usando un range u32; la prima esecuzione fallita non e PASS.
Il boot smoke ha preceduto il limite statico finale; i dieci boot sotto usano
la build finale e il relativo hash. Nessuna modifica ai sorgenti dopo la
fotografia finale delle fonti.

## Prove host finali

| Prova | Risultato | Perimetro |
| --- | --- | --- |
| Queue/channel Rust | 5/5 debug e 5/5 release | FIFO, 256 slot, overflow, LOSS, reset, ownership, saturazione |
| Modello di riferimento | 500.000 operazioni per profilo | Confronto con VecDeque e modello perdita, incluso nei 5 test |
| Owner/pending Rust | 28/28 per profilo | Regressione scheduler |
| Registro Rust | 3/3 per profilo | Regressione source/token |
| Payload C/Rust | 523.081 controlli | Rust reale; CPU/device mock; C strumentato ASan |
| Altre FFI | 545.700 gateway, 110.016 binding | Libreria reale, contesto mock |
| Engine | 69.078 controlli in nove profili | Owner Rust reale, backend mock |
| Compiler/Clippy | PASS, warning come errori | MSVC e Rust 1.98.1, ABI supportata |
| Backend | Dieci ripetizioni dei profili host PASS | Switch Windows reale; privilegi/device mock |
| Cppcheck | Tre fixture core e 24 input backend | Zero error/warning/performance/portability |

[Log core](D:/OS/kernel/build/scheduler-payload-core-final.log), evidenze
`kernel/build/scheduler-host-c7a22410489544358513cae0ace56336/`.
[Log backend](D:/OS/kernel/build/scheduler-payload-backend.log), evidenze
`kernel/build/scheduler-context-ec9dacd2e2504df78f45edd1ad145e55/`;
i relativi hash sono stati confrontati con i sorgenti finali, invariati.
Gli XML conservano i messaggi style/information: callback chiamati da Rust non
risolti da cppcheck e include standard mancanti nel modello di analisi.
Non viene dichiarato zero messaggi cppcheck di ogni categoria.

ASan strumenta C, non Rust staticlib/Assembly. Nessun heap nella coda;
accounting RAM fisica distinto nelle prove firmware. Valgrind non eseguito,
coverage linee/branch N/D; non promettiamo zero leak per tutto l'OS.

## Prove firmware finali

[Immagine finale](D:/OS/kernel/build/scheduler-payload-verified/kernel.elf):
SHA256 `449F0ED0224926E413D1950B2ECC9B356021FB1D1416654D83A2DA6F27B765F5`.

Dieci boot consecutivi PASS, q35/TCG/max, 128 MiB, un BSP, OVMF/ESP privata.
Marker `KERNEL_SCHEDULER_PAYLOAD_OK` richiesto insieme a IRQ/binding/idle;
verificati contesti, CR3/RSP/RAX, budget restituito e 144 PTE task assenti.
[Log positivo](D:/OS/kernel/build/scheduler-payload-positive.log), evidenze
`kernel/build/scheduler-firmware-4d22a8bed6824090b21b3a1655838426/`.

Nove regressioni negative PASS, un boot isolato ciascuna: guard-low, guard-high,
NX, retired, rollback, blocked, irq-timeout, idle-timeout e idle-repeat-timeout.
[Risultati/hash delle dieci immagini](D:/OS/kernel/build/scheduler-payload-results.json),
[directory delle evidenze negative](D:/OS/kernel/build/scheduler-payload-review/negative-evidence.json)
e [log suite](D:/OS/kernel/build/scheduler-payload-firmware-suite.log).
I timeout conservano le risorse necessarie al fail-stop previsto; non
dimostrano recovery automatico. I fault intenzionali non sono crash inattesi.

Loop continuo PASS, 17 eventi effettivi, soglia host nove eventi, stop esterno
e cleanup verificati; la differenza deriva dall'osservazione asincrona dello
stop. [Log loop](D:/OS/kernel/build/scheduler-payload-loop.log), evidenze
`kernel/build/loop-firmware-1efd8214f92e4284b4b20f0fd25d08f6/`.
L'immagine EventLoop e separata da quella dei dieci boot positivi.
SHA256 EventLoop `8EC6AD07C99B7DABB7077136CACC040AEAA13E5FD06DB8411364C36931EB1E16`;
marker payload verificato anche nel suo log seriale. Il loop e una regressione
del runtime dopo le tre sessioni payload, non un carico continuo di input.

## Limiti e prossimo passo

[Manifest finale](D:/OS/kernel/build/scheduler-payload-review/sources.json):
72 sorgenti/script, max 388 righe, max cinque sorgenti diretti per cartella;
controllo whitespace e hash, ricontrollati dopo la suite finale. I 461 file di reuse/ coincidono con la fotografia
precedente e non sono stati modificati. Nessun nuovo registro, I/O o Assembly:
si riusano driver e primitive CPU gia documentati.

Profilo BSP/IF0 per le operazioni sulla coda; nessuna prova SMP, NMI concorrente,
ring3 o hardware fisico. Il guard e non bloccante: BUSY/rifiuto deve essere
gestito dal chiamante; non certifica una coda lock-free multiprocessore.
Source/token e consumer sono autorita interne a kernel fidato, non un confine
di sicurezza contro codice ring0 ostile.

Il test consegna un byte diagnostico LAPIC, non uno scancode PS/2 o un report
USB. Decoder, discovery e driver input reali restano da progettare. Il loro
consumer dovra applicare OBSERVE/ricontrollo dati/WAIT e risincronizzarsi su
LOSS; questa task prepara e verifica la coda senza dichiarare quei driver pronti.
