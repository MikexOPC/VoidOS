use crate::evidence::results::{Value, object};
use crate::evidence::trace;
use crate::evidence::serial::SerialLog;
use crate::vm::machine::{Limits, Paths, Plan, Vm};
use std::path::Path;
use std::time::{Duration, Instant};

pub const SHELL_CASE: u32 = 1;
pub const SHELL_BRIDGE_FAULT_CASE: u32 = 2;
pub const NO_PS2_CASE: u32 = 3;
// --ordinary-soak N is encoded as SOAK_CASE_BASE + N minutes.
pub const SOAK_CASE_BASE: u32 = 1000;
const SOAK_PAUSE: Duration = Duration::from_secs(5);
const UMIP: u64 = 1 << 11;
const SMEP: u64 = 1 << 20;
const SMAP: u64 = 1 << 21;
const CR4_DEADLINE: Duration = Duration::from_secs(5);
const CR4_POLL: Duration = Duration::from_millis(10);
const IF: u64 = 1 << 9;
const INIT_LINE: &[u8] = b"USER: VoidOS init, ring3";
// The ordinary image ends in the shell: success is the shell waiting for input.
const SHELL_READY: &str = "KERNEL_PS2_INPUT_READY";

// ====== expected_protection ======

fn expected_protection(cpu: &str) -> u64 {
    match cpu {
        "max,-smap" => SMEP | UMIP,
        "max,-smep" => SMAP | UMIP,
        "max,-smep,-smap" => UMIP,
        _ => SMEP | SMAP | UMIP,
    }
}

// ====== parse_cr4 ======

pub fn parse_cr4(bytes: &[u8], cpu: u32) -> Option<u64> {
    parse_register(bytes, cpu, b"CR4=")
}

// ====== parse_register ======

pub fn parse_register(bytes: &[u8], cpu: u32, name: &[u8]) -> Option<u64> {
    let mut matching_cpu = false;
    for line in bytes.split_inclusive(|byte| *byte == b'\n') {
        let Some(line) = line.strip_suffix(b"\n") else {
            break;
        };
        let line = line.trim_ascii();
        if let Some(index) = line.strip_prefix(b"CPU#") {
            matching_cpu = std::str::from_utf8(index).ok().and_then(|index| index.parse::<u32>().ok()) == Some(cpu);
        } else if matching_cpu {
            let mut fields = line.split(|byte| byte.is_ascii_whitespace()).filter(|field| !field.is_empty());
            while let Some(field) = fields.next() {
                if let Some(value) = field.strip_prefix(name) {
                    let value = if value.is_empty() { fields.next()? } else { value };
                    return u64::from_str_radix(std::str::from_utf8(value).ok()?, 16).ok();
                }
            }
        }
    }
    None
}

// ====== read_cr4 ======

fn read_cr4(vm: &mut Vm, cpu: u32) -> Result<u64, String> {
    Ok(read_registers(vm, cpu, &[b"CR4="])?[0])
}

// ====== read_registers ======

fn read_registers(vm: &mut Vm, cpu: u32, names: &[&[u8]]) -> Result<Vec<u64>, String> {
    vm.command(&format!("cpu {cpu}"));
    let offset = vm.monitor_size()? as usize;
    vm.command("info registers");
    let deadline = Instant::now() + CR4_DEADLINE;
    while Instant::now() < deadline {
        let bytes = vm.monitor_bytes()?;
        if let Some(values) = bytes.get(offset..).and_then(|bytes| {
            names.iter().map(|name| parse_register(bytes, cpu, name)).collect::<Option<Vec<_>>>()
        }) {
            return Ok(values);
        }
        std::thread::sleep(CR4_POLL);
    }
    Err(format!("Missing register snapshot of CPU {cpu}"))
}

// ====== ordinary_boot ======

fn ordinary_boot(vm: &mut Vm, plan: &Plan) -> Result<Value, String> {
    let log = vm.wait(SHELL_READY, 1, Some("OK"))?;
    if !log.failures().is_empty() || log.terminal() {
        return Err(format!("Unexpected kernel failure {:?}", log.failures()));
    }
    let expected = expected_protection(plan.cpu);
    if log.number("KERNEL_USER_PROTECTION", 0)? != expected {
        return Err("Unexpected reported protection".to_string());
    }
    let full = expected & (SMEP | SMAP) == SMEP | SMAP;
    if log.has("KERNEL_USER_MODE_ALLOWED") != full || log.has("USER_MODE_DISABLED") == full {
        return Err("Unexpected user mode decision".to_string());
    }
    let serial = std::fs::read(&vm.serial).map_err(|error| format!("Serial: {error}"))?;
    let printed = serial.windows(INIT_LINE.len()).any(|window| window == INIT_LINE);
    if full && (!printed || !log.before("KERNEL_PROCESS_OK", SHELL_READY)) {
        return Err("The first process did not run".to_string());
    }
    if !full && (printed || log.has("KERNEL_PROCESS_OK") || !log.has("PROCESS_DISABLED")) {
        return Err("A CPU without protections ran a process".to_string());
    }
    if !log.before("KERNEL_ENTROPY_READY", SHELL_READY) ||
        log.number("KERNEL_ENTROPY_SOURCE", 0)? < 1 {
        return Err("Hardware entropy or CSPRNG failed to initialize".to_string());
    }
    if !log.before("KERNEL_VIRTIO_BLK_READY", SHELL_READY) ||
        log.number("KERNEL_VIRTIO_BLK_SECTORS", 0)? != 2048 {
        return Err("VirtIO-blk block device did not start".to_string());
    }
    if !log.before("KERNEL_AHCI_READY", SHELL_READY) ||
        log.number("KERNEL_AHCI_SECTORS", 0)? == 0 {
        return Err("AHCI SATA block device did not start".to_string());
    }
    if !log.before("KERNEL_VFS_READY", SHELL_READY) ||
        (full && !log.before("KERNEL_VFS_EXEC_OK", SHELL_READY)) ||
        (full && !log.before("KERNEL_USER_SHELL_OK", SHELL_READY)) ||
        log.number("KERNEL_VFS_MOUNTS", 0)? != 2 ||
        log.number("KERNEL_VFS_ROOT_BYTES", 0)? != 21 ||
        log.number("KERNEL_VFS_BOOT_BYTES", 0)? != 15 {
        return Err("VFS FAT mount or file verification failed".to_string());
    }
    for cpu in 0..plan.smp {
        if read_cr4(vm, cpu)? & (SMEP | SMAP | UMIP) != expected {
            return Err(format!("Unexpected CR4 on CPU {cpu}"));
        }
    }
    let bytes = vm.trace_bytes()?;
    if !trace::user_faults(&bytes).is_empty() || trace::triple_fault(&bytes) {
        return Err("Unexpected fault in the ordinary boot".to_string());
    }
    Ok(object(vec![
        ("Cpu", Value::Text(plan.cpu.to_string())),
        ("Protection", Value::Number(expected)),
        ("Cpus", Value::Number(u64::from(plan.smp))),
        ("Process", Value::Bool(full)),
    ]))
}

// ====== preemption_evidence ======

pub fn preemption_evidence(log: &SerialLog) -> Result<Value, String> {
    let outside = log.number("KERNEL_PREEMPT_OUTSIDE_WINDOW", 0)?;
    let suspended = log.number("KERNEL_PREEMPT_SUSPENDED", 0)?;
    let overruns = log.number("KERNEL_PREEMPT_OVERRUNS", 0)?;

    if outside == 0 || outside > suspended || overruns != 0 {
        return Err(format!("Invalid production preemption: outside={outside}, suspended={suspended}, overruns={overruns}"));
    }

    Ok(object(vec![
        ("OutsideWindow", Value::Number(outside)),
        ("Suspended", Value::Number(suspended)),
        ("Overruns", Value::Number(overruns)),
    ]))
}

// ====== dispatcher_evidence ======

pub fn dispatcher_evidence(log: &SerialLog) -> Result<Value, String> {
    let samples = log.events.iter().filter(|event| event.name == "KERNEL_DISPATCH_IDLE_OK" && event.level == "OK").count();
    let cycles = log.numbers("KERNEL_DISPATCH_IDLE_CYCLES");
    let ticks = log.numbers("KERNEL_DISPATCH_TIMER_TICKS");
    let pages = log.numbers("KERNEL_DISPATCH_FREE_PAGES");
    if samples < 2 || cycles.len() < samples || ticks.len() < samples || pages.len() < samples {
        return Err("Missing complete BSP dispatcher samples".to_string());
    }
    let cycles = &cycles[..samples];
    let ticks = &ticks[..samples];
    let pages = &pages[..samples];
    if !log.failures().is_empty() || log.terminal() || cycles[0] == 0 ||
        cycles.windows(2).any(|pair| pair[1] <= pair[0]) ||
        ticks.windows(2).any(|pair| pair[1] <= pair[0]) || pages.iter().any(|value| *value != pages[0]) ||
        !log.before("KERNEL_READY", "KERNEL_DISPATCHER_READY") ||
        !log.before("KERNEL_DISPATCHER_READY", "KERNEL_DISPATCH_IDLE_OK") {
        return Err("Invalid persistent BSP dispatcher evidence".to_string());
    }
    if log.has("KERNEL_SCHEDULER_CLEANUP_OK") &&
        (log.number("SCHEDULER_BUDGET_BEFORE", 0)? != pages[0] ||
         log.number("SCHEDULER_BUDGET_AFTER", 0)? != pages[0]) {
        return Err("The input session changed the BSP page budget".to_string());
    }
    Ok(object(vec![
        ("IdleCycles", Value::Number(*cycles.last().unwrap())),
        ("TimerTicks", Value::Number(*ticks.last().unwrap())),
        ("FreePages", Value::Number(pages[0])),
    ]))
}

// ====== parse_memory ======

pub fn parse_memory(bytes: &[u8], address: u64, count: usize) -> Option<Vec<u64>> {
    let mut words = Vec::new();
    for line in std::str::from_utf8(bytes).ok()?.split_inclusive('\n') {
        let Some(line) = line.strip_suffix('\n') else { break };
        let Some((location, values)) = line.trim().split_once(':') else { continue };
        let location = location.strip_prefix("0x").unwrap_or(location);
        let Ok(location) = u64::from_str_radix(location, 16) else { continue };
        if location != address.checked_add((words.len() * 8) as u64)? {
            continue;
        }
        for value in values.split_whitespace() {
            let digits = value.strip_prefix("0x")?;
            if digits.len() != 16 {
                return None;
            }
            words.push(u64::from_str_radix(digits, 16).ok()?);
            if words.len() == count {
                return Some(words);
            }
        }
    }
    None
}

// ====== read_memory ======

fn read_memory(vm: &mut Vm, address: u64, count: usize) -> Result<Vec<u64>, String> {
    let offset = vm.monitor_size()? as usize;
    vm.command("cpu 0");
    vm.command(&format!("x /{count}gx 0x{address:016x}"));
    let deadline = Instant::now() + CR4_DEADLINE;
    while Instant::now() < deadline {
        let bytes = vm.monitor_bytes()?;
        if let Some(words) = bytes.get(offset..).and_then(|bytes| parse_memory(bytes, address, count)) {
            return Ok(words);
        }
        std::thread::sleep(CR4_POLL);
    }
    Err("Missing IRQ registry memory snapshot".to_string())
}

// ====== ap_dispatchers ======

fn ap_dispatchers(vm: &mut Vm, cpus: u32) -> Result<Value, String> {
    let log = vm.log();
    let base = log.number("KERNEL_AP_DISPATCH_LOCALS",0)?;
    let stride = log.number("KERNEL_AP_DISPATCH_LOCAL_BYTES",0)?;
    let idle_rip = log.number("KERNEL_AP_DISPATCH_IDLE_RIP",0)?;
    let jobs = log.numbers("KERNEL_AP_TASK_JOBS").first().copied().unwrap_or(0);
    if stride != 704 || log.number("KERNEL_AP_DISPATCH_COUNT",0)? != u64::from(cpus - 1) {
        return Err("Invalid permanent AP dispatcher storage".to_string());
    }
    let mut minimum_idle = u64::MAX;
    let has_batch = !log.numbers("SHELL_AP_TASKS").is_empty();
    let mut total_completed = 0u64;
    for cpu in 1..cpus {
        let deadline = Instant::now() + CR4_DEADLINE;
        loop {
            let registers = read_registers(vm,cpu,&[b"RFL=",b"HLT=",b"RIP="])?;
            if registers[0] & IF != 0 && registers[1] == 1 && registers[2] == idle_rip { break; }
            if Instant::now() >= deadline {
                return Err(format!("AP {cpu} did not idle with interrupts enabled"));
            }
        }
        let address = base.checked_add(stride * u64::from(cpu)).ok_or("AP storage overflow")?;
        let words = read_memory(vm,address,(stride / 8) as usize)?;
        let completed = words[74] >> 32;
        let expected_cpu = log.numbers("SHELL_AP_CPU").iter().filter(|owner| **owner == u64::from(cpu)).count() as u64;
        let count_ok = if has_batch { completed >= expected_cpu } else { completed == expected_cpu };
        if words[78] != 0 || words[80] != 0xffff_ffff_0000_0001 || words[81] != 0 ||
            words[82] != address || words[83] == 0 || words[85] != 1 || !count_ok {
            return Err(format!("AP {cpu} lost its permanent dispatcher or job ownership"));
        }
        total_completed += completed;
        minimum_idle = minimum_idle.min(words[84]);
    }
    Ok(object(vec![("Cpus",Value::Number(u64::from(cpus - 1))),
        ("CompletedJobs",Value::Number(if has_batch { total_completed } else { jobs })),
        ("MinimumIdleCycles",Value::Number(minimum_idle))]))
}

// ====== ap_preemption ======

fn ap_preemption(vm: &mut Vm, cpus: u32) -> Result<Value, String> {
    let log = vm.log();
    if log.numbers("KERNEL_PREEMPT_CPUS").is_empty() { return Ok(object(vec![])); }
    let base = log.number("KERNEL_PREEMPT_CPUS",0)?;
    let stride = log.number("KERNEL_PREEMPT_CPU_BYTES",0)?;
    if stride != 320 { return Err("Invalid per-CPU preemption storage".to_string()); }
    let mut records = Vec::new();
    for cpu in 1..cpus {
        let deadline = Instant::now() + vm.limits.wait;
        let address = base + stride * u64::from(cpu);
        let initial = read_memory(vm,address,(stride / 8) as usize)?;
        let words = loop {
            let words = read_memory(vm,address,(stride / 8) as usize)?;
            if words[15] != 1 && words[11] > initial[11] { break words }
            if Instant::now() >= deadline {
                return Err(format!("AP {cpu} timer did not resume and advance after session cleanup"));
            }
            std::thread::sleep(CR4_POLL);
        };
        let visits = log.numbers("SHELL_AP_CPUS");
        let long_job = log.numbers("SHELL_AP_CPU").iter().zip(log.numbers("SHELL_AP_COUNT")).enumerate()
            .any(|(index,(owner,count))| *owner == u64::from(cpu) && *count == 100_000_000 &&
                visits.get(index).map_or(true,|mask| mask.count_ones() == 1));
        if words[0] == 0 || words[1] == 0 || words[2] != 0xd1 || words[4] == 0 || words[11] == 0 ||
            words[6] != words[14] || words[7] != 0 || words[8] != 0 || words[9] != 0 ||
            words[15] != 0x0000_0001_0000_0001 || (long_job && words[6] == 0) {
            return Err(format!("Invalid timer/preemption state on AP {cpu}"));
        }
        records.push(object(vec![("Cpu",Value::Number(u64::from(cpu))),
            ("Ticks",Value::Number(words[11])),("Suspended",Value::Number(words[6])),
            ("Resumed",Value::Number(words[14]))]));
    }
    Ok(Value::List(records))
}

// ====== migration_evidence ======

pub fn migration_evidence(log: &SerialLog, cpus: u32) -> Result<Value,String> {
    if !log.has("KERNEL_AP_MIGRATION_READY") { return Ok(object(vec![])); }
    if cpus == 0 || cpus > 64 { return Err("Invalid migration CPU count".to_string()); }
    let allowed = if cpus == 64 { u64::MAX } else { (1u64 << cpus) - 1 };
    let owners = log.numbers("SHELL_AP_CPU");
    let counts = log.numbers("SHELL_AP_COUNT");
    let checksums = log.numbers("SHELL_AP_CHECKSUM");
    let visits = log.numbers("SHELL_AP_CPUS");
    let migrations = log.numbers("SHELL_AP_MIGRATIONS");
    if [counts.len(),checksums.len(),visits.len(),migrations.len()].iter().any(|length| *length != owners.len()) {
        return Err("Incomplete migration job records".to_string());
    }
    let batch_record = !log.numbers("SHELL_AP_TASKS").is_empty();
    let mut checks = std::collections::BTreeMap::new();
    let (mut total,mut migrated_jobs,mut visited,mut migrating_cpus) = (0,0,0,0);
    for index in 0..owners.len() {
        let mask = visits[index];
        let moves = migrations[index];
        let is_batch = batch_record && index + 1 == owners.len();
        if owners[index] >= u64::from(cpus) || mask == 0 || mask & !allowed != 0 ||
            mask & (1u64 << owners[index]) == 0 ||
            (!is_batch && moves < u64::from(mask.count_ones() - 1)) || (mask.count_ones() == 1 && moves != 0) {
            return Err(format!("Invalid native task migration at job {index}"));
        }
        if let Some(expected) = checks.insert(counts[index],checksums[index]) {
            if expected != checksums[index] {
                return Err(format!("Job checksum changed across CPU contexts at job {index}"));
            }
        }
        total += moves;
        visited |= mask;
        if moves != 0 {
            migrated_jobs += 1;
            migrating_cpus |= mask;
        }
    }
    Ok(object(vec![("Jobs",Value::Number(owners.len() as u64)),
        ("MigratedJobs",Value::Number(migrated_jobs)),("Migrations",Value::Number(total)),
        ("VisitedCpus",Value::Number(visited)),("MigratingCpus",Value::Number(migrating_cpus))]))
}

// ====== irq_ownership ======

fn irq_ownership(vm: &mut Vm, cpus: u32) -> Result<Value, String> {
    let log = vm.log();
    let address = log.number("KERNEL_IRQ_REGISTRY", 0)?;
    let bytes = log.numbers("KERNEL_IRQ_REGISTRY_BYTES").first().copied().unwrap_or(240);
    if bytes != 240 && bytes != 9000 {
        return Err("Unsupported IRQ registry layout".to_string());
    }
    let words = read_memory(vm,address,(bytes / 8) as usize)?;
    let mut shared_cpus = 0;
    let mut ipi_subscriptions = 0;
    let preemptive = log.has("KERNEL_AP_PREEMPT_READY");
    if words[0] != address || words[5] == 0 || words[9] != 0x40 ||
        words[10..25].iter().any(|word| *word != 0) || words[25] == 0 || words[29] != 0xd1 ||
        words[30..].iter().any(|word| *word != 0) {
        return Err("IRQ ownership after PS/2 cleanup is invalid".to_string());
    }
    if bytes == 9000 {
        let base = log.number("KERNEL_IRQ_CPUS",0)?;
        let stride = log.number("KERNEL_IRQ_CPU_BYTES",0)?;
        if base != address || stride < bytes || stride % 64 != 0 {
            return Err("Invalid per-CPU IRQ storage".to_string());
        }
        for cpu in 0..cpus {
            if read_registers(vm,cpu,&[b"IDT="])?[0] != log.number("KERNEL_IRQ_IDT_BASE",0)? {
                return Err(format!("CPU {cpu} did not load the shared IRQ IDT"));
            }
            shared_cpus += 1;
            if cpu == 0 { continue; }
            let address = base.checked_add(stride.checked_mul(u64::from(cpu)).ok_or("IRQ storage overflow")?)
                .ok_or("IRQ storage overflow")?;
            let registers = read_memory(vm,address,(bytes / 8) as usize)?;
            let active: Vec<_> = registers[5..].chunks_exact(5).filter(|slot| slot[0] != 0).collect();
            if registers[0] != address || active.len() != if preemptive { 2 } else { 1 } ||
                !active.iter().any(|slot| slot[4] == 0xe1 && slot[3] != 0) ||
                (preemptive && !active.iter().any(|slot| slot[4] == 0xd1 && slot[3] != 0)) {
                return Err(format!("Invalid IPI ownership on CPU {cpu}"));
            }
            ipi_subscriptions += 1;
        }
    }
    let dispatchers = if log.has("KERNEL_AP_DISPATCHERS_READY") {
        ap_dispatchers(vm,cpus)?
    } else { object(vec![]) };
    let preemption = ap_preemption(vm,cpus)?;
    Ok(object(vec![
        ("HpetSubscription", Value::Number(words[5])),
        ("TimerSubscription", Value::Number(words[25])),
        ("ReleasedDeviceSlots", Value::Number(3)),
        ("RegistryBytes", Value::Number(bytes)),
        ("SharedIdtCpus", Value::Number(shared_cpus)),
        ("ApIpiSubscriptions", Value::Number(ipi_subscriptions)),
        ("ApDispatchers", dispatchers),
        ("ApPreemption", preemption),
    ]))
}

// ====== wait_dispatcher ======

fn wait_dispatcher(vm: &mut Vm) -> Result<Value, String> {
    let log = vm.wait("KERNEL_DISPATCH_IDLE_OK", 2, Some("OK"))?;
    let evidence = dispatcher_evidence(&log)?;
    let deadline = Instant::now() + CR4_DEADLINE;
    while Instant::now() < deadline {
        let registers = read_registers(vm, 0, &[b"RFL=", b"HLT="])?;
        if registers[0] & IF != 0 && registers[1] == 1 {
            return Ok(evidence);
        }
    }
    Err("BSP dispatcher did not idle with interrupts enabled".to_string())
}

// ====== shell_boot ======

fn shell_boot(vm: &mut Vm, plan: &Plan, fault: bool) -> Result<Value, String> {
    let log = vm.wait("KERNEL_PS2_ROUTE_ACQUIRED", 1, Some("INFO"))?;
    let enabled = expected_protection(plan.cpu) & (SMEP | SMAP) == SMEP | SMAP;
    if !log.failures().is_empty() || log.terminal() ||
        (enabled && !log.before("KERNEL_PROCESS_OK", "KERNEL_PS2_ROUTE_ACQUIRED")) ||
        (!enabled && !log.before("PROCESS_DISABLED", "KERNEL_PS2_ROUTE_ACQUIRED")) {
        return Err("Unexpected user-mode initialization before the input session".to_string());
    }
    let serial = std::fs::read(&vm.serial).map_err(|error| format!("Serial: {error}"))?;
    if serial.windows(INIT_LINE.len()).any(|window| window == INIT_LINE) != enabled {
        return Err("Unexpected first-process output".to_string());
    }
    vm.wait("KERNEL_PS2_INPUT_READY", 1, Some("OK"))?;
    let marker = if enabled && !fault { "SHELL_BRIDGE_STATUS_OK" } else { "SHELL_BRIDGE_UNAVAILABLE" };
    for count in 1..=2 {
        shell_type(vm, "bridge status")?;
        vm.wait(marker, count, None)?;
    }
    if enabled && !fault {
        shell_type(vm, "proc")?;
        let proc_log = vm.wait("SHELL_PROC_OK", 1, Some("OK"))?;
        let expected_ap_cpu = if plan.smp > 1 { 1 } else { 0 };
        if proc_log.number("SHELL_PROC_SPAWNED", 0)? != 5
            || proc_log.number("SHELL_PROC_PEAK_ACTIVE", 0)? != 6
            || proc_log.number("SHELL_PROC_IPC_OK", 0)? != 4
            || proc_log.number("SHELL_PROC_FAULT_VECTOR", 0)? != 6
            || proc_log.number("SHELL_PROC_REAPED", 0)? != 5
            || proc_log.number("SHELL_PROC_AP_CPU", 0)? != expected_ap_cpu
        {
            return Err("Unexpected multi-process IPC/fault counts".to_string());
        }
        shell_type(vm, "bridge status")?;
        vm.wait(marker, 3, None)?;
        shell_type(vm, "fl")?;
        let ls_log = vm.wait("SHELL_LS_OK", 1, Some("OK"))?;
        if ls_log.number("SHELL_LS_COUNT", 0)? < 1 {
            return Err("Expected at least 1 root entry in fl".to_string());
        }
        shell_type(vm, "write note voidos")?;
        let write_log = vm.wait("SHELL_WRITE_OK", 1, Some("OK"))?;
        if write_log.number("SHELL_WRITE_BYTES", 0)? != 6 {
            return Err("Unexpected byte count written by shell write".to_string());
        }
        shell_type(vm, "rp note")?;
        let cat_log = vm.wait("SHELL_CAT_OK", 1, Some("OK"))?;
        if cat_log.number("SHELL_CAT_BYTES", 0)? != 6 || !cat_log.text.contains("SHELL_CAT: voidos") {
            return Err("Unexpected content returned by shell rp".to_string());
        }
        shell_type(vm, "rm note")?;
        vm.wait("SHELL_RM_OK", 1, Some("OK"))?;
        shell_type(vm, "sync")?;
        vm.wait("SHELL_SYNC_OK", 1, Some("OK"))?;
    }
    let remote = plan.smp > 1 && !fault;
    let preemptive = vm.log().has("KERNEL_AP_PREEMPT_READY");
    let mut expected_cpus = Vec::new();
    let mut expected_counts = Vec::new();
    for _ in 0..if remote { 3 } else { 0 } {
        shell_type(vm, "ap 1000")?;
        expected_cpus.push(1);
        expected_counts.push(1000);
    }
    if remote && preemptive {
        for cpu in 1..plan.smp {
            shell_type(vm,&format!("ap 100000000 {cpu}"))?;
            expected_cpus.push(u64::from(cpu));
            expected_counts.push(100_000_000);
        }
    }
    if remote && vm.log().has("KERNEL_AP_MIGRATION_READY") {
        shell_type(vm,"ap 100000000 0")?;
        expected_cpus.push(0);
        expected_counts.push(100_000_000);
        shell_type(vm,"ap 100000000 auto")?;
        let log = vm.wait_field("SHELL_AP_SUM",expected_cpus.len() + 1)?;
        expected_cpus.push(log.number("SHELL_AP_CPU",expected_cpus.len())?);
        expected_counts.push(100_000_000);
        shell_type(vm,"ap 1000000 auto 16")?;
        let log = vm.wait_field("SHELL_AP_SUM",expected_cpus.len() + 1)?;
        if log.number("SHELL_AP_TASKS", 0)? != 16 || log.number("SHELL_AP_MAX_INDEX", 0)? < 16 {
            return Err("Batch of 16 simultaneous tasks did not exceed the old 8-task and 16-slot PT bounds".to_string());
        }
        expected_cpus.push(log.number("SHELL_AP_CPU",expected_cpus.len())?);
        expected_counts.push(1_000_000);
    }
    let jobs = expected_cpus.len() as u64;
    shell_type(vm, "status")?;
    shell_type(vm, "exit")?;
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    let proc_cmds = if enabled && !fault { 7 } else { 0 };
    if !log.failures().is_empty() || log.number("SHELL_COMMAND_COUNT", 0)? != 4 + proc_cmds + jobs ||
        !log.text.contains("SHELL_STATUS: BSP PS2, kernel tasks") ||
        !log.before("SHELL_EXIT_REQUEST", "KERNEL_PS2_INPUT_CLEANUP_OK") ||
        !log.before("KERNEL_PS2_INPUT_CLEANUP_OK", "KERNEL_READY") {
        return Err("The input session did not clean up".to_string());
    }
    if !log.before("KERNEL_SCHEDULER_CLEANUP_OK", "KERNEL_PS2_INPUT_CLEANUP_OK") ||
        !log.has("KERNEL_PS2_MOUSE_CLEANUP_OK") || !log.has("KERNEL_CONSOLE_SHELL_CLEAN") {
        return Err("Missing collected stacks, mouse or console cleanup".to_string());
    }
    let sums: Vec<u64> = expected_counts.iter().map(|count| count * (count + 1) / 2).collect();
    let proc_ap_jobs = if enabled && !fault && remote { 1 } else { 0 };
    if remote && (log.numbers("SHELL_AP_CPU") != expected_cpus ||
        log.numbers("SHELL_AP_COUNT") != expected_counts ||
        log.numbers("SHELL_AP_SUM") != sums || log.number("KERNEL_AP_TASK_JOBS", 0)? != jobs + proc_ap_jobs ||
        !log.before("KERNEL_AP_TASKS_CLEAN", "KERNEL_PS2_INPUT_CLEANUP_OK")) {
        return Err("AP job or its session cleanup is invalid".to_string());
    }
    let trace_bytes = vm.trace_bytes()?;
    let expected = if enabled {
        vec![("06".to_string(), "0000".to_string()), ("06".to_string(), "0000".to_string())]
    } else if fault {
        vec![("06".to_string(), "0000".to_string())]
    } else {
        Vec::new()
    };
    if trace::user_faults(&trace_bytes) != expected || trace::triple_fault(&trace_bytes) {
        return Err("Unexpected fault during the shell session".to_string());
    }
    let preemption = preemption_evidence(&log)?;
    let dispatcher = wait_dispatcher(vm)?;
    let ownership = irq_ownership(vm,plan.smp)?;
    let migration = migration_evidence(&log,plan.smp)?;
    Ok(object(vec![
        ("Process", Value::Bool(enabled)),
        ("Bridge", Value::Bool(enabled && !fault)),
        ("Preemption", preemption),
        ("Dispatcher", dispatcher),
        ("IrqOwnership", ownership),
        ("ApJob", Value::Bool(remote)),
        ("Migration", migration),
    ]))
}

// ====== no_ps2_boot ======

// Machine without a PS/2 controller: no shell, the process still ran and the BSP stays idle
// with interrupts enabled instead of stopping.
fn no_ps2_boot(vm: &mut Vm, plan: &Plan) -> Result<Value, String> {
    let log = vm.wait("KERNEL_DISPATCHER_READY", 1, Some("OK"))?;
    let enabled = expected_protection(plan.cpu) & (SMEP | SMAP) == SMEP | SMAP;
    if !log.failures().is_empty() || log.terminal() {
        return Err(format!("Unexpected kernel failure {:?}", log.failures()));
    }
    if !log.before("KERNEL_PS2_INPUT_ABSENT", "KERNEL_READY") || log.has(SHELL_READY) {
        return Err("The image did not report the missing PS/2 controller".to_string());
    }
    if enabled && !log.before("KERNEL_PROCESS_OK", "KERNEL_READY") {
        return Err("The first process did not run".to_string());
    }
    if !log.before("KERNEL_ENTROPY_READY", "KERNEL_READY") ||
        log.number("KERNEL_ENTROPY_SOURCE", 0)? < 1 {
        return Err("Hardware entropy or CSPRNG failed without PS/2".to_string());
    }
    if !log.before("KERNEL_VIRTIO_BLK_READY", "KERNEL_READY") ||
        log.number("KERNEL_VIRTIO_BLK_SECTORS", 0)? != 2048 {
        return Err("VirtIO-blk block device did not start without PS/2".to_string());
    }
    if !log.before("KERNEL_AHCI_READY", "KERNEL_READY") ||
        log.number("KERNEL_AHCI_SECTORS", 0)? == 0 {
        return Err("AHCI SATA block device did not start without PS/2".to_string());
    }
    if !log.before("KERNEL_VFS_READY", "KERNEL_READY") ||
        (enabled && !log.before("KERNEL_VFS_EXEC_OK", "KERNEL_READY")) ||
        (enabled && !log.before("KERNEL_USER_SHELL_OK", "KERNEL_READY")) ||
        log.number("KERNEL_VFS_MOUNTS", 0)? != 2 ||
        log.number("KERNEL_VFS_ROOT_BYTES", 0)? != 21 ||
        log.number("KERNEL_VFS_BOOT_BYTES", 0)? != 15 {
        return Err("VFS FAT mount or file verification failed without PS/2".to_string());
    }
    if !log.before("KERNEL_READY", "KERNEL_DISPATCHER_READY") {
        return Err("The idle loop started before the boot completed".to_string());
    }
    let dispatcher = wait_dispatcher(vm)?;
    let bytes = vm.trace_bytes()?;
    let expected = if enabled { vec![("06".to_string(), "0000".to_string())] } else { Vec::new() };
    if trace::user_faults(&bytes) != expected || trace::triple_fault(&bytes) {
        return Err("Unexpected fault without a PS/2 controller".to_string());
    }
    let ownership = irq_ownership(vm,plan.smp)?;
    Ok(object(vec![("Ps2", Value::Bool(false)), ("Process", Value::Bool(enabled)),
        ("Dispatcher", dispatcher), ("IrqOwnership", ownership)]))
}

// ====== soak_boot ======

// Long shell session: bridge status round trips until the deadline, then exit. The kernel checks
// at session close that the physical page budget is unchanged.
fn soak_boot(vm: &mut Vm, plan: &Plan, minutes: u32) -> Result<Value, String> {
    for event in ["input_event_key_qcode", "ps2_put_keycode", "pckbd_kbd_read_data"] {
        vm.command(&format!("trace-event {event} on"));
    }
    let log = vm.wait("KERNEL_PS2_INPUT_READY", 1, Some("OK"))?;
    if expected_protection(plan.cpu) & (SMEP | SMAP) != SMEP | SMAP || !log.has("KERNEL_PROCESS_OK") {
        return Err("The soak needs the bridge service: SMEP, SMAP and the first process".to_string());
    }
    vm.inject_on("sendkey b 20");
    let deadline = Instant::now() + Duration::from_secs(u64::from(minutes) * 60);
    let preemptive = log.has("KERNEL_AP_PREEMPT_READY");
    let migrating = log.has("KERNEL_AP_MIGRATION_READY");
    let mut rounds = 0usize;
    while Instant::now() < deadline {
        shell_type(vm, "bridge status")?;
        rounds += 1;
        let log = vm.wait("SHELL_BRIDGE_STATUS_OK", rounds, None)?;
        if !log.failures().is_empty() || log.terminal() {
            return Err(format!("Kernel failure after {rounds} rounds {:?}", log.failures()));
        }
        if plan.smp > 1 {
            let cpu = if migrating && rounds % 3 == 2 { 0 }
                else if preemptive { (rounds as u32 - 1) % (plan.smp - 1) + 1 } else { 1 };
            let count = if preemptive { 100_000_000 } else { 1000 };
            let automatic = migrating && rounds % 3 == 0;
            let command = if automatic { format!("ap {count} auto") }
                else if preemptive { format!("ap {count} {cpu}") } else { format!("ap {count}") };
            shell_type(vm,&command)?;
            let log = vm.wait_field("SHELL_AP_SUM",rounds)?;
            let actual_cpu = log.number("SHELL_AP_CPU",rounds - 1)?;
            if actual_cpu >= u64::from(plan.smp) || (!automatic && actual_cpu != u64::from(cpu)) ||
                log.number("SHELL_AP_COUNT",rounds - 1)? != count ||
                log.number("SHELL_AP_SUM",rounds - 1)? != count * (count + 1) / 2 {
                return Err(format!("Invalid AP job after {rounds} rounds"));
            }
        }
        std::thread::sleep(SOAK_PAUSE);
    }
    shell_type(vm, "exit")?;
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    let commands = log.number("SHELL_COMMAND_COUNT", 0)?;
    if !log.failures().is_empty() || commands != rounds as u64 * if plan.smp > 1 { 2 } else { 1 } + 1 ||
        (plan.smp > 1 && log.number("KERNEL_AP_TASK_JOBS",0)? != rounds as u64) ||
        !log.before("SHELL_EXIT_REQUEST", "KERNEL_PS2_INPUT_CLEANUP_OK") ||
        !log.before("KERNEL_PS2_INPUT_CLEANUP_OK", "KERNEL_READY") || log.has("KERNEL_IDLE") {
        return Err(format!("The long session did not close cleanly: {commands} commands, {rounds} rounds"));
    }
    let bytes = vm.trace_bytes()?;
    if !trace::user_faults(&bytes).is_empty() || trace::triple_fault(&bytes) {
        return Err("Unexpected fault during the long session".to_string());
    }
    let preemption = preemption_evidence(&log)?;
    let mut dispatcher = wait_dispatcher(vm)?;
    let idle_deadline = Instant::now() + Duration::from_secs(u64::from(minutes) * 60);
    let mut samples = 2;
    while Instant::now() < idle_deadline {
        samples += 1;
        let log = vm.wait("KERNEL_DISPATCH_IDLE_OK", samples, Some("OK")).map_err(|error| {
            let log = vm.log();
            format!("{error}; expected {samples} samples, found {}", log.count("KERNEL_DISPATCH_IDLE_OK", Some("OK")))
        })?;
        dispatcher = dispatcher_evidence(&log)?;
    }
    let ownership = irq_ownership(vm,plan.smp)?;
    let migration = migration_evidence(&log,plan.smp)?;
    if migrating && plan.smp > 1 && !log.numbers("SHELL_AP_MIGRATIONS").iter().any(|moves| *moves != 0) {
        return Err("No native task migration observed during the soak".to_string());
    }
    let file = std::fs::File::open(&vm.trace).map_err(|error| format!("PS/2 trace: {error}"))?;
    let size = file.metadata().map_err(|error| format!("PS/2 trace size: {error}"))?.len();
    let snapshot = std::io::Read::take(file,size);
    let (generated,received,peak) = trace::ps2_fifo(std::io::BufReader::new(snapshot))?;
    Ok(object(vec![
        ("Minutes", Value::Number(u64::from(minutes))),
        ("IdleMinutes", Value::Number(u64::from(minutes))),
        ("Rounds", Value::Number(rounds as u64)),
        ("Preemption", preemption),
        ("Dispatcher", dispatcher),
        ("IrqOwnership", ownership),
        ("Migration", migration),
        ("Ps2GeneratedBytes", Value::Number(generated)),
        ("Ps2ReceivedBytes", Value::Number(received)),
        ("Ps2FifoPeak", Value::Number(peak as u64)),
    ]))
}

// ====== input_prefix_valid ======

pub fn input_prefix_valid(log: &SerialLog, command: &str, first: usize) -> bool {
    let values = log.numbers("PS2_UNICODE");
    let Some(actual) = values.get(first..) else { return false };
    let expected = command.chars().map(|key| u64::from(u32::from(key))).collect::<Vec<_>>();
    expected.starts_with(actual)
}

// ====== shell_type ======

fn shell_type(vm: &mut Vm, command: &str) -> Result<(), String> {
    let before = vm.log();
    let first = before.numbers("PS2_UNICODE").len();
    let lines = before.text.split_inclusive('\n').filter_map(|line| line.strip_suffix('\n'))
        .filter(|line| line.starts_with("PS2_LINE_READY: ")).count();
    for key in command.chars() {
        let key = if key == ' ' { "spc".to_string() } else { key.to_string() };
        vm.command(&format!("sendkey {key} 20"));
        std::thread::sleep(Duration::from_millis(55));
        let log = vm.log();
        if !input_prefix_valid(&log,command,first) {
            vm.command("stop");
            let values = log.numbers("PS2_UNICODE");
            return Err(format!("Input prefix mismatch for {command:?}: {:?}",
                values.get(first..).unwrap_or(&[])));
        }
    }
    vm.command("sendkey ret 20");
    std::thread::sleep(Duration::from_millis(55));
    let deadline = Instant::now() + vm.limits.wait;
    while Instant::now() < deadline {
        let log = vm.log();
        if log.terminal() { return Err("Kernel failed while typing shell command".to_string()) }
        if let Some(actual) = log.text.split_inclusive('\n').filter_map(|line| line.strip_suffix('\n'))
            .filter_map(|line| line.strip_prefix("PS2_LINE_READY: ")).nth(lines) {
            if actual != command {
                vm.command("stop");
                return Err(format!("Input line mismatch: expected {command:?}, received {actual:?}"));
            }
            return Ok(());
        }
        if !vm.alive() { return Err("Owned VM exited while typing shell command".to_string()) }
        std::thread::sleep(CR4_POLL);
    }
    Err(format!("Missing submitted input line for {command:?}"))
}

// ====== writable_image_ranges ======

pub fn writable_image_ranges(bytes: &[u8]) -> Option<Vec<(u64,u64)>> {
    if bytes.get(..6)? != b"\x7fELF\x02\x01" { return None }
    let u16_at = |at: usize| Some(u16::from_le_bytes(bytes.get(at..at.checked_add(2)?)?.try_into().ok()?));
    let u32_at = |at: usize| Some(u32::from_le_bytes(bytes.get(at..at.checked_add(4)?)?.try_into().ok()?));
    let u64_at = |at: usize| Some(u64::from_le_bytes(bytes.get(at..at.checked_add(8)?)?.try_into().ok()?));
    let offset = usize::try_from(u64_at(32)?).ok()?;
    let stride = usize::from(u16_at(54)?);
    let count = usize::from(u16_at(56)?);
    if stride < 56 { return None }
    let end = offset.checked_add(stride.checked_mul(count)?)?;
    bytes.get(offset..end)?;
    let mut ranges = Vec::new();
    for index in 0..count {
        let header = offset + index * stride;
        if u32_at(header)? != 1 || u32_at(header + 4)? & 2 == 0 { continue }
        let address = u64_at(header + 16)?;
        let size = u64_at(header + 40)?;
        address.checked_add(size)?;
        if size != 0 { ranges.push((address,size)); }
    }
    Some(ranges)
}

// ====== run_ordinary ======

pub fn run_ordinary(
    directory: &Path,
    image: &Path,
    paths: &Paths,
    plan: &Plan,
    limits: Limits,
    negative: Option<u32>,
) -> Result<Value, String> {
    let mut vm = Vm::start(directory, image, paths, plan, limits)?;
    let outcome = match negative {
        Some(SHELL_CASE) => shell_boot(&mut vm, plan, false),
        Some(SHELL_BRIDGE_FAULT_CASE) => shell_boot(&mut vm, plan, true),
        Some(NO_PS2_CASE) => no_ps2_boot(&mut vm, plan),
        Some(case) if case > SOAK_CASE_BASE => soak_boot(&mut vm, plan, case - SOAK_CASE_BASE),
        _ => ordinary_boot(&mut vm, plan),
    };
    if outcome.is_err() {
        vm.command("stop");
        if let Ok(bytes) = std::fs::read(directory.join("esp/EFI/BOOT/kernel.elf")) {
            if let Some(ranges) = writable_image_ranges(&bytes) {
                for (index,(address,size)) in ranges.into_iter().enumerate() {
                    if size > u64::from(plan.memory) * 1024 * 1024 { continue }
                    let path = directory.join(format!("writable-{index}.bin"));
                    let path = path.to_string_lossy().replace('\\',"/").replace('"',"\\\"");
                    vm.command(&format!("memsave 0x{address:x} {size} \"{path}\""));
                }
            }
        }
        for cpu in 0..plan.smp {
            let offset = vm.monitor_size().unwrap_or(0) as usize;
            if let Ok(values) = read_registers(&mut vm, cpu, &[b"RSP=", b"R15="]) {
                vm.command(&format!("x /512gx 0x{:016x}", values[0].saturating_sub(4096)));
                vm.command(&format!("x /4gx 0x{:016x}", values[1]));
            }
            if let Ok(bytes) = vm.monitor_bytes() {
                if let Some(base) = bytes.get(offset..).and_then(|bytes| {
                    bytes.split(|byte| *byte == b'\n').find_map(|line| {
                        let line = std::str::from_utf8(line).ok()?.trim();
                        let fields = line.strip_prefix("GS ")?.split_whitespace().collect::<Vec<_>>();
                        u64::from_str_radix(fields.get(1)?,16).ok()
                    })
                }) {
                    vm.command(&format!("x /56gx 0x{base:016x}"));
                }
            }
        }
        let log = vm.log();
        if let Ok(record) = log.number("KERNEL_PANIC_RECORD", 0) {
            let _ = read_memory(&mut vm,record,64);
        }
        if let Ok(base) = log.number("KERNEL_AP_BASE", 0) {
            for cpu in 1..plan.smp {
                let record = base + u64::from(cpu) * 0x40000 + 0x30100;
                let _ = read_memory(&mut vm,record,64);
            }
        }
        vm.command("info lapic");
        if let Ok(owner) = vm.log().number("KERNEL_WATCHDOG_OWNER", 0) {
            vm.command(&format!("x /28gx 0x{owner:016x}"));
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    let closed = vm.close();
    match (outcome, closed) {
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error),
        (Ok(value), Ok(())) => Ok(value),
    }
}
