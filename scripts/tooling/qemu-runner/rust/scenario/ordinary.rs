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
const IDLE_SETTLE: Duration = Duration::from_secs(3);
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
            for field in line.split(|byte| byte.is_ascii_whitespace()) {
                if let Some(value) = field.strip_prefix(name) {
                    return u64::from_str_radix(std::str::from_utf8(value).ok()?, 16).ok();
                }
            }
        }
    }
    None
}

// ====== read_cr4 ======

fn read_cr4(vm: &mut Vm, cpu: u32) -> Result<u64, String> {
    read_register(vm, cpu, b"CR4=")
}

// ====== read_register ======

fn read_register(vm: &mut Vm, cpu: u32, name: &[u8]) -> Result<u64, String> {
    vm.command(&format!("cpu {cpu}"));
    let offset = vm.monitor_size()? as usize;
    vm.command("info registers");
    let deadline = Instant::now() + CR4_DEADLINE;
    while Instant::now() < deadline {
        let bytes = vm.monitor_bytes()?;
        if let Some(value) = bytes.get(offset..).and_then(|bytes| parse_register(bytes, cpu, name)) {
            return Ok(value);
        }
        std::thread::sleep(CR4_POLL);
    }
    Err(format!("Missing {} of CPU {cpu}", String::from_utf8_lossy(name)))
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
    shell_type(vm, "status");
    shell_type(vm, "exit");
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    if !log.failures().is_empty() || log.number("SHELL_COMMAND_COUNT", 0)? != 4 ||
        !log.text.contains("SHELL_STATUS: BSP PS2, kernel tasks") ||
        !log.before("SHELL_EXIT_REQUEST", "KERNEL_PS2_INPUT_CLEANUP_OK") ||
        !log.before("KERNEL_PS2_INPUT_CLEANUP_OK", "KERNEL_READY") {
        return Err("The input session did not clean up".to_string());
    }
    let trace_bytes = vm.trace_bytes()?;
    let expected = if fault { vec![("06".to_string(), "0000".to_string())] } else { Vec::new() };
    if trace::user_faults(&trace_bytes) != expected || trace::triple_fault(&trace_bytes) {
        return Err("Unexpected fault during the shell session".to_string());
    }
    let preemption = preemption_evidence(&log)?;
    Ok(object(vec![
        ("Process", Value::Bool(enabled)),
        ("Bridge", Value::Bool(enabled && !fault)),
        ("Preemption", preemption),
    ]))
}

// ====== no_ps2_boot ======

// Machine without a PS/2 controller: no shell, the process still ran and the BSP stays idle
// with interrupts enabled instead of stopping.
fn no_ps2_boot(vm: &mut Vm, plan: &Plan) -> Result<Value, String> {
    let log = vm.wait("KERNEL_IDLE", 1, Some("OK"))?;
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
    if !log.before("KERNEL_READY", "KERNEL_IDLE") {
        return Err("The idle loop started before the boot completed".to_string());
    }
    std::thread::sleep(IDLE_SETTLE);
    let flags = read_register(vm, 0, b"RFL=")?;
    let halted = read_register(vm, 0, b"HLT=")?;
    if flags & IF == 0 || halted != 1 {
        return Err(format!("BSP not idle with interrupts enabled: RFL={flags:x} HLT={halted}"));
    }
    let after = vm.wait("KERNEL_IDLE", 1, Some("OK"))?;
    if !after.failures().is_empty() || after.terminal() {
        return Err(format!("Kernel failure while idle {:?}", after.failures()));
    }
    let bytes = vm.trace_bytes()?;
    if !trace::user_faults(&bytes).is_empty() || trace::triple_fault(&bytes) {
        return Err("Unexpected fault without a PS/2 controller".to_string());
    }
    Ok(object(vec![("Ps2", Value::Bool(false)), ("Process", Value::Bool(enabled))]))
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
    Ok(object(vec![
        ("Minutes", Value::Number(u64::from(minutes))),
        ("Rounds", Value::Number(rounds as u64)),
        ("Preemption", preemption),
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
    let closed = vm.close();
    match (outcome, closed) {
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error),
        (Ok(value), Ok(())) => Ok(value),
    }
}
