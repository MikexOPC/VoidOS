# K4-B — Invalidazione dell'intero contesto TLB

## Decisione

Il BSP resta l'unico autore delle modifiche di paging. Il gruppo CPU
ammesso è fisso e condivide una root. La prima integrazione è diagnostica:
una finestra dedicata, due frame con lease trattenute e accessi AP
consentiti solo nelle fasi previste dal test.

Ogni CPU ricarica il proprio CR3 senza cambiare root. Il profilo
richiede CR4.PGE (bit 7) e CR4.PCIDE (bit 17) a zero su tutte le CPU.
Il BSP verifica i prerequisiti prima della modifica. Ogni AP li ricontrolla
prima del flush. Il servizio non modifica CR4. Il costo è la perdita
anche delle traduzioni utili, da misurare prima dell'uso ordinario.

## Protocollo

1. Il BSP verifica root, gruppo, ownership e disponibilità di tutte
   le mailbox. Gli AP terminano gli accessi alla finestra di prova.
2. Il BSP modifica la PTE, ricarica CR3 e pubblica con release la richiesta
   di flush e la sua generazione. Nessun indirizzo virtuale nel payload.
3. Ciascun AP acquisisce la richiesta, verifica identità, root, modalità
   e generazione, ricarica CR3, completa EOI e pubblica l'ACK.
   L'handler non usa lock o allocazioni.
4. Il BSP attende gli ACK della generazione esatta, con deadline ACPI PM
   e numero massimo di controlli. Solo allora rende visibile la nuova
   generazione del paging e autorizza il rilascio del frame precedente.
   Se la modifica è già avvenuta e una verifica fallisce, panic
   coordinato con risorse trattenute.

Il primo test rimappa una foglia da 4 KiB e conserva le tabelle
intermedie. Il flush non sostituisce le verifiche di ownership, durata
degli accessi e alias. Il collegamento ai percorsi ordinari sarà verificato
separatamente prima di autorizzare il riuso delle loro pagine.

## Accettazione

Scaldare la vecchia traduzione su ogni AP, rimappare la stessa VA e
verificare che tutti leggano il nuovo contenuto dopo gli ACK. Verificare
rifiuto di modalità incompatibili, ACK assente e generazione errata,
con risorse trattenute dopo la modifica. Suite host per ordine, limiti e
timeout. Dieci boot diagnostici consecutivi e regressione del boot ordinario.

## Fonti

- [Intel SDM Vol. 3A, §§4.10.4–4.10.5, rev. 078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf)
  per gli effetti del reload di CR3 e la propagazione tra CPU.
- [Linux v6.12, arch/x86/mm/tlb.c](https://github.com/torvalds/linux/blob/v6.12/arch/x86/mm/tlb.c)
  come riferimento per la distinzione fra flush di intervallo e contesto.

## Finestra della prova

Il profilo IPI usa in esclusiva la page window esistente. Il BSP prepara
due lease distinti, parcheggia il primo frame letto dagli AP e rimappa
la stessa VA sul secondo. Le invalidazioni locali della finestra fanno
parte dell'API esistente; la barriera che autorizza il rilascio del
vecchio frame è invece l'ACK del flush remoto, seguito dalla rilettura
su tutti gli AP. Il secondo frame resta pinned fino all'arresto
diagnostico. Il paging ordinario richiederà una barriera propria prima
di pubblicare la generazione condivisa.
