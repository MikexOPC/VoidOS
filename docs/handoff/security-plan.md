# Piano sicurezza dei sottosistemi

Stato: avviato (bounds e health test entropia pronti). Questo file e il punto di partenza per chi lavora sulla
sicurezza; va aggiornato a ogni passo.

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

Prima stesura, 2026-11-06, da sola lettura del codice e di `docs/status.md`.
Da confermare prima di aprire ciascun passo. Il passo 4 e stato anticipato: i passi 1-3
toccano `arch/` e il kernel, in uso per K4-C5.

**Gia presente:** paging NX/WP, guard page sugli stack e IST, heap con canary e
rifiuto del double free, autorizzazione DMA e base VT-d, TLB shootdown prima
del riuso.

**Mancante:** stack canary (tutto compila con `/GS-`), sorgente di entropia,
W^X dell'immagine kernel verificato per sezione, SMEP/SMAP/UMIP, user mode.

| # | Passo | Rischio | Motivo |
|---|---|---|---|
| 1 | `lib/security/entropy` + `arch/x86/security/entropy` | Alto | Base per canary e randomizzazione. **Parte comune fatta** (health test SP 800-90B); resta la sorgente RDRAND/RDSEED via CPUID |
| 2 | Stack canary del kernel | Alto | Oggi nessuna protezione sull'overflow di stack in C; richiede supporto `/GS` freestanding o canary propri |
| 3 | W^X dell'immagine kernel | Alto | Verificare e imporre `.text` RX, `.rdata` RO+NX, dati RW+NX; test negativo su scrittura in `.text` |
| 4 | Primitive comuni `lib/security/bounds` | Medio-alto | **Fatto per primo** (2026-11-06): non tocca hardware ne kernel, utile a tutti i passi successivi |
| 5 | Input non fidati: ACPI, VirtIO, PS/2, shell | Medio | Tabelle firmware e device sono input esterni: limiti, lunghezze, checksum |
| 6 | SMEP/SMAP/UMIP | Medio, ora | Utili davvero con lo user mode; abilitabili prima solo se CPUID li offre e il kernel non tocca pagine user |
| 7 | DMA: VT-d attivo per i device | Medio | Il DMA bypassa il paging della CPU; da collegare a VirtIO |
| 8 | Syscall, user copy, KPTI | Futuro | Solo quando esiste lo user mode |

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
