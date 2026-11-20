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
            if registers[0] != address || active.len() != 1 || active[0][4] != 0xe1 || active[0][3] == 0 {
                return Err(format!("Invalid IPI ownership on CPU {cpu}"));
            }
            ipi_subscriptions += 1;
        }
    }
    Ok(object(vec![
        ("HpetSubscription", Value::Number(words[5])),
        ("TimerSubscription", Value::Number(words[25])),
        ("ReleasedDeviceSlots", Value::Number(3)),
        ("RegistryBytes", Value::Number(bytes)),
        ("SharedIdtCpus", Value::Number(shared_cpus)),
        ("ApIpiSubscriptions", Value::Number(ipi_subscriptions)),
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
        shell_type(vm, "bridge status");
        vm.wait(marker, count, None)?;
    }
    let remote = plan.smp > 1 && !fault;
    if remote {
        shell_type(vm, "ap 1000");
    }
    shell_type(vm, "status");
    shell_type(vm, "exit");
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    if !log.failures().is_empty() || log.number("SHELL_COMMAND_COUNT", 0)? != 4 + u64::from(remote) ||
        !log.text.contains("SHELL_STATUS: BSP PS2, kernel tasks") ||
        !log.before("SHELL_EXIT_REQUEST", "KERNEL_PS2_INPUT_CLEANUP_OK") ||
        !log.before("KERNEL_PS2_INPUT_CLEANUP_OK", "KERNEL_READY") {
        return Err("The input session did not clean up".to_string());
    }
    if !log.before("KERNEL_SCHEDULER_CLEANUP_OK", "KERNEL_PS2_INPUT_CLEANUP_OK") ||
        !log.has("KERNEL_PS2_MOUSE_CLEANUP_OK") || !log.has("KERNEL_CONSOLE_SHELL_CLEAN") {
        return Err("Missing collected stacks, mouse or console cleanup".to_string());
    }
    if remote && (log.number("SHELL_AP_CPU", 0)? != 1 || log.number("SHELL_AP_COUNT", 0)? != 1000 ||
        log.number("SHELL_AP_SUM", 0)? != 500500 || log.number("KERNEL_AP_TASK_JOBS", 0)? != 1 ||
        !log.before("KERNEL_AP_TASKS_CLEAN", "KERNEL_PS2_INPUT_CLEANUP_OK")) {
        return Err("AP job or its session cleanup is invalid".to_string());
    }
    let trace_bytes = vm.trace_bytes()?;
    let expected = if fault { vec![("06".to_string(), "0000".to_string())] } else { Vec::new() };
    if trace::user_faults(&trace_bytes) != expected || trace::triple_fault(&trace_bytes) {
        return Err("Unexpected fault during the shell session".to_string());
    }
    let preemption = preemption_evidence(&log)?;
    let dispatcher = wait_dispatcher(vm)?;
    let ownership = irq_ownership(vm,plan.smp)?;
    Ok(object(vec![
        ("Process", Value::Bool(enabled)),
        ("Bridge", Value::Bool(enabled && !fault)),
        ("Preemption", preemption),
        ("Dispatcher", dispatcher),
        ("IrqOwnership", ownership),
        ("ApJob", Value::Bool(remote)),
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
    if !log.before("KERNEL_READY", "KERNEL_DISPATCHER_READY") {
        return Err("The idle loop started before the boot completed".to_string());
    }
    let dispatcher = wait_dispatcher(vm)?;
    let bytes = vm.trace_bytes()?;
    if !trace::user_faults(&bytes).is_empty() || trace::triple_fault(&bytes) {
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
    let log = vm.wait("KERNEL_PS2_INPUT_READY", 1, Some("OK"))?;
    if expected_protection(plan.cpu) & (SMEP | SMAP) != SMEP | SMAP || !log.has("KERNEL_PROCESS_OK") {
        return Err("The soak needs the bridge service: SMEP, SMAP and the first process".to_string());
    }
    let deadline = Instant::now() + Duration::from_secs(u64::from(minutes) * 60);
    let mut rounds = 0usize;
    while Instant::now() < deadline {
        shell_type(vm, "bridge status");
        rounds += 1;
        let log = vm.wait("SHELL_BRIDGE_STATUS_OK", rounds, None)?;
        if !log.failures().is_empty() || log.terminal() {
            return Err(format!("Kernel failure after {rounds} rounds {:?}", log.failures()));
        }
        std::thread::sleep(SOAK_PAUSE);
    }
    shell_type(vm, "exit");
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    let commands = log.number("SHELL_COMMAND_COUNT", 0)?;
    if !log.failures().is_empty() || commands != rounds as u64 + 1 ||
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
    Ok(object(vec![
        ("Minutes", Value::Number(u64::from(minutes))),
        ("IdleMinutes", Value::Number(u64::from(minutes))),
        ("Rounds", Value::Number(rounds as u64)),
        ("Preemption", preemption),
        ("Dispatcher", dispatcher),
        ("IrqOwnership", ownership),
    ]))
}

// ====== shell_type ======

fn shell_type(vm: &mut Vm, command: &str) {
    for key in command.chars() {
        let key = if key == ' ' { "spc".to_string() } else { key.to_string() };
        vm.command(&format!("sendkey {key} 20"));
        std::thread::sleep(Duration::from_millis(55));
    }
    vm.command("sendkey ret 20");
    std::thread::sleep(Duration::from_millis(55));
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
        let _ = read_registers(&mut vm, 0, &[b"RFL=", b"HLT="]);
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
