# Agent Skills: OS Development v0 → Production
**Versione:** 1.1 | **Ultimo aggiornamento:** 2026-10-05 | **Target:** MikeSW OS Agent

---

R-003: seguire direttive-locali e il [riferimento verificato](docs/review/instructions/security.md). Massimo 400 righe per sorgente e cinque file diretti; limiti inferiori negli esempi sono obiettivi locali. Test e script del componente sono parte del modulo. Per documentazione pura: review delle fonti/link; nessun test runtime inventato.

## Skill 1: hardware-real-device-mapper

### Trigger
Quando: generare codice per controllare hardware reale (GPU, NIC, storage, interrupt controller, timer, APIC)

### Input
- Device type (es. "PCIe NIC", "AHCI storage", "LAPIC timer")
- Operazione (es. "leggi MAC address", "inizializza interrupt", "scrivi registro")

### Processo
1. **Identifica il device via PCI/ACPI**
   - Se PCI: estrai vendor_id, device_id, BAR addresses (Memory-Mapped I/O o Port I/O)
   - Se ACPI: leggi la tabella DSDT/SSDT per l'indirizzo di base del device
   - **BLOCCA se l'indirizzo non è calcolato esplicitamente**

2. **Mappa la memoria volatile**
   ```c
   typedef struct {
       uint32_t volatile reg_offset;
       uint16_t volatile control;
   } DeviceRegs;

   DeviceRegs *device = (DeviceRegs *)(uintptr_t)bar_address;
   ```
   - Usa `volatile` SEMPRE per hardware registers
   - Documenta ogni offset in esadecimale con il datasheet

3. **Sequenza di inizializzazione** (da datasheet del device)
   - Scrivi in ordine esatto gli step dal manual ufficiale
   - Includi i delay necessari (`io_wait()`, timer busy-loop)
   - **BLOCCA se manca la sequenza corretta**

### Output
- ✅ File `drivers/[device_type]/init.c` + header
- ✅ Offset e bitmask commentati con riferimento al datasheet
- ✅ Test verificabile in QEMU (es. leggi e stampa un registro noto)
- ❌ BLOCCA se: indirizzo non verificato contro la discovery PCI/ACPI appropriata, volatile non usato, sequenza incompleta

### Checklist Hardware
```
[ ] PCI/ACPI address discovered and validated for the actual device
[ ] volatile keyword on all register accesses
[ ] Datasheet step-by-step sequence followed
[ ] I/O delays inserted correctly
[ ] Test case (read known register) exists
[ ] BAR address validated (non-zero, aligned)
```

---

## Skill 2: cybersecurity-kernel-enforcement

### Trigger
Quando il modulo gestisce memoria utente, syscall, interrupt o istruzioni privilegiate.

### Processo
Applicare il [riferimento R-003](docs/review/instructions/security.md): prerequisiti CPUID, protezioni paging e entry/return per il profilo reale. EFER.NXE abilita NX; una CALL nello stesso ring non e una violazione. Separare controlli numerici, mapping/permission, capacita destinazione e fault recovery.

### Checklist
- [ ] Prerequisiti CPU e paging verificati prima di scrivere registri.
- [ ] Ownership/bounds/lifetime e copie utente fallibili dove applicabili.
- [ ] Entry Assembly salva contesto prima del C e rispetta ABI/SIMD.
- [ ] SMEP/SMAP e KPTI applicabili o dichiarati gate futuri con threat model.
- [ ] Nessuna protezione o prova indicata PASS senza evidenza.

### Output
Bloccare violazioni reali del contratto del modulo; non imporre user mode/KPTI a codice che non espone tale superficie. Non copiare esempi generici come entry syscall completa.

---

## Skill 3: long-driver-modularization

### Trigger
Quando: file driver supera 400 righe, file kernel supera 400 righe

### Input
- Percorso file
- Numero di righe attuali
- Funzioni/sezioni principali

### Processo

1. **Analisi della Granularità**
   - Identifica i confini logici (init, interrupt handler, I/O ops, cleanup)
   - Raggruppa per **funzionalità**, non per grandezza

2. **Split Rules (Rigide)**
   ```
   drivers/device_type/
   ├── init.c         (< 150 righe) - PCI probe, memory mapping, IRQ setup
   ├── io_ops.c       (< 150 righe) - read/write register operations
   ├── irq_handler.c  (< 100 righe) - interrupt logic
   ├── cleanup.c      (< 80 righe)  - shutdown, resource release
   └── device.h       (< 60 righe)  - struct definitions, constants
   ```

3. **Dependency Mapping**
   ```c
   // device.h - UNICO file con typedef pubbliche
   typedef struct {
       uintptr_t mmio_base;
       uint32_t irq_num;
       spinlock_t lock;
   } device_state;

   // init.c
   #include "device.h"
   extern device_state *global_device;

   // io_ops.c
   #include "device.h"
   extern device_state *global_device;

   // irq_handler.c
   #include "device.h"
   extern device_state *global_device;
   ```

4. **Link e placement**
   - Aggiornare gli script di build del componente con toolchain e ABI supportate.
   - Il numero di file non determina l'uso di un linker script GNU: non inserirlo in un progetto MSVC/PE senza progetto del formato immagine.
   - Massimo cinque file diretti; dividere per responsabilita se il limite viene superato.

### Output
- ✅ File suddivisi con max 400 righe
- ✅ `device.h` centrale con typedef
- ✅ Dependency graph (file imports file)
- ✅ Makefile aggiornato per new files
- ✅ Link test in QEMU (carica tutti i file correttamente)

### Checklist Modularization
```
[ ] No file > 400 righe
[ ] device.h contiene SOLO typedef e #define
[ ] Circular includes evitati
[ ] Global state in un file solo (init.c o separate state.c)
[ ] Makefile compila tutti i nuovi file
[ ] Symbol visibility checked (static vs extern)
[ ] Link test passa (no undefined symbols)
```

---

## Skill 4: cybersecurity-threat-modeling

### Trigger
Quando: aggiornare driver per periferica, aggiungere nuova syscall, o modificare interrupt handling

### Input
- Codice nuovo/modificato
- Attore minaccia (local attacker con user shell, remote network attacker, DMA-capable device)

### Processo

1. **Threat Matrix per Componente**
   ```
   THREAT               | ATTACK VECTOR          | MITIGATION
   ─────────────────────┼────────────────────────┼──────────────────
   Privilege Escalation | Exploit syscall bounds | Input validation
   Memory Disclosure    | Read kernel heap       | U/S, copie fallibili, ownership
   DMA Attacks          | Rogue PCIe device      | IOMMU setup
   Interrupt Hijacking  | Malicious IRQ handler  | IDT write-protection
   Spectre/Meltdown     | Cache timing           | Mitigazioni CPU-specifiche; KPTI non copre tutto Spectre
   ```

2. **Valutazione Risk**
   - **CRITICAL**: Può eseguire codice arbitrario in Ring 0
   - **HIGH**: Può leggere/scrivere kernel memory
   - **MEDIUM**: Denial of service (crash kernel)
   - **LOW**: Information leak non critica

3. **Mitigation Checklist per Datasheet**
   ```
   Driver per [device]:
   - [ ] IOMMU enabled for DMA (if applicable)
   - [ ] Shared IRQ state has synchronization appropriate to BSP/SMP and ownership
   - [ ] Device memory mapped read-only where possible
   - [ ] Input from device validated before use
   - [ ] Rate limiting on interrupts
   - [ ] No unbounded loops triggered by device
   ```

### Output
- ✅ Threat model in commenti sopra il codice sensibile
- ✅ Mitigazioni implementate nel codice
- ✅ Test case per ogni threat (es. fuzzing input)
- ❌ BLOCCA se: threat CRITICAL senza mitigation, IOMMU non usato per DMA

### Checklist Threat Model
```
[ ] Threat matrix completata
[ ] Risk level assegnato (CRITICAL/HIGH/MEDIUM/LOW)
[ ] Mitigation per ogni threat CRITICAL
[ ] IOMMU setup (se DMA device)
[ ] Input validation da device
[ ] Interrupt rate limiting
[ ] Test per denial-of-service attempt
```

---

## Skill 5: code-quality-gates

### Trigger
Sempre prima di proporre codice

### Input
- File da controllare
- Linguaggio (C, ASM, Rust)

### Processo

1. **Separatori Obbligatori**
   ```c
   // ====== device_init ======

   int device_init(void) { ... }

   // ====== device_read ======

   uint32_t device_read(uint32_t offset) { ... }
   ```

2. **Conteggio Righe Automatico**
   - Esegui `wc -l file.c`
   - Se > 400: applica skill `long-driver-modularization` IMMEDIATAMENTE
   - Non proporre il file, proponi il refactor

3. **Static Analysis Minima**
   ```bash
   # Compilare con la toolchain supportata del modulo e warning come errori
   cppcheck --enable=all      # analisi statica
   ```
   - Zero warning tollerati
   - BLOCCA se compilation fallisce

4. **Datasheet Cross-Check (Hardware)**
   - Tabella di ogni offset register con commento linea-per-linea
   - Se un registro è specificato nel datasheet, non inventarne uno nuovo

### Output
- ✅ File compila senza warning
- ✅ Separatori presenti e formattati
- ✅ Ogni offset documentato con linea del datasheet
- ✅ Nessun file > 400 righe proposto
- ❌ BLOCCA se: compilazione fallisce, warning generati, offset non documentato

---

## Global Execution Order

Quando aggiornare OS:

```
1. Scrivi test case (QEMU script o unit test)
2. Esegui Skill #4 (threat modeling su nuovo codice)
3. Esegui Skill #5 (code quality gates)
4. Esegui Skill #1 (hardware mapper se periferica)
5. Esegui Skill #2 (security enforcement su syscall/memory)
6. Esegui Skill #3 (modularization se > 400 righe)
7. Compila + testa in QEMU
8. Proponi codice
```

---

## Datasheet Reference Template

Per ogni registro inserire esclusivamente dati verificati:
- Device/revisione e link del datasheet.
- Sezione/tabella, offset, larghezza e semantica di accesso.
- Bit/maschera, campi riservati e motivo dell'operazione.
- Prerequisiti PCI/ACPI, mapping, cache/ordering e sequenza init/stop.
- Test con hardware/mock appropriato e limiti della prova.

Non riutilizzare gli offset CTRL/STATUS/RX_DMA del vecchio esempio 82599: erano placeholder non verificati. Finche manca la fonte esatta, nessun accesso hardware va implementato.

---

## Command Reference

**Apply skill during development:**
```
> Apply Skill #1: hardware-real-device-mapper
  Input: PCIe NIC for 82599

> Apply Skill #2: cybersecurity-kernel-enforcement
  Input: new syscall handler

> Apply Skill #3: long-driver-modularization
  Input: drivers/storage/init.c (420 righe)

> Apply Skill #4: cybersecurity-threat-modeling
  Input: DMA controller driver

> Apply Skill #5: code-quality-gates
  Input: drivers/network/io_ops.c
```

---

## Notes

- Questo file è **operativo**: ogni skill ha trigger chiari, input/output misurabili, checklist binaria
- Non è narrativo: istruzioni eseguibili passo-passo
- Cybersecurity è integrata come controllo attivo, non opzionale
- Driver modulari mantengono la codebase leggibile anche a 50k LOC
- Ogni skill è indipendente ma ordinate nell'execution order globale
