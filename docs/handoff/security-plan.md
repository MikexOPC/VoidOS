# Piano sicurezza dei sottosistemi

Stato: avviato. Primitive comuni pronte ma non ancora usate dal kernel;
user mode, syscall e user copy esistono con SMEP/SMAP obbligatori; mancano
sorgente di entropia, stack canary e UMIP. Stato riallineato al codice il
2026-11-12 (vedi "Classifica"). Questo file e il punto di partenza per chi
lavora sulla sicurezza; va aggiornato a ogni passo.

## Ordine concordato — 6 novembre 2026

Decisione dell'utente: completare prima i punti K, poi affrontare il
consolidamento iniziale della sicurezza e infine la GUI.

1. K4-C5: driver sicuri su più CPU; K4-C6: integrazione e chiusura K4.
2. K5: syscall, confini di privilegio e user copy; K6: primo programma
   utente ELF e gestione dei suoi fault.
3. Review e consolidamento iniziale della sicurezza descritti qui.
4. GUI, dopo i relativi prerequisiti di input, output e servizi.

Questa sequenza rinvia la campagna dedicata, non i controlli necessari
alle implementazioni. Ownership, bounds, sincronizzazione e transizioni
di privilegio devono essere verificati nei rispettivi punti K. Le
protezioni richieste dal profilo userspace vanno implementate e provate
con K5/K6, prima di eseguire programmi utente; non aspettano la review.
La classifica sotto resta una prima stesura da rivalutare dopo i punti K.

## Obiettivo

Ogni sottosistema deve essere sicuro. L'impegno e proporzionale a quanto il
sottosistema e grande ed esposto: memoria, interrupt, SMP, driver con DMA e
futuri syscall/user mode vengono prima di console o layout.

## Regole di lavoro

1. **Decidere prima cosa mettere in sicurezza.** Leggere documentazione e
   sorgenti del progetto, poi scrivere una classifica in questo file:
   sottosistema, rischio, motivo, priorita.
2. **Un sottosistema alla volta** (direttive-locali, Constraint 1). Non si apre il
   successivo finche quello corrente non ha test e documentazione chiusi.
3. **Per ogni sottosistema:** leggere → decidere → agire → testare →
   documentare → commit.
4. **Fonti obbligatorie** per ogni accesso hardware o protezione CPU
   (Constraint 4) e riferimento verificato
   [R-003](../review/instructions/security.md).
5. **Niente protezioni dichiarate senza prova.** SMEP, SMAP, KPTI, CET ecc.
   si dichiarano solo se abilitate, verificate via CPUID e testate in QEMU.
6. **Sicurezza in Rust.** Primitive, controlli e decisioni di sicurezza si
   scrivono in Rust; C/asm solo dove serve la CPU.
7. **Su misura.** Si studiano le soluzioni esistenti (Linux, seL4, Redox,
   ecc.) ma le protezioni si progettano per questo kernel, per averne il
   pieno controllo. Eccezione: gli algoritmi crittografici non si inventano;
   si implementano algoritmi standard e verificati, testati con i vettori
   ufficiali.

## Dove va il codice

| Livello | Cartella | Contenuto |
|---|---|---|
| Comune | `lib/security/<funzione>/` | Primitive indipendenti dall'architettura: bounds e aritmetica controllata, validazione range/indici, confronto a tempo costante, azzeramento sicuro, canary, generazione/handle |
| Architettura | `arch/x86/security/<funzione>/` | Protezioni della CPU: NX/XD, WP, SMEP/SMAP, UMIP, CET, mitigazioni speculative, validazione frame di interrupt |
| Sottosistema | cartella del modulo | Uso delle primitive e invarianti propri del modulo |

- `arch/x86/security/` non esiste ancora. Oggi le protezioni CPU stanno
  nei moduli che le usano: SMEP/SMAP in `arch/x86/kernel/descriptors/`
  (`protection.c`, `rust/protection/`, `assembly/protection/`), confine
  user/kernel in `arch/x86/kernel/process/` e regole in
  `kernel/process/rust/policy/`.
- Le cartelle `<funzione>` si creano solo quando esiste il modulo, senza
  segnaposto. Massimo 6-8 file per cartella.
- Test in `test/lib/security/...` e `test/arch/x86/security/...`, guidati da
  programmi C/C++/Rust (per `lib/security`: `test/lib/security/runner.rs`,
  suite `security`); PowerShell solo come ingresso minimo per `run.ps1`.
- `lib/security` non sostituisce i controlli dei moduli: fornisce gli
  strumenti, ogni modulo resta responsabile dei propri invarianti.

## Test richiesti

- Test host: casi positivi, confini, overflow, input ostili, rollback.
- Test negativi: l'attacco o l'errore deve essere bloccato e registrato.
- QEMU: 10 boot per protezioni CPU, interrupt, paging, SMP; altrimenti 3/5
  (Constraint 6).

## Documentazione

Per ogni sottosistema una voce breve in fondo a questo file: cosa e stato
protetto, come, quali test, cosa resta aperto. Niente documenti separati per
ogni passo.

## Classifica

Prima stesura 2026-11-06; riallineata il 2026-11-12 da sola lettura del
codice (nessun test rieseguito). Da confermare prima di aprire ciascun passo. Il passo 4 e stato anticipato: i passi 1-3
toccano `arch/` e il kernel, in uso per K4-C5.

**Gia presente:** paging NX/WP, guard page sugli stack e IST, heap con canary e
rifiuto del double free, autorizzazione DMA e base VT-d, TLB shootdown prima
del riuso. Dal lavoro K5/K6: SMEP e SMAP abilitati via CPUID e riletti da CR4
(user mode negato se ne manca uno), user mode con root di paging separata
per processo, ingresso SYSCALL con ritorno `iretq` e frame validato, user
copy limitata a 256 byte con `stac`/`clac` e recupero del page fault solo
nella finestra di copia, budget di tempo per processo.

**Mancante:** stack canary (tutto compila con `/GS-`), sorgente di entropia,
W^X dell'immagine kernel verificato per sezione, UMIP, KASLR, mitigazioni
speculative oltre a `lfence` dopo `swapgs`.

**Limiti noti del codice presente:**

- `lib/security` non ha consumatori nel kernel: nessuna chiamata a
  `os_bounds_*`, nessun header C, SHA-256/HMAC/DRBG senza sorgente.
- La root del processo mappa tutta l'immagine kernel (serve a ingresso e
  user copy): non e KPTI e non va dichiarata come isolamento Meltdown.
- Lo stack delle syscall e un array statico con un canary alla parola 0,
  controllato a fine turno; guard page non trovata, da verificare.
- `arch/x86/kernel/process/process.c`: 993 righe, stato globale, un solo
  processo sul BSP, simboli `k6_*`, hook di test nel percorso reale sotto
  `#ifdef`. Da dividere.
- `x86_process_stop` ferma la CPU senza messaggio (`cli; hlt`).

| # | Passo | Rischio | Motivo |
|---|---|---|---|
| 1 | `lib/security/entropy` + `arch/x86/security/entropy` | Alto | Base per canary e randomizzazione. **Parte comune fatta**: health test SP 800-90B, SHA-256, HMAC, HMAC_DRBG; resta la sorgente RDRAND/RDSEED via CPUID |
| 2 | Stack canary del kernel | Alto | Oggi nessuna protezione sull'overflow di stack in C; richiede supporto `/GS` freestanding o canary propri |
| 3 | W^X dell'immagine kernel | Alto | Verificare e imporre `.text` RX, `.rdata` RO+NX, dati RW+NX; test negativo su scrittura in `.text` |
| 4 | Primitive comuni `lib/security/bounds` | Medio-alto | **Fatto per primo** (2026-11-06): non tocca hardware ne kernel, utile a tutti i passi successivi |
| 5 | Input non fidati: ACPI, VirtIO, PS/2, shell | Medio | Tabelle firmware e device sono input esterni: limiti, lunghezze, checksum |
| 6 | UMIP | Medio, ora | **SMEP/SMAP fatti** con K5/K6. Resta UMIP: CR4 bit 11, da abilitare solo con verifica CPUID |
| 7 | DMA: VT-d attivo per i device | Medio | Il DMA bypassa il paging della CPU; da collegare a VirtIO |
| 8 | Consolidamento del confine user/kernel | Alto, ora | **Syscall e user copy fatti** con K5/K6. Restano: guard page sullo stack delle syscall, divisione di `process.c`, arresti con diagnostica, decisione su KPTI |
| 9 | Collegare `lib/security/bounds` ai moduli | Medio | Header C e sostituzione dei controlli scritti a mano |

## Progressi

- **2026-11-06 — `lib/security/bounds`.** Aritmetica controllata, allineamento,
  range e indici dentro un contenitore, disgiunzione fail-closed. Rust `no_std`,
  export C a soli scalari. Suite host `security`: 14 test (debug e
  release), oracolo a 128 bit, input ostili, clippy, oggetto freestanding.
  Aperto: header C e collegamento ai moduli. Dettagli in
  [bounds.md](../../lib/security/bounds/documentation/bounds.md).
- **2026-11-06 — `lib/security/entropy` (parte comune).** Health test NIST
  SP 800-90B: Repetition Count e Adaptive Proportion, avvio a 1024 campioni,
  guasto persistente fino a `restart()`. Rust `no_std`, API Rust. Suite host
  `security`: 16 test, cutoff confrontati con la Table 2 dello
  standard. Aperto: sorgente x86, condizionamento/DRBG. Dettagli in
  [entropy.md](../../lib/security/entropy/documentation/entropy.md).
- **2026-11-07 — `lib/security/hash/sha256`.** SHA-256 FIPS 180-4, primo
  anello della catena SHA-256 → HMAC → HMAC_DRBG verso i canary. Rust
  `no_std`, API Rust. Suite `security`: 9 test, costanti ricalcolate dai
  primi, vettori NIST, confronto con Windows CNG. Stato azzerato alla
  distruzione con `lib/security/memory`. Dettagli in
  [sha256.md](../../lib/security/hash/sha256/documentation/sha256.md).
- **2026-11-07 — `lib/security/memory`.** Azzeramento non eliminabile
  (`wipe`, `Wiping`) e confronto senza uscita anticipata (`equal`). Rust
  `no_std`, API Rust, gia usato da SHA-256. Suite `security`: 8 test piu una
  sonda sull'assembly che dimostra che l'azzeramento resta in release.
  Aperto: tempo costante solo "best effort". Dettagli in
  [memory.md](../../lib/security/memory/documentation/memory.md).
- **2026-11-07 — `lib/security/mac/hmac`.** HMAC-SHA-256 FIPS 198-1, secondo
  anello verso HMAC_DRBG. Chiave, pad e stato precalcolato azzerati. Rust
  `no_std`, API Rust; SHA-256 diviso in radice + `engine.rs` per poterlo
  includere. Suite `security`: 8 test, vettori RFC 4231 e confronto con
  Windows CNG. Dettagli in
  [hmac.md](../../lib/security/mac/hmac/documentation/hmac.md).
- **2026-11-07 — `lib/security/random/drbg`.** HMAC_DRBG SHA-256 SP 800-90A
  Rev. 1: chiude la catena software dei numeri casuali. Rust `no_std`, API
  Rust, limiti della Table 2, Key/V azzerati. Suite `security`: 9 test con
  480 vettori ufficiali NIST CAVP. Aperto: sorgente x86 e canary, dopo K4-C5.
  Dettagli in [drbg.md](../../lib/security/random/drbg/documentation/drbg.md).
- **2026-11-07 — progetto della sorgente x86.** Fonti verificate (guida
  Intel DRNG, CPUID, QEMU TCG `-cpu max` con RDRAND/RDSEED). Decisioni:
  CPU accreditata (RDSEED, RDRAND di riserva) piu jitter RDTSC e interrupt
  non accreditati, Rust con intrinsic, arresto con diagnostica se manca
  entropia valida. Codice dopo K4-C5. Dettagli in
  [security-entropy.md](../design/security-entropy.md).
- **2026-11-07 — `test/tooling/entropy`.** Strumento host Rust di stima
  della min-entropia SP 800-90B: 10 estimatori non-IID e combinazione
  §3.1.3. Suite `security` (modulo `assessment`): 19 test con tutti gli
  esempi ufficiali e la Table 3. Servira ad accreditare jitter e interrupt.
  Dettagli in [entropy.md](../../test/tooling/entropy/documentation/entropy.md).
- **2026-11-07 — confronto con lo strumento NIST.** Lo strumento di stima
  coincide con il riferimento NIST (`ea_non_iid`) su 105 valori di 7 dataset;
  z al quantile esatto come NIST. Test differenziale permanente con i valori
  NIST salvati. Restano aperti test IID e test di riavvio.
- **2026-11-07 — test di riavvio SP 800-90B §3.1.4.** Comando
  `assessment.exe restart`: controllo di sanita binomiale, H_r e H_c su righe
  e colonne, regola H_I/2. H_r e H_c coincidono con NIST (28 su 28). Resta
  aperto il test IID (§5).
- **2026-11-12 — riallineamento del piano.** Review di sola lettura di
  `lib/security`, protezione SMEP/SMAP e confine user/kernel. Corrette le
  voci superate (SMEP/SMAP, user mode, syscall e user copy risultavano
  mancanti o futuri) e aggiunti i limiti noti. Nessun codice modificato,
  nessun test eseguito. Ordine proposto: sorgente RDSEED/RDRAND, canary e
  guard page, header di `bounds`, UMIP, divisione di `process.c`.
