use crate::evidence::results::{Value, object};
use crate::evidence::serial::SerialLog;
use crate::evidence::trace;
use crate::scenario::copy::wait_window;
use crate::vm::machine::{Limits, Paths, Plan, Vm};
use std::path::Path;
use std::time::{Duration, Instant};

const POLL_SLEEP: Duration = Duration::from_millis(100);
const USER_FAULTS: [(&str, &str); 4] = [("0e", "0004"), ("0e", "0007"), ("0d", "0000"), ("0e", "0015")];
const WINDOWS: u64 = 6;

// ====== process_boot ======

fn process_boot(vm: &mut Vm) -> Result<Value, String> {
    vm.inject_on("nmi");
    for index in 0..WINDOWS as usize {
        let log = vm.wait("KERNEL_K6_NMI_ARMED", index + 1, Some("OK"))?;
        let first = log.number("K6_NMI_FIRST", index)?;
        let last = log.number("K6_NMI_LAST", index)?;
        wait_window(vm, first, last, 0)?;
        vm.command("nmi");
        vm.wait("KERNEL_K6_NMI_RESUMED", index + 1, Some("OK"))?;
    }
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    if !log.failures().is_empty() || log.terminal() || !log.before("KERNEL_K6_PROCESS_OK", "KERNEL_READY") {
        return Err("Missing clean process continuation".to_string());
    }
    for marker in ["KERNEL_K6_ELF_REJECTED", "KERNEL_K6_HELLO_OK", "KERNEL_K6_FAULTS_OK", "KERNEL_K6_POINTERS_OK", "KERNEL_K6_ISOLATION_OK", "KERNEL_K6_IRQ_OK", "KERNEL_K6_BUDGET_OK", "KERNEL_K6_AC_REJECTED", "KERNEL_K6_AFTER_BUDGET_OK", "KERNEL_K6_DRAIN_OK", "KERNEL_K6_SPURIOUS_OK", "KERNEL_K6_SPURIOUS_USER_OK", "KERNEL_K6_WRITE_QUOTA_OK"] {
        if !log.has(marker) {
            return Err(format!("Missing {marker}"));
        }
    }
    if log.numbers("K6_FAULT_VECTOR") != [14, 14, 13, 14] || log.numbers("K6_FAULT_ERROR") != [4, 7, 0, 0x15] {
        return Err("Unexpected reported user faults".to_string());
    }
    let bytes = vm.trace_bytes()?;
    let user = trace::user_faults(&bytes);
    let expected: Vec<(String, String)> = USER_FAULTS.iter().map(|(v, e)| (v.to_string(), e.to_string())).collect();
    if user != expected {
        return Err(format!("Unexpected CPL3 faults {user:?}"));
    }
    let instruction = log.number("K6_COPY_INSTRUCTION", 0)?;
    let recovered: Vec<(u64, u64)> = trace::kernel_faults(&bytes)
        .iter()
        .filter(|fault| fault.rip == instruction)
        .map(|fault| (fault.error, fault.cr2))
        .collect();
    if recovered != [(0, 0x5fd000)] {
        return Err(format!("Unexpected recovered copy faults {recovered:?}"));
    }
    if trace::triple_fault(&bytes) {
        return Err("Unexpected triple fault".to_string());
    }
    Ok(object(vec![
        ("UserFaults", Value::Number(4)),
        ("RecoveredFaults", Value::Number(1)),
        ("Nmis", Value::Number(WINDOWS)),
        ("Hello", Value::Bool(true)),
    ]))
}

// ====== accepted ======

fn accepted(log: &SerialLog) -> bool {
    log.has("KERNEL_K6_PROCESS_OK") || log.has("KERNEL_READY")
}

// ====== process_negative ======

fn process_negative(vm: &mut Vm, case: u32) -> Result<Value, String> {
    let halted_case = matches!(case, 34 | 35);
    let reason = match case {
        31 | 32 => "PROCESS_KERNEL_FAULT",
        33 => "PROCESS_ROOT_CHANGED",
        34 | 35 => "",
        36 => "PROCESS_STRAY_IRQ",
        37 => "PROCESS_STRAY_NMI",
        _ => return Err(format!("Unknown process negative: {case}")),
    };
    vm.inject_on(if case == 37 { "nmi" } else { "info registers" });
    let deadline = Instant::now() + vm.limits.negative;
    while Instant::now() < deadline && vm.alive() {
        let log = vm.log();
        if accepted(&log) {
            return Err("Negative process case was accepted".to_string());
        }
        if case == 37 && log.has("KERNEL_K6_STRAY_NMI_READY") {
            vm.command("nmi");
        }
        vm.command("info registers");
        std::thread::sleep(POLL_SLEEP);
        let done = if halted_case { trace::halted(&vm.monitor_bytes()?) } else { log.terminal() };
        if done {
            let log = vm.log();
            if accepted(&log) || (!reason.is_empty() && !log.failures().contains(&reason)) {
                return Err(format!("Unexpected rejection {:?}", log.failures()));
            }
            if trace::triple_fault(&vm.trace_bytes()?) {
                return Err("Negative process case caused a triple fault".to_string());
            }
            return Ok(object(vec![("ExpectedRejection", Value::Number(u64::from(case)))]));
        }
    }
    Err("Negative process case was not rejected".to_string())
}

// ====== process_disabled ======

fn process_disabled(vm: &mut Vm) -> Result<Value, String> {
    let log = vm.wait("KERNEL_READY", 1, Some("OK"))?;
    if !log.has("PROCESS_DISABLED") || log.has("KERNEL_K6_PROCESS_OK") || log.has("KERNEL_K6_HELLO_OK") {
        return Err("A CPU without protections ran a process".to_string());
    }
    if !trace::user_faults(&vm.trace_bytes()?).is_empty() {
        return Err("Ring 3 ran without protections".to_string());
    }
    Ok(object(vec![("Disabled", Value::Bool(true))]))
}

// ====== run_process ======

pub fn run_process(
    directory: &Path,
    image: &Path,
    paths: &Paths,
    plan: &Plan,
    limits: Limits,
    negative: Option<u32>,
) -> Result<Value, String> {
    let mut vm = Vm::start(directory, image, paths, plan, limits)?;
    let outcome = match negative {
        Some(0) => process_disabled(&mut vm),
        Some(case) => process_negative(&mut vm, case),
        None => process_boot(&mut vm),
    };
    let closed = vm.close();
    match (outcome, closed) {
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error),
        (Ok(value), Ok(())) => Ok(value),
    }
}
