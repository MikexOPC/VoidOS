# R-003 — chiusura della correzione documentale

Data: 5 ottobre 2026. Stato: corretto nei documenti operativi, senza modifiche a codice eseguibile.

## File interessati

I primi tre nomi indicano istruzioni locali del checkpoint.

- Direttive locali, versione 2.1: conservati i sette constraint, corretti il perimetro e gli esempi di sicurezza/test/driver/design.
- `SKILL.md`, versione 1.1: prerequisiti CPU, applicabilita delle protezioni, limiti, build e template datasheet coerenti.
- `REVIEW_INSTRUCTION.md`, versione 1.1: classificazione dei rischi basata su prove, esempi distinti da risultati e analisi CPU/ring/KPTI/DMA corrette.
- [Riferimento verificato](security.md): registri/CPUID, semantica delle protezioni e fonti primarie.

## Controlli completati

- Rimossi i riferimenti operativi a NX come bit di CR0 e la soglia alternativa di 250 righe.
- Verificati registri/bit, leaf CPUID e sezioni Intel: incluso §6.8.8 per SYSCALL/SYSRET, §7.14 per interrupt e Figura 2-7 per control registers.
- Corrette le prescrizioni di attivazione incondizionata delle feature opzionali e il trattamento KPTI come difesa universale contro Spectre.
- Eliminati salvataggio interrupt in C, near CALL identificata automaticamente come ring crossing e copie utente presentate come complete con solo range+memcpy.
- Rimossi snippet 82599 senza registri/sequenze verificati e fix di lock che saltava la reacquisizione senza ownership.
- Conservati i gate di test per codice eseguibile; test documentali e runtime sono distinti. Nessun test PASS o dato di coverage/leak inventato.
- Controllati link locali, delimitatori Markdown bilanciati, whitespace e diff. Sette constraint presenti dopo la correzione.
- Nessuna modifica a bootloader, kernel, driver o test eseguibili durante R-003. Build/QEMU/ASan/leak test: N/A per questa task documentale, non nuovi successi di runtime.

## Provenienza e limiti

Le copie dei tre documenti prima di R-003 e gli script temporanei della correzione sono conservati in kernel/build/r003-guidance/, fra gli artefatti ignorati. Le modifiche personali precedenti restano incluse nello snapshot: non e stato usato il reset a HEAD per sostituirle.

La revisione qui riguarda l'accuratezza delle istruzioni: non implementa SMEP/SMAP/KPTI, non progetta syscall o driver e non certifica il progetto. I dati illustrativi nei template restano marcati come esempi, non misure. La full review precedente rimane storica; le sue linee/ID non vengono riscritti come se i fix fossero gia presenti nello snapshot originale.

Prossima task distinta: R-001, disponibilita dell'attesa timer quando manca l'IRQ, con ricerca/design della source indipendente prima di scrivere nuovi accessi hardware.
