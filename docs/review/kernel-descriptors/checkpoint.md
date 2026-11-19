# Revisione GDT, TSS e IST kernel — 3 ottobre 2026

Checkpoint del modulo `arch/x86/kernel/descriptors/`, con le sole dipendenze
necessarie: IDT, allocatore fisico, accesso al ramo paging e build/test kernel.
Il loader EFI resta invariato. Non e una revisione globale dell'OS.

## Risultato verificato

Il kernel carica la propria GDT e TSS, conserva dodici pagine fisiche pin-nate
e usa tre IST da 16 KiB con quattro pagine guardia virtuali. NMI e double fault
raggiungono rispettivamente IST2 e IST3. Le altre eccezioni usano IST1.
Le librerie GDT/TSS gia presenti vengono riutilizzate; Rust valida geometria
e limiti fisici. Lo stack principale resta un prestito del loader.

## Controlli e correzioni durante il ciclo

- Aggiunti i percorsi header standard MSVC/UCRT, mantenendo il link senza CRT.
- Reso esplicito il padding dello storage, con assert su dimensione,
  allineamento e offset di TSS/operando GDTR; nessuna struttura packed.
- Rifiutati owner non allineati, intervalli fuori dal profilo canonico,
  alias tra owner/pool e sovrapposizione con la regione IST, prima degli accessi.
- Verificati root, slot ricorsivo e parent prima di raggiungere le foglie.
  Nessuna adozione o cancellazione di PTE estranee; guardie esattamente zero.
- Pin prima delle PTE, zeroing prima di LTR, batch completo prima di INVLPG;
  rollback con unmap e invalidazioni prima di unpin/rilascio.
- Commit prima di LGDT/LTR: nessun rilascio dello storage raggiungibile dalla
  CPU nei percorsi di errore successivi. L'arresto mantiene l'owner vivo.
- Busy verificato esattamente; la normalizzazione per riusare il validatore
  condiviso avviene solo su copie locali, dopo il confronto dei campi aggiunti.
- Selezione IST2/IST3 nella IDT soltanto dopo verifica della TSS caricata.
- Test QEMU aggiornati ai nuovi stack, senza riusare bounds del vecchio IST.
  Aggiunti controlli indipendenti su GDTR, base/limite della cache TR, busy
  in memoria, sedici PTE, quattro guardie e frame reali.
- Fault double disponibile soltanto nella variante di test; log INFO esplicito.
  Non viene assunto un RIP definito per il frame di double fault.

## Evidenze

Build MSVC C11 `/W4 /WX`, MASM `/W3 /WX` e Rust `-D warnings` superate;
ELF generato senza dipendenze CRT. Test host completi superati, inclusi:

- 2.464 alterazioni di singoli bit GDT/TSS/GDTR negli stati available e busy;
- 176 stati non vergini respinti senza scritture e 256 byte tipo TSS verificati;
- 1.000 corruzioni delle sedici PTE respinte, con 24 alterazioni A/D ammesse;
- sedici mapping estranei respinti, parent con permessi/flag errati respinti;
- rollback su ciascuno dei tre buffer assenti, alias, owner copiato, OOM;
- 256 cicli allocate/zero/unmap/free senza perdita del budget di 32 pagine;
- regressione contratto, pool fisico, finestra, oggetti e IDT;
- 18 test Rust complessivi del kernel, tre specifici del layout IST.

QEMU `11.1.0 (v11.1.0-12130-ge470268ff4)`, q35/TCG/CPU max, EDK2 UEFI:
**14/14 scenari** superati sullo stato finale. Avvio a 128/256 MiB, panic,
NX, unmap, due guardie della finestra, UD, GP, DE, push sulla guardia dello
stack principale, fault annidato, DF e NMI dal monitor. Ogni trace
`-d int,cpu_reset` e stato controllato: nessun triple fault.

Log finale locale ignorato da Git:
`kernel/build/firmware-09adce8b983a495db54fdee6102d16e2/`.
Log host: `kernel/build/descriptors-host.log`.

Il kernel normale compilato, installato e copiato nel test ready coincide:
SHA256 `ECC691AFD10200408B990E06F7048F62AAE172717EC7A3ED880F0117E031645C`.
EFI invariato:
SHA256 `07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3`.
ELF: 45.056 byte su disco, 49.152 byte nei segmenti arrotondati a pagine;
ulteriori 49.152 byte fisici persistenti per gli IST. Nessuna nuova tabella paging.

## Limiti rimasti

Questo e un bootstrap UP con IF zero e diagnostica terminale. Non ci sono
handler recuperabili, scheduler, IRQ, SMP o prova su hardware fisico.
L'allocazione richiede dodici pagine contigue; OOM viene gestito arrestando
il bootstrap. Il rilascio dopo commit non e implementato intenzionalmente.
Un'invariante corrotta puo trattenere risorse per evitare di liberare pagine
ancora in uso: non si dichiara recupero da corruzione arbitraria.

NMI e DF sono provati come singoli ingressi terminali. La guardia globale
blocca il rientro in C, ma non offre isolamento per CPU o recupero dell'IST.
Lo stack principale, le tabelle paging e altre pagine del loader restano
riservati. La prossima rimozione di una dipendenza riguarda lo stack principale,
prima di pianificare interrupt e scheduling del kernel ibrido.

Struttura: cinque file diretti nel modulo, Rust/Assembly in sottocartelle
funzionali; moduli C principali 200/201 righe, test C 203/232.
Header e adattatori compatti secondo la deroga esplicita dell'utente.
Modifiche preesistenti a `.gitignore`, `SKILL.md`, `.idea/` e `.vs/`
escluse dal checkpoint Git.

Le fonti ufficiali e la procedura ripetibile sono in
[ownership e geometria](../../kernel/descriptors/ownership.md).
