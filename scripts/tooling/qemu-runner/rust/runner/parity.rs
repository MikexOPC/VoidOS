use crate::evidence::serial::SerialLog;
use crate::evidence::sha256::hash_file;
use crate::evidence::trace;
use std::fmt::Write as _;
use std::fs;
use std::path::Path;

const ADDITIVE: [&str; 1] = ["vm.json"];
const VOLATILE: &str = "volatile ";
const TIMING_EVENTS: [&str; 1] = ["TIMER_CALIBRATION_RETRY"];
const TIMING_FIELDS: [&str; 7] = [
    "K5_NMI_RIP",
    "KERNEL_TIMER_CALIBRATION_ATTEMPT",
    "KERNEL_TIMER_IRQ_PM",
    "KERNEL_TIMER_RELOAD",
    "KERNEL_TIMER_WAIT_PM",
    "SCHEDULER_IRQ_FLAGS",
    "SCHEDULER_IRQ_RIP",
];

// ====== evidence_files ======

fn evidence_files(directory: &Path, prefix: &str, output: &mut Vec<String>) {
    let Ok(entries) = fs::read_dir(directory) else { return };
    for entry in entries.flatten() {
        let name = format!("{prefix}{}", entry.file_name().to_string_lossy());
        if entry.path().is_dir() {
            evidence_files(&entry.path(), &format!("{name}/"), output);
        } else if !ADDITIVE.contains(&name.as_str()) {
            output.push(name);
        }
    }
    output.sort();
}

// ====== dump_boot ======

pub fn dump_boot(directory: &Path) -> String {
    let mut text = String::new();
    let mut files = Vec::new();
    evidence_files(directory, "", &mut files);
    let _ = writeln!(text, "files {}", files.join(" "));
    for name in ["esp/EFI/BOOT/kernel.elf", "esp/EFI/BOOT/BOOTX64.EFI", "esp/EFI/BOOT/boot-check.txt"] {
        let digest = hash_file(&directory.join(name)).unwrap_or_else(|_| "absent".to_string());
        let _ = writeln!(text, "esp {name} {digest}");
    }
    let stderr = fs::read(directory.join("stderr.log")).unwrap_or_default();
    let _ = writeln!(text, "stderr {}", String::from_utf8_lossy(&stderr).trim());
    let log = SerialLog::from_file(&directory.join("serial.log"));
    let _ = writeln!(text, "terminal {}", log.terminal());
    for event in &log.events {
        let prefix = if TIMING_EVENTS.contains(&event.name.as_str()) { VOLATILE } else { "" };
        let _ = writeln!(text, "{prefix}event {} {}", event.level, event.name);
        let _ = writeln!(text, "{VOLATILE}event-line {} {}", event.line, event.name);
    }
    for (name, values) in &log.fields {
        if TIMING_FIELDS.contains(&name.as_str()) {
            let _ = writeln!(text, "field {name} count {}", values.len());
            let _ = writeln!(text, "{VOLATILE}field {name} {values:x?}");
        } else {
            let _ = writeln!(text, "field {name} {values:x?}");
        }
    }
    let monitor = fs::read(directory.join("monitor.log")).unwrap_or_default();
    let _ = writeln!(text, "halted {}", trace::halted(&monitor));
    let _ = writeln!(text, "{VOLATILE}registers {:x?}", trace::registers(&monitor));
    let bytes = fs::read(directory.join("qemu.log")).unwrap_or_default();
    let _ = writeln!(text, "triple {}", trace::triple_fault(&bytes));
    let _ = writeln!(text, "user-faults {:?}", trace::user_faults(&bytes));
    let kernel: Vec<(u64, u64, u64)> = trace::kernel_faults(&bytes).iter().map(|fault| (fault.error, fault.rip, fault.cr2)).collect();
    let _ = writeln!(text, "kernel-faults {kernel:x?}");
    text
}

// ====== normalized_results ======

fn normalized_results(campaign: &Path) -> Result<String, String> {
    let path = campaign.join("results.json");
    let text = fs::read_to_string(&path).map_err(|error| format!("{}: {error}", path.display()))?;
    let absolute = std::path::absolute(campaign).map_err(|error| error.to_string())?;
    let plain = absolute.display().to_string();
    let escaped = plain.replace('\\', "\\\\");
    Ok(text.replace("\r\n", "\n").replace(&escaped, "<OUT>").replace(&plain, "<OUT>"))
}

// ====== compare ======

pub fn compare(first: &Path, second: &Path) -> Result<Vec<String>, String> {
    let mut differences = Vec::new();
    if normalized_results(first)? != normalized_results(second)? {
        differences.push("results.json differs".to_string());
    }
    let mut boots = 0;
    while first.join(format!("boot-{}", boots + 1)).is_dir() || second.join(format!("boot-{}", boots + 1)).is_dir() {
        boots += 1;
        let name = format!("boot-{boots}");
        let (a, b) = (dump_boot(&first.join(&name)), dump_boot(&second.join(&name)));
        let stable = |text: &'_ str| -> Vec<String> {
            text.lines().filter(|line| !line.starts_with(VOLATILE)).map(String::from).collect()
        };
        let (lines_a, lines_b) = (stable(&a), stable(&b));
        for index in 0..lines_a.len().max(lines_b.len()) {
            let (left, right) = (lines_a.get(index), lines_b.get(index));
            if left != right {
                let show = |line: Option<&String>| line.map_or("<absent>".to_string(), String::clone);
                differences.push(format!("{name}: {} <> {}", show(left), show(right)));
            }
        }
    }
    if boots == 0 {
        differences.push("no boot directories".to_string());
    }
    Ok(differences)
}
