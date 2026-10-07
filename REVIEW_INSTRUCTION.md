# REVIEW_INSTRUCTION.md: Full Project Review Guide
**Versione:** 1.1 | **Target:** MikeSW OS Agent | **Lingua:** Italiano

---

## Attendibilita dei template

Tutti gli output, conteggi, prestazioni, severity e stime qui sotto sono esempi, non risultati misurati. Usare il [riferimento R-003](docs/review/instructions/security.md) per hardware, limiti e prove applicabili. Non riempire coverage non misurata: N/D. ASan non certifica leak fisici; Valgrind non esegue un kernel freestanding come processo host. Non modificare/riformattare sorgenti durante un audit senza task di fix autorizzata. Benchmark Linux/Minix e target di boot dei template non sono promesse o dati del progetto.

## Trigger
Quando: utente chiede "fai una review del progetto" o "analizza tutto"

## Input Obbligatorio

- Scope review (es. "tutto", "solo kernel", "security", "performance")

---

## PHASE 1: Code Quality Scan (Automatico)

### 1.1 File Size Audit
```bash
Esegui per ogni file .c, .h, .cpp, .asm:
  wc -l file
  Se > 400 righe: REPORT come violation
  Se 200-400: OK ma nota
  Se < 200: PERFETTO

OUTPUT TEMPLATE:
File Size Violations:
  ❌ kernel/scheduler/scheduler.c (480 righe) - EXCEEDS 400
  ⚠️  kernel/memory/allocator.c (350 righe) - OK but large
  ✅ drivers/timer/lapic.c (145 righe) - GOOD

Total files > 400: X
Average file size: Y righe
```

### 1.2 Compiler Warnings
```bash
Compila tutto con:
  gcc -Wall -Wextra -Werror -std=c11 *.c

OUTPUT TEMPLATE:
Compiler Warnings:
  ✅ kernel/scheduler.c - ZERO warnings
  ❌ drivers/nic.c:45 - unused variable 'timeout'
  ❌ arch/boot.asm:12 - implicit function declaration

Total warnings: X
Fixable: Y
Unfixable (hardware-dependent): Z
```

### 1.3 Static Analysis
```bash
Esegui:
  cppcheck --enable=all .

CERCA:
  - Memory leaks
  - Null pointer dereference
  - Array bounds
  - Buffer overflow
  - Integer overflow

OUTPUT TEMPLATE:
Static Analysis (cppcheck):
  ❌ kernel/memory.c:67 - Buffer overflow risk
     Details: strcpy without bounds check
  ❌ kernel/scheduler.c:145 - Memory leak
     Details: malloc without free
  ✅ drivers/timer.c - NO ISSUES

Total issues: X
Severity: CRITICAL (Y), HIGH (Z)
```

### 1.4 Memory Safety
```bash
Esegui:
  valgrind --leak-check=full --show-leak-kinds=all ./kernel_test

Oppure:
  gcc -fsanitize=address -g ./test.c kernel.c -o test
  ./test

OUTPUT TEMPLATE:
Memory Safety (Valgrind/ASAN):
  ✅ Heap blocks: 0 bytes in 0 blocks
  ✅ Possible leaks: 0 bytes
  ✅ Reachable: 1,024 bytes (expected)
  ❌ Definitely lost: 4,096 bytes in 2 blocks
     Location: kernel/allocator.c:78 (malloc)
     Not freed at: kernel/allocator.c:89 (free)

Total leaks: X bytes in Y blocks
Fixable: Z
```

### 1.5 Code Style
```bash
Esegui:
  clang-format -i *.c *.h
  git diff (mostra violations)

CERCA:
  - Inconsistent indentation
  - Trailing whitespace
  - Line length > 100 chars
  - Missing braces
  - Alignment violations

OUTPUT TEMPLATE:
Code Style Violations:
  ⚠️  kernel/paging.c:12-45
      Inconsistent indentation (mix tab/space)
  ⚠️  drivers/nic.c:203
      Line too long (127 chars, max 100)
  ⚠️  arch/boot.asm:34
      Misaligned typedef struct

Total violations: X
Auto-fixable: Y
Manual fix needed: Z
```

### 1.6 Security Red Flags
```bash
GREP PER:
  grep -rn "strcpy\|sprintf\|gets\|scanf" . (unsafe functions)
  grep -rn "TODO\|FIXME\|XXX\|HACK\|SECURITY" . (known issues)
  grep -rn "magic.*number\|hardcode" . (hardcoded values)

OUTPUT TEMPLATE:
Security Red Flags:
  ❌ kernel/syscall.c:67 - strcpy without bounds
     Risk: Buffer overflow → RCE
  ⚠️  kernel/memory.c:34 - TODO: validate user pointer
     Risk: Use-after-free possible
  ⚠️  drivers/nic.c:145 - FIXME: add timeout
     Risk: Deadlock if device hangs

Critical patterns found: X
TODO/FIXME: Y (indicates known issues)
```

---

## PHASE 2: Architecture Review

### 2.1 Modular Isolation
```bash
Per ogni cartella:
  Conta file: ls -1 | wc -l
  Se > 5 file: VIOLATION
  Se ≤ 5: OK

Cerca dipendenze:
  grep -r "#include.*kernel/scheduler" kernel/memory/
  Se trova: Circular dependency, REPORT

OUTPUT TEMPLATE:
Modular Isolation:
  ✅ kernel/scheduler/ (4 file) - OK
  ❌ kernel/memory/ (7 file) - EXCEEDS 5
     Files: allocator.c, paging.c, vmm.c, heap.c, test/, cleanup.c, debug.c
  ✅ drivers/nic/ (3 file) - OK

Circular includes:
  ✅ None detected

Dependency graph:
  kernel/memory → kernel/paging (allowed)
  kernel/scheduler → kernel/memory (allowed)
  ❌ kernel/paging → kernel/scheduler (CIRCULAR!)
```

### 2.2 Language Allocation Correctness
```bash
VERIFICA:
  C file in root? (✅)
  C file in c/ subfolder? (❌ VIOLATION)
  C++ in cpp/? (✅)
  Rust in rust/? (✅)
  ASM in assembly/? (✅)

OUTPUT TEMPLATE:
Language File Organization:
  ✅ C files in component root (correct)
     kernel/scheduler.c, kernel/paging.c
  ⚠️  C++ files location
     ✅ drivers/nic/cpp/nic_wrapper.cpp (correct)
  ✅ Rust files in rust/ subfolder
  ✅ Assembly in assembly/ subfolder

Violations: 0 ✅
```

### 2.3 Data Structure Design
```bash
CERCA in typedef struct:
  volatile keyword on hardware access? (✅)
  __attribute__((aligned(4096)))? (✅ se page tables)
  Padding explicit? (✅ per alignment)

OUTPUT TEMPLATE:
Data Structure Design:
  ✅ device_regs struct has volatile (hardware access)
  ✅ page_table struct aligned 4096
  ⚠️  schedule_t struct missing padding
      Size: 48 bytes, should be 64 (alignment)
  ✅ task_state_t uses proper enum

Issues: 1 (fixable)
```

### 2.4 Naming Conventions
```bash
CERCA:
  camelCase in C? (❌ VIOLATION)
  snake_case funzioni? (✅)
  UPPER_CASE costanti? (✅)
  _prefix static/private? (✅)

OUTPUT TEMPLATE:
Naming Convention:
  ✅ Functions use snake_case
     schedule_task(), context_switch()
  ✅ Constants use UPPER_CASE
     MAX_TASKS, TIMESLICE_DEFAULT
  ✅ Static functions prefixed
     _validate_page_table(), _allocate_frame()
  ❌ Variables inconsistent
     scheduleQueue (camelCase) vs schedule_queue (snake_case)
     Locations: kernel/scheduler.c:12, 45, 78

Violations: 3
```

---

## PHASE 3: Security Audit

### 3.1 Memory Access Validation
```bash
CERCA IN SYSCALL HANDLERS:
  copy_from_user() - ha bounds check?
  validate_user_ptr() - esiste?
  MAX_LEN passato come argomento?

OUTPUT TEMPLATE:
Memory Access Validation:
  ❌ sys_read(int fd, void *buf, size_t len)
     Risk: unbounded copy_from_user()
     Fix: Add (len > MAX_BUF_SIZE) check

  ✅ sys_write() has proper validation
     Code: if (len > MAX_BUF_SIZE) return -EFAULT;

  ⚠️  Incomplete user pointer validation
     Missing: validate_user_ptr(buf, len) call

Vulnerabilities: 1 (RCE risk)
```

### 3.2 Ring Enforcement
Verificare CPL, entry/return, stack, stato e permissions usando il [riferimento R-003](docs/review/instructions/security.md). Non classificare una near CALL kernel→kernel come ring crossing. SYSCALL/SYSRET presenti nel testo non dimostrano un entry sicuro.

### 3.3 CPU Protection Flags
Usare la tabella R-003 per registri/bit e CPUID. NX richiede EFER.NXE e XD delle entry; non esiste un flag NX in CR0. Verificare supporto e prerequisiti prima di SMEP/SMAP; distinguere protezione supervisor da pagine user e protezione user da pagine supervisor.

Output per protezione: IMPLEMENTATA / SUPPORTATA NON ATTIVA / NON SUPPORTATA / NON APPLICABILE / NON VERIFICATA, con profilo CPU, location ed evidenza. L'assenza di una feature opzionale non e automaticamente CRITICAL.

### 3.4 Page Table Isolation (KPTI)
Verificare separazione delle viste e mapping minime entry/exit, U/S, CPU e threat model. KPTI mitiga Meltdown su CPU vulnerabili; non e una difesa generale contro Spectre. Senza user mode: gate futuro, non exploit attuale. Niente stime fisse di 16 ore o accesso user presunto soltanto perche esistono mapping kernel.


### 3.5 Interrupt Handler Safety
```bash
CERCA NEL INTERRUPT HANDLER:
  Registers saved BEFORE device access?
  Device output validated BEFORE use?
  Atomic operations on shared data?

OUTPUT TEMPLATE:
Interrupt Handler Safety:
  ⚠️  timer_interrupt_handler()
     Location: drivers/timer/lapic.c:89
     Issue: No spinlock on shared_data access
     Risk: Race condition → data corruption
     Symptom: Occasional scheduler hangs
     Fix: Add spinlock_lock() + memory barrier

  ✅ nic_interrupt_handler() - registers saved ✅
  ✅ Device output validated ✅

Issues: 1 (MEDIUM)
```

### 3.6 DMA & Device Validation
Controllare inventario e bus mastering, IOMMU/policy del profilo, buffer pinned con limiti, revoca, reset e timeout appropriato. Non inferire accesso DMA di ogni device dalla sola assenza di IOMMU. Le PTE CPU non proteggono da DMA.

MMIO read-only soltanto per registri che lo consentono; ordering e cache policy derivano dal device/CPU. Un timeout usa clock, deadline e recovery verificabili: nessun valore universale di 100 ms.

---

## PHASE 4: Performance Analysis

### 4.1 Algorithm Complexity
```bash
CERCA:
  Nested loop O(n²)?
  Linear search in large data?
  Inefficient algorithms?

OUTPUT TEMPLATE:
Algorithm Complexity:
  ❌ Scheduler: O(n) task search per tick
     Location: kernel/scheduler/scheduler.c:67
     Impact: 1000 task = 1000 iterations per 10ms
     Bottleneck: 10% CPU overhead
     Fix: Implement O(log n) priority queue (20 hours)

  ✅ Memory allocator: O(log n) bitmap
  ✅ Page table lookup: O(1)

  Bottlenecks: 1 (fixable)
```

### 4.2 Memory Bloat & Waste
```bash
CALCOLA:
  Total malloc size vs actual used
  Unused struct fields
  Buffer sizes can be reduced?

OUTPUT TEMPLATE:
Memory Bloat Analysis:
  ⚠️  Heap allocator uses 2KB for metadata per alloc
     With 1000 task = 2MB waste
     Optimization: Reduce metadata (8 bytes → 4 bytes)
     Savings: 1MB

  ⚠️  RX descriptor ring: 256 descriptors (4KB each) = 1MB
     Usage: ~100 frames/sec, descriptor rate OK
     Status: Can't reduce without impact

  ✅ Stack usage reasonable

Avoidable waste: ~1MB
```

### 4.3 Bottlenecks
```bash
MISURA/ESTIMA:
  Boot time
  Context switch latency
  Device I/O latency

OUTPUT TEMPLATE:
Performance Bottlenecks:
  ⚠️  Boot time: 850ms (target: 200ms)
     Cause: Device initialization takes 600ms
     Breakdown:
       - NIC init: 250ms
       - Storage init: 200ms
       - Other: 150ms
     Fix: Parallelize device init (not critical for v0)

  ⚠️  Context switch: 5µs (target: 1µs)
     Cause: Unnecessary register saves
     Fix: Optimize assembly (4 hours)

  ✅ Device I/O latency: 100µs (good)

Fixable bottlenecks: 2
```

---

## PHASE 5: Detailed Bug List

### Output Template per Bug
```
[BugID-XXX] Titolo breve
  Severity: CRITICAL | HIGH | MEDIUM | LOW
  Location: file.c:line_number
  Component: [scheduler/memory/driver/etc]

  Symptom:
    [Cosa succede quando il bug si manifesta]

  Root Cause:
    [Perché accade]

  Impact:
    [Conseguenze]

  Reproduction:
    [Come riprodurlo]

  Proposed Fix:
    [Correzione con invarianti, rollback e criteri di accettazione]

  Time Estimate: [intervallo di ore, incluse verifiche]
  Effort: EASY | MEDIUM | HARD
  Risk: LOW | MEDIUM | HIGH con motivazione

  Priority: P0 | P1 | P2 | P3
```

### Esempio illustrativo, non rilievo del progetto

Esempio: hang da riacquisizione dello stesso lock nel percorso scheduler.
- Severity: HIGH se confermato come perdita di progresso; non attribuire un triple fault a una deadlock senza evidenza CPU.
- Location: file/riga da determinare nel caso reale, non un indirizzo inventato.
- Sintomo: CPU senza progresso; root cause da verificare con stack/lock owner e IRQ state.
- Riproduzione: injection di rientro nel profilo supportato, deadline del test host e cattura dello stato.
- Fix: lock order e ownership, ingressi non rientranti e aggiornamenti coerenti. Non saltare un lock held senza identificarne il proprietario e gli invarianti.
- Stima/effort/rischio: dopo diagnosi e scope, non una promessa di one-line fix.
- Validazione: progresso, integrita delle code, IRQ e cleanup; test positivi/negativi e stress pertinente.

---

## PHASE 6: Vulnerabilities & Falle

### Output Template per Vulnerability
```
[SEC-XXX] Titolo
  Type: Buffer Overflow | RCE | Privilege Escalation | Race Condition | etc
  Severity: CRITICAL | HIGH | MEDIUM
  Location: file.c:line

  Vulnerability:
    [Descrizione tecnica]

  Attack Vector:
    [Come un attacker potrebbe sfruttarla]

  Risk/Impact:
    [Conseguenza della sfruttamento]

  Affected Code:
    [Snippet del codice vulnerabile]

  Mitigation:
    [Come riparare]

  Status: NOT_FIXED | IN_PROGRESS | FIXED
```

### Esempio
```
[SEC-001] Buffer overflow in sys_read()
  Type: Buffer Overflow
  Severity: CRITICAL
  Location: kernel/syscall/syscall.c:67

  Vulnerability:
    sys_read() calls copy_from_user() senza bounds check
    su user-supplied buffer length.

  Attack Vector:
    User app calls sys_read() con len=999999
    Kernel copia oltre il 4KB buffer → heap corruption

  Risk/Impact:
    RCE (Arbitrary code execution in kernel context)
    Privilege escalation

  Affected Code:
    ```c
    int sys_read(int fd, void *buf, size_t len) {
        return copy_from_user(kernel_buf, buf, len);
        // NO SIZE CHECK!
    }
    ```

  Mitigation:
    ```c
    int sys_read(int fd, void *buf, size_t len) {
        if (len > MAX_BUF_SIZE) return -EFAULT;
        if (!validate_user_ptr(buf, len)) return -EFAULT;
        return copy_from_user(kernel_buf, buf, len);
    }
    ```

  Status: NOT_FIXED (P0 priority)
```

---

## PHASE 7: Improvements & Future Work

### SHORT-TERM (Next 2 weeks)
```
[FEAT-001] Migrate scheduler to priority-based
  Description:
    Valutare politiche scheduler dopo workload/benchmark; nessuna migrazione CFS prestabilita

  Why:
    Current round-robin has starvation issue
    Low-priority tasks never run

  Effort: 20 hours
  Risk: MEDIUM (big refactor, but isolated)
  Dependencies: None
  Timeline: 2 weeks

  Tasks:
    1. Design priority queue (4h)
    2. Implement in Rust (12h)
    3. Test & integrate (4h)

[FEAT-002] Add IOMMU support
  Description:
    Implement IOMMU page tables for DMA device access

  Why:
    Security requirement (prevent DMA attacks)

  Effort: 16 hours
  Risk: HIGH (hardware-dependent, complex)
  Timeline: 2 weeks

  Blocks: Device driver stability

[FEAT-003] Optimize page table layout
  Description:
    Reduce TLB misses by improving page table locality

  Why:
    Memory access latency critical for gaming

  Effort: 12 hours
  Risk: MEDIUM
  Timeline: 1 week
```

### MEDIUM-TERM (Next month)
```
[FEAT-004] Priority-based interrupt handling
  Effort: 24 hours

[FEAT-005] Simplified ext4 file system
  Effort: 40+ hours
  Blocks: Persistent storage, game assets loading

[FEAT-006] GPU driver (Intel iGPU)
  Effort: 80+ hours
  Risk: VERY HIGH (new subsystem)
  Blocks: Gaming functionality
```

### LONG-TERM (v1.0)
```
- Userland libc + basic syscalls
- Competitive scheduler (O(log n))
- Virtual memory optimization
- Networking stack (TCP/IP)
- Audio subsystem
- Input devices (keyboard, mouse, gamepad)
```

---

## PHASE 8: Executive Summary

### Output Template
```
═══════════════════════════════════════════════════════
OS v0 - COMPREHENSIVE PROJECT REVIEW REPORT
═══════════════════════════════════════════════════════

📊 CODEBASE STATISTICS
─────────────────────
  Total Lines of Code: X
  Total Files: Y
  C files: Z
  C++ files: W
  Rust files: V
  Assembly files: U

  Largest file: kernel/scheduler.c (480 righe) ⚠️
  Smallest file: drivers/timer/lapic.c (45 righe)
  Average file size: 200 righe

🧪 TEST COVERAGE
────────────────
  Unit tests: X/Y (coverage %)
  Integration tests: Pass/Fail
  QEMU boot tests: 50/50 pass rate ✅
  Memory leak tests: Pass ✅
  Flaky tests: 2 (list: X, Y)

🔴 ISSUES SUMMARY
─────────────────
  CRITICAL: 2 (MUST FIX)
    - [BugID-001] Triple Fault on boot
    - [BugID-002] Memory leak in allocator

  HIGH: 4 (SHOULD FIX before release)
    - [BugID-003] NIC timeout
    - [BugID-004] Scheduler starvation
    - [SEC-001] SMEP not enabled
    - [SEC-002] No KPTI isolation

  MEDIUM: 5
    - [BugID-005] Context switch performance
    - ... (list altri)

  LOW: 3
    - Code style violations
    - Documentation gaps
    - ... (list altri)

⚠️  SECURITY STATUS
───────────────────
  Vulnerabilities: 3 (CRITICAL, HIGH, MEDIUM)
  Missing protections: SMEP, SMAP (partial), KPTI
  DMA protection: NOT IMPLEMENTED (IOMMU)
  Risk level: MEDIUM (acceptable for v0)

✅ WHAT'S WORKING WELL
──────────────────────
  ✅ Boot sequence stable (50/50 boots)
  ✅ Syscall handler correct (tested)
  ✅ Memory isolation working
  ✅ Interrupt handling atomic
  ✅ Code architecture clean
  ✅ File size limits respected (mostly)
  ✅ No memory leaks (except known)
  ✅ Zero circular includes

📈 TOP 5 PRIORITIES (in order)
──────────────────────────────
  1. FIX [BugID-001] Triple Fault (2h) - P0
  2. FIX [BugID-002] Memory leak (1h) - P0
  3. ENABLE SMEP protection (10min) - P1
  4. FIX Scheduler starvation (20h) - P1
  5. IMPLEMENT KPTI (16h) - P1

⏱️  ESTIMATED WORK
──────────────────
  P0 bugs (critical): 3 hours
  P1 issues (high): 36 hours
  P2 issues (medium): 12 hours
  Documentation: 8 hours
  ─────────────────────────────
  TOTAL: 59 hours

  Realistic timeline (40h/week):
    All critical: 1-2 days
    All P0+P1: 2 weeks
    All issues: 3 weeks

🎯 NEXT MILESTONE (v0.1)
────────────────────────
  Target date: 2 weeks

  Must-have:
    [ ] Fix all P0 bugs
    [ ] Fix all P1 security issues
    [ ] Boot stable (100% pass)
    [ ] NIC driver working
    [ ] Basic security (SMEP+SMAP+NX)

  Nice-to-have:
    [ ] Priority scheduler
    [ ] Full KPTI
    [ ] Performance optimizations

═══════════════════════════════════════════════════════
```

---

## FINAL OUTPUT FILES TO GENERATE

Crea questi file .md:

```
review_results/
├── 00_EXECUTIVE_SUMMARY.md
├── 01_CODE_QUALITY_REPORT.md
├── 02_ARCHITECTURE_REVIEW.md
├── 03_SECURITY_AUDIT.md
├── 04_PERFORMANCE_ANALYSIS.md
├── 05_DETAILED_BUG_LIST.md
├── 06_VULNERABILITIES_FALLE.md
├── 07_IMPROVEMENTS_ROADMAP.md
├── 08_FIX_PRIORITY_CHECKLIST.md (priorità P0→P1→P2→P3)
└── REVIEW_COMPLETE.txt (timestamp + status)
```

---

## AGENTE OUTPUT FINALE
```
✅ COMPREHENSIVE REVIEW COMPLETE

Generated reports:
  ✅ Code quality audit
  ✅ Architecture review
  ✅ Security audit
  ✅ Performance analysis
  ✅ Detailed bug list (X bugs total)
  ✅ Vulnerabilities & falle
  ✅ Improvements roadmap
  ✅ Executive summary

All files saved in: ./review_results/

CRITICAL ISSUES: 2 (fix immediately)
HIGH ISSUES: 4 (fix before release)
MEDIUM ISSUES: 5
LOW ISSUES: 3

Estimated work: 59 hours

Next phase: Start fixing P0 issues?
```
