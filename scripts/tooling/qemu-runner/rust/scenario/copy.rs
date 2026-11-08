use crate::evidence::results::{Value, object};
use crate::evidence::serial::SerialLog;
use crate::evidence::trace;
use crate::vm::machine::{Limits, Paths, Plan, Vm};
use std::path::Path;
use std::time::{Duration, Instant};

const WINDOW_POLL_SLEEP: Duration = Duration::from_millis(5);
const NEGATIVE_POLL_SLEEP: Duration = Duration::from_millis(100);

// ====== wait_window ======

pub fn wait_window(vm: &mut Vm, start: u64, end: u64, cpl: u8) -> Result<(), String> {
    let deadline = Instant::now() + vm.limits.window;
    while Instant::now() < deadline && vm.alive() {
        let offset = vm.monitor_size()? as usize;
        vm.command("info registers");
        let poll = deadline.min(Instant::now() + vm.limits.window_poll);
        while Instant::now() < poll {
            let bytes = vm.monitor_bytes()?;
            if let Some((rip, seen)) = trace::registers(bytes.get(offset..).unwrap_or(&[])) {
                if (start..end).contains(&rip) && seen == cpl {
                    return Ok(());
                }
                break;
            }
            std::thread::sleep(WINDOW_POLL_SLEEP);
        }
    }
    Err(format!("Missing diagnostic window CPL={cpl}, RIP={start:x}..{end:x}"))
}

// ====== copy_boot ======

fn copy_boot(vm: &mut Vm) -> Result<Value, String> {
    vm.inject_on("nmi");
    let log = vm.wait("KERNEL_K5_NMI_ARMED", 1, Some("OK"))?;
    let first = log.number("K5_NMI_FIRST", 0)?;
    let last = log.number("K5_NMI_LAST", 0)?;
    wait_window(vm, first, last, 0)?;
    vm.command("nmi");
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    if !log.failures().is_empty() || log.terminal() || !log.before("KERNEL_K5_COPY_OK", "KERNEL_READY") {
        return Err("Missing clean copy continuation".to_string());
    }
    if !log.has("KERNEL_K5_SMEP_SMAP_SUPPORTED") || log.numbers("K5_COPY_FAULTS") != [4] {
        return Err("Missing SMAP or fault evidence".to_string());
    }
    let bytes = vm.trace_bytes()?;
    let instruction = log.number("K5_COPY_INSTRUCTION", 0)?;
    let faults: Vec<(u64, u64)> = trace::kernel_faults(&bytes)
        .iter()
        .filter(|fault| fault.rip == instruction)
        .map(|fault| (fault.error, fault.cr2))
        .collect();
    let expected = [(0, 0x401000), (3, 0x406000), (0, 0x401000), (3, 0x406000)];
    let user = trace::user_faults(&bytes);
    if faults != expected || user != [("06".to_string(), "0000".to_string())] {
        return Err(format!("Unexpected fault sequence {faults:?}"));
    }
    if trace::triple_fault(&bytes) {
        return Err("Unexpected triple fault".to_string());
    }
    Ok(object(vec![
        ("RecoveredFaults", Value::Number(4)),
        ("Nmis", Value::Number(1)),
        ("Smap", Value::Bool(true)),
        ("Smep", Value::Bool(true)),
    ]))
}

// ====== expected_error ======

fn expected_error(case: u32) -> Result<Option<u64>, String> {
    match case {
        20 => Ok(Some(0x3)),
        21 => Ok(Some(0x11)),
        22 | 23 => Ok(Some(0x0)),
        24 => Ok(Some(0x2)),
        25 => Ok(None),
        _ => Err(format!("Unknown copy negative: {case}")),
    }
}

// ====== judge_negative ======

fn judge_negative(vm: &Vm, case: u32, expected: Option<u64>, log: &SerialLog) -> Result<Value, String> {
    let bytes = vm.trace_bytes()?;
    let events = trace::kernel_faults(&bytes);
    let faults: Vec<u64> = events.iter().map(|fault| fault.error).collect();
    if trace::triple_fault(&bytes) {
        return Err("Negative copy case caused a triple fault".to_string());
    }
    let wrong = match expected {
        None => !faults.is_empty(),
        Some(error) => faults.last() != Some(&error),
    };
    if wrong {
        return Err(format!("Unexpected kernel faults {faults:?}"));
    }
    if case == 23 {
        let last = events.last().ok_or("Missing kernel fault")?;
        let seen = (last.error, last.rip, last.cr2);
        if seen != (0, log.number("K5_COPY_OUTSIDE", 0)?, 0x401000) {
            return Err(format!("RIP negative did not fault at its isolated copy instruction: {seen:?}"));
        }
    }
    let tail: Vec<Value> = faults.last().map(|error| Value::Number(*error)).into_iter().collect();
    Ok(object(vec![
        ("ExpectedRejection", Value::Number(u64::from(case))),
        ("Faults", Value::List(tail)),
    ]))
}

// ====== copy_negative ======

fn copy_negative(vm: &mut Vm, case: u32) -> Result<Value, String> {
    let expected = expected_error(case)?;
    vm.inject_on("info registers");
    let deadline = Instant::now() + vm.limits.negative;
    let accepted = |log: &SerialLog| log.has("KERNEL_K5_COPY_OK") || log.has("KERNEL_READY");
    while Instant::now() < deadline && vm.alive() {
        if accepted(&vm.log()) {
            return Err("Negative copy case was accepted".to_string());
        }
        vm.command("info registers");
        std::thread::sleep(NEGATIVE_POLL_SLEEP);
        if trace::halted(&vm.monitor_bytes()?) {
            let log = vm.log();
            if accepted(&log) {
                return Err("Negative copy case was accepted".to_string());
            }
            return judge_negative(vm, case, expected, &log);
        }
    }
    Err("Negative copy case was not rejected".to_string())
}

// ====== run_copy ======

pub fn run_copy(
    directory: &Path,
    image: &Path,
    paths: &Paths,
    plan: &Plan,
    limits: Limits,
    negative: Option<u32>,
) -> Result<Value, String> {
    let mut vm = Vm::start(directory, image, paths, plan, limits)?;
    let outcome = match negative {
        Some(case) => copy_negative(&mut vm, case),
        None => copy_boot(&mut vm),
    };
    let closed = vm.close();
    match (outcome, closed) {
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error),
        (Ok(value), Ok(())) => Ok(value),
    }
}
