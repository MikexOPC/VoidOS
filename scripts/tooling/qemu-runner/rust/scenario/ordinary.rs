use crate::evidence::results::{Value, object};
use crate::evidence::trace;
use crate::vm::machine::{Limits, Paths, Plan, Vm};
use std::path::Path;
use std::time::{Duration, Instant};

pub const SHELL_CASE: u32 = 1;
const SMEP: u64 = 1 << 20;
const SMAP: u64 = 1 << 21;
const CR4_DEADLINE: Duration = Duration::from_secs(5);
const CR4_POLL: Duration = Duration::from_millis(10);
const INIT_LINE: &[u8] = b"USER: VoidOS init, ring3";

// ====== expected_protection ======

fn expected_protection(cpu: &str) -> u64 {
    match cpu {
        "max,-smap" => SMEP,
        "max,-smep" => SMAP,
        "max,-smep,-smap" => 0,
        _ => SMEP | SMAP,
    }
}

// ====== parse_cr4 ======

pub fn parse_cr4(bytes: &[u8], cpu: u32) -> Option<u64> {
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
                if let Some(value) = field.strip_prefix(b"CR4=") {
                    return u64::from_str_radix(std::str::from_utf8(value).ok()?, 16).ok();
                }
            }
        }
    }
    None
}

// ====== read_cr4 ======

fn read_cr4(vm: &mut Vm, cpu: u32) -> Result<u64, String> {
    vm.command(&format!("cpu {cpu}"));
    let offset = vm.monitor_size()? as usize;
    vm.command("info registers");
    let deadline = Instant::now() + CR4_DEADLINE;
    while Instant::now() < deadline {
        let bytes = vm.monitor_bytes()?;
        if let Some(value) = bytes.get(offset..).and_then(|bytes| parse_cr4(bytes, cpu)) {
            return Ok(value);
        }
        std::thread::sleep(CR4_POLL);
    }
    Err(format!("Missing CR4 of CPU {cpu}"))
}

// ====== ordinary_boot ======

fn ordinary_boot(vm: &mut Vm, plan: &Plan) -> Result<Value, String> {
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    if !log.failures().is_empty() || log.terminal() {
        return Err(format!("Unexpected kernel failure {:?}", log.failures()));
    }
    let expected = expected_protection(plan.cpu);
    if log.number("KERNEL_USER_PROTECTION", 0)? != expected {
        return Err("Unexpected reported protection".to_string());
    }
    let full = expected == SMEP | SMAP;
    if log.has("KERNEL_USER_MODE_ALLOWED") != full || log.has("USER_MODE_DISABLED") == full {
        return Err("Unexpected user mode decision".to_string());
    }
    let serial = std::fs::read(&vm.serial).map_err(|error| format!("Serial: {error}"))?;
    let printed = serial.windows(INIT_LINE.len()).any(|window| window == INIT_LINE);
    if full && (!printed || !log.before("KERNEL_PROCESS_OK", "KERNEL_READY")) {
        return Err("The first process did not run".to_string());
    }
    if !full && (printed || log.has("KERNEL_PROCESS_OK") || !log.has("PROCESS_DISABLED")) {
        return Err("A CPU without protections ran a process".to_string());
    }
    for cpu in 0..plan.smp {
        if read_cr4(vm, cpu)? & (SMEP | SMAP) != expected {
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

// ====== shell_boot ======

fn shell_boot(vm: &mut Vm) -> Result<Value, String> {
    let log = vm.wait("KERNEL_PS2_ROUTE_ACQUIRED", 1, Some("INFO"))?;
    if !log.failures().is_empty() || log.terminal() || !log.before("KERNEL_PROCESS_OK", "KERNEL_PS2_ROUTE_ACQUIRED") {
        return Err("The process did not finish before the input session".to_string());
    }
    let serial = std::fs::read(&vm.serial).map_err(|error| format!("Serial: {error}"))?;
    if !serial.windows(INIT_LINE.len()).any(|window| window == INIT_LINE) {
        return Err("The first process did not print".to_string());
    }
    Ok(object(vec![("Process", Value::Bool(true))]))
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
    let outcome = if negative == Some(SHELL_CASE) { shell_boot(&mut vm) } else { ordinary_boot(&mut vm, plan) };
    let closed = vm.close();
    match (outcome, closed) {
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error),
        (Ok(value), Ok(())) => Ok(value),
    }
}
