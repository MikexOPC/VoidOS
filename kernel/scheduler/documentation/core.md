# Scheduler — core Rust e gateway C

Snapshot storico del primo blocco host (stati, coda, gateway FFI).
Stato corrente: [docs/status.md](../../../docs/status.md). Stack, mailbox
e switch nativo appartengono ai report successivi, a partire da
[integrazione v0](../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).

Blocco dell'8 ottobre 2026, successivo al [progetto](design.md).
Perimetro di questo file: core stati/coda, gateway FFI e suite host.
Nessuna modifica a kernel main, paging, timer o Assembly in quel blocco.
Le prove sotto non dimostrano lo scheduler eseguibile completo.

## Implementazione

- [Owner Rust](../rust/state/owner.rs): otto slot, handle indice/generazione,
  reserve/publish/abort/select/yield/exit/collect, query e invarianti.
- [Coda Rust](../rust/state/queue.rs): FIFO circolare a capacita fissa;
  membership, duplicati, range e celle inattive verificati.
- [Gateway](../rust/ffi/gateway.rs) e [header C](../scheduler.h): owner statico
  non accessibile tramite pointer C, reply scalare repr(C) di 32 byte,
  assert C e Rust di ogni offset e allineamento, INIT ammesso una sola volta.

Il core non contiene unsafe, raw pointer o heap. Owner non espone Copy/Clone:
il candidato di transazione e una copia privata per rollback. Un rifiuto
lascia lo stato immutato; RETIRED e l'unico esito non-successo che pubblica una
transizione, prima di un eventuale wrap della generazione. Se il contatore
eventi e esaurito, anche il ritiro viene rifiutato senza mutazioni.

Metadata Owner: 208 byte sulla toolchain host x64 verificata, esclusi storage
gateway/guard e risorse native future. La coda ha otto elementi e nessuna
allocazione dinamica; i test usano VecDeque soltanto nel modello indipendente.
Ogni transizione valida lo stato precedente e il candidato. Questi controlli
sono O(N^2), limitati a otto slot: nessun benchmark di scheduler e dichiarato.

Il gateway serializza l'accesso con compare_exchange Acquire e rilascio Release.
Il callback di contesto avviene dopo acquisizione ma prima di creare riferimenti
allo storage; un rientro ritorna BUSY senza callback o borrow del proprietario.
Il riferimento interno termina nella medesima invocazione: non ci sono callback
durante il prestito. Gli unsafe del gateway documentano ABI del backend, accesso
esclusivo allo storage e percorso terminale di panic; panic=abort, nessun unwind.

## Contratto nativo ancora aperto

`kernel_scheduler_context_valid()` e `kernel_scheduler_fail_stop()` sono
simboli del backend richiesti, non implementazioni kernel gia presenti.
Il primo dovra verificare BSP, IF=0, DF=0 e chiamante dispatcher fuori IRQ/NMI;
il secondo non deve tornare. Nei test C sono mock e arresto del processo host.
Il gateway richiede risposta normalizzata esattamente 1; non legge registri CPU.
L'atomic guard non dimostra supporto dello scheduler SMP o preemption.

PUBLISH richiede che il caller abbia verificato stack/entry/mapping nativi;
ABORT e COLLECT richiedono rollback/cleanup gia verificato sul dispatcher.
Rust non possiede ancora le lease C e non puo dimostrare il loro rilascio.
La query puo riportare FREE/RETIRED per una generazione ancora corrispondente,
ma non autorizza mutazioni o risorse di un nuovo task. Nessuna API reset o
seeding delle generazioni e esportata per aggirare questo contratto.

## Verifica eseguita sullo stato finale

```powershell
& kernel/scheduler/scripts/test.ps1
# Se cppcheck non e nel PATH o nella cache della review locale:
& kernel/scheduler/scripts/test.ps1 -CppcheckPath 'percorso/cppcheck.exe'
```

Clippy deve poter leggere la sua configurazione: nel sandbox Windows e stato
necessario eseguire gli strumenti host con escalation approvata automaticamente.
Lo script ripristina INCLUDE/LIB/PATH/CLIPPY_CONF_DIR, salva log degli argomenti
e manifest hash e scrive soltanto artefatti in kernel/build.

Evidenza finale: `kernel/build/scheduler-host-3a9108bdf94f4fd29019361108f239b6/`.

| Prova | Esito effettivo |
|---|---|
| Rust 1.98.1, debug/release, -D warnings, overflow checks | 13/13 test distinti PASS in ciascuna build |
| Modello indipendente VecDeque | 64 seed fissi, 320.000 comandi confrontati per build |
| Matrice stati/operazioni | 6 stati x 8 operazioni, confrontati con modello |
| FIFO/stress | 20.000 select/yield nel core e 20.000 rotazioni della coda per build |
| Confini | Handle invalidi/stale, capacita, generazione MAX, eventi MAX in tutte le mutazioni, corruzioni: PASS |
| Clippy core e gateway, -D warnings | PASS, zero warning |
| ABI C/Rust MSVC /W4 /WX /O2 e ASan C | 486.644 check PASS, 80.000 coppie select/yield |
| ABI/rientro | Layout 32 byte e offset, bit alti della generazione, indice MAX, INIT ripetuto, BUSY e contesto mock negato: PASS |
| Cppcheck 2.22 sul test C | Zero error/warning/performance/portability; due style, cinque information |

Le due style unusedFunction riguardano context_valid e fail_stop chiamati
tramite simboli Rust; il perimetro cppcheck e una singola unita C e non vede il
call graph Rust. Information: quattro include standard e checkersReport. Nessuna
soppressione aggiunta per far apparire il report privo di segnalazioni.

Il primo tentativo di link aveva passato la staticlib a cl con /TC, che la
interpretava come sorgente: corretta la posizione dopo /link. Corretto anche
un errore di continuazione degli argomenti PowerShell aggiungendo un array.
Clippy ha fatto aggiungere Default dell'owner e fill nel test. L'ultima suite
completa e stata eseguita dopo questi fix e il caso ABI con bit alti u64;
i tentativi precedenti non sostituiscono i risultati finali.

ASan strumenta la fixture C, non automaticamente gli oggetti Rust o futuro
Assembly. Core e gateway non allocano heap, ma non e una prova leak della RAM
fisica o delle future lease. Valgrind non eseguito; coverage linee/branch N/D.
Nessun boot QEMU dello scheduler, switch stack, test guard/NX, IRQ o prova su
hardware fisico eseguito. Gli 82 profili watchdog non sono prove di questo core.

## Review e gate successivo

Review: dimensioni, whitespace, limiti di cartella, parser PowerShell, link,
invarianti, immutabilita dei rifiuti, lifetime del borrow FFI e confini ABI.
Controllo finale eseguito: dieci sorgenti/config, massimo sorgente 231 righe,
massimo cinque file diretti per cartella, 24 link locali validi, zero trailing
whitespace/tab o separatori senza riga vuota; dieci hash del manifest finale
corrispondenti. Parser PowerShell e git diff --check PASS.
In questo blocco il core e testato host; il modulo eseguibile non era
ancora dichiarato READY. L'immagine kernel ordinaria di allora non
includeva la staticlib. Il collegamento nativo e nei report successivi.

Verificati entrambi gli ELF ordinari, kernel/build/kernel.elf e copia ESP:
SHA256 `ED0D57A4367272D5AC1EFD5687EE4451E0EED1A692DED01B7B8AA177DBD9D286`.
Nessun commit/push aggiuntivo in questo blocco; il diff resta circoscritto allo
scheduler. Il commit del progetto precedente rimane `d1d7005`.

Il passo successivo previsto allora era l'adapter x86 e il dispatcher.
Quel lavoro e nei report nativi successivi, non in questo snapshot.
