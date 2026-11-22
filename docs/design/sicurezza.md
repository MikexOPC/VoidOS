# Design — Obiettivo 5: Sicurezza di sistema

## 1. Obiettivo

Il kernel inizializza una sorgente di entropia hardware verificata via CPUID (`RDSEED`/`RDRAND` + health test NIST SP 800-90B), attiva stack canary (`/GS`) con cookie casuale al boot, verifica W^X su tutte le pagine mappate dell'immagine kernel e le guard page degli stack, valida le protezioni CPU (`NX`/`WP`/`SMEP`/`SMAP`/`UMIP`) e implementa KPTI completo (doppia PML4 con trampolino minimale) più audit delle mitigazioni speculative, chiudendo il cancello «base solida» prima della GUI.

## 2. Entropia hardware e CSPRNG del kernel (Task 5.1)

- **Fonti hardware (Intel SDM):**
  - `CPUID.(EAX=01H)`: `ECX` bit 30 = `RDRAND` (Intel SDM Vol. 2A, Table 3-10).
  - `CPUID.(EAX=07H, ECX=0H)`: `EBX` bit 18 = `RDSEED` (Intel SDM Vol. 2A, Table 3-8).
  - `RDSEED` / `RDRAND` (Intel SDM Vol. 2B): `CF=1` indica campione valido; limite di 64 tentativi per `RDSEED` e 10 per `RDRAND` (Intel DRNG Software Implementation Guide §5.2.1), mai attesa infinita.
  - `RDTSC` usato esclusivamente come nonce/personalizzazione aggiuntiva, mai accreditato come entropia crittografica.
- **Health test e DRBG:** riuso diretto delle primitive in `lib/security/` (`HealthMonitor` RCT + APT su 1024 campioni di startup, `HmacDrbg` SHA-256 NIST SP 800-90A Rev. 1).
- **Ownership e lock:** stato globale protetto da `KERNEL_LOCK` (rank `120`, inferiore a process table `150`, heap `200`, physical `300`). Se l'hardware non espone né `RDSEED` né `RDRAND` o fallisce l'health test, il kernel riporta esplicitamente lo stato senza fingere entropia crittografica.

## 3. Stack canary del kernel `/GS` (Task 5.2)

- Rimozione di `/GS-` e attivazione di `/GS` su tutti i sorgenti C del kernel in `scripts/tooling/build-plan/rust/plan/main.rs`.
- Definizione di `uintptr_t __security_cookie` e `__security_check_cookie(uintptr_t cookie)`:
  - Cookie iniziale non nullo a compile-time (per il percorso precoce pre-entropia), poi sostituito a runtime con un valore casuale generato dal CSPRNG del kernel prima dell'avvio dei sottosistemi dinamici (funzioni che non rientrano nel prologo pre-reseed).
  - In caso di mismatch: `__report_gsfailure` / `__security_check_cookie` invoca `kernel_panic("STACK_COOKIE_CORRUPTED")` (nessun arresto muto).

## 4. Audit W^X runtime, guard page e UMIP (Task 5.3)

- **Audit W^X:** scansione delle PTE effettive dell'intera immagine del kernel (`X86_KERNEL_IMAGE_BASE .. X86_KERNEL_IMAGE_LIMIT`) tramite vista ricorsiva: ogni pagina presente deve rispettare `!(PTE_WRITE && !PTE_NX)`; qualsiasi pagina `W+X` provoca `kernel_panic("KERNEL_WX_VIOLATION")`.
- **Guard page:** verifica esplicita sulle PTE che le pagine di guardia dello stack di boot, dello stack syscall BSP/AP e degli stack task siano non presenti (`!PTE_PRESENT`).
- **UMIP reale in Ring 3:** quando `CR4.UMIP` (bit 11, Intel SDM Vol. 3A §2.5) è attivo, verifica che un'istruzione `SGDT` eseguita da CPL3 generi `#GP` (vettore `13`) e il processo venga isolato e raccolto senza impattare il sistema.

## 5. KPTI completo e audit mitigazioni speculative (Task 5.4)

- **Doppia PML4 (`UserRoot` vs `KernelRoot`):**
  - `transition.asm` esegue già lo switch di `CR3` a ogni ingresso/uscita `SYSCALL`, `IRQ`, `FAULT` e `NMI`.
  - Con KPTI completo, `UserRoot` **non** mappa `X86_LAYOUT_HEAP_SLOT` (32 MiB heap kernel) né `X86_LAYOUT_AP_SLOT` (strutture AP), e riduce il mapping kernel in `UserRoot` al solo trampolino minimale necessario allo switch `CR3` (codice `transition.asm`, GDT/TSS/IDT e stato per-CPU `X86_PROCESS_CPU`).
  - Verifica esplicita nei test che gli indirizzi dell'heap kernel e dei dati di altri processi non siano presenti nella PML4 utente (`!PTE_PRESENT`).
- **Audit CPUID / `IA32_ARCH_CAPABILITIES` (MSR `0x10A`):** rilevamento via `CPUID.(EAX=07H, ECX=0H)` (`EDX` bit 26 `IBRS/IBPB`, bit 27 `STIBP`, bit 29 `ARCH_CAPABILITIES`), abilitazione delle mitigazioni hardware disponibili e log esplicito dello stato.

## 6. Chiusura del cancello «base solida» pre-GUI (Task 5.5)

- Suite host completa (ASan + Rust) verde.
- Immagine unica `ordinary`: 10 boot `--ordinary-shell --smp 4`, 3 boot `--ordinary-shell --smp 8`, 1 boot `--ordinary-no-ps2 --smp 4`, **soak continuo da 30 minuti** (`--ordinary-soak 30`) senza errori né perdite di memoria.
- Aggiornamento [docs/validation/sicurezza.md](../validation/sicurezza.md) e [docs/status.md](../status.md).
