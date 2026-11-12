# Design della sicurezza di sistema

Versione completa precedente in `docs/design/security-entropy.md` e `docs/handoff/security-plan.md`, al commit `efecb8b`.

Le protezioni seguono il confine a cui si applicano. Il kernel valida
lunghezze, intervalli, permessi, overflow e lifetime per dati da ring 3,
firmware e dispositivi. I controlli sugli accessi utente sono descritti
nel [design dei processi](processi.md). Le fonti hardware e i criteri
di verifica seguono [R-003](../review/instructions/security.md).

## Protezioni del kernel

Il percorso ordinario usa paging NX/WP, SMEP e SMAP quando disponibili e
UMIP quando CPUID lo dichiara. La politica SMEP/SMAP decide l'ammissione
di ring 3. Gli stack di sistema e IST hanno guardie nei rispettivi
layout. Lo stack delle syscall ha una lease acquisita in
`x86_process_open` e rilasciata in `x86_process_close`, con quattro
pagine fra due guardie nello slot delle finestre. La raccolta controlla
che le guardie restino non mappate.

La base comune in `lib/security/` comprende bounds, azzeramento,
confronto, health test, SHA-256, HMAC e HMAC_DRBG. Il kernel deve ancora
collegare queste primitive. Le policy di modulo restano vicine ai
chiamanti. La root di processo condivide l'immagine kernel e KPTI
resta una scelta separata.

## Entropia e canary

La decisione del 7 novembre prevede una sorgente CPU con RDSEED
preferito e RDRAND di riserva. Il progetto usa intrinsic Rust con
controllo CPUID e un collegamento C sottile all'avvio. Jitter RDTSC e
tempi degli interrupt entrano come input supplementari senza credito.
L'accreditamento di queste sorgenti richiede stime non-IID su hardware
reale. In assenza di entropia valida l'avvio si ferma con diagnostica.

La proposta software scarta 1024 campioni di avvio, applica RCT e APT
con guasto persistente e richiede almeno 256 bit di min-entropia
accreditata per il seed e 128 separati per il nonce. La soglia dei
test di salute non accredita entropia. In riserva, la proposta usa
1024 campioni RDRAND da 64 bit condizionati con SHA-256. Prevede un
DRBG globale sotto lock e reseed ogni 2^16 richieste. I valori di
proposta e il credito vanno verificati prima del collegamento. Fonti
di riferimento sono Intel DRNG Software Implementation Guide
§3.2–3.3 e §5.2–5.3, Intel SDM Vol. 2 e NIST SP 800-90B §4.3 e §6.3.

Restano da scegliere l'integrazione dello stack canary con la toolchain
freestanding, l'imposizione e il collaudo W^X per sezione, KPTI e le
mitigazioni speculative. Restano aperti anche audit degli input esterni,
collegamento di bounds ai moduli e generalizzazione dell'isolamento DMA.
[Validazione](../validation/sicurezza.md).

## Prove previste per la sorgente

Host con capacità CPU mancanti, sorgente intermittente, valore bloccato
e credito insufficiente anche con health test superati. QEMU con dieci
boot e un marker di sorgente pronta, riserva RDRAND con RDSEED assente,
arresto con diagnostica se mancano entrambe. Per W^X occorre verificare
`.text` RX, `.rdata` RO/NX e dati RW/NX, con un negativo di scrittura
sulla sezione di codice.
