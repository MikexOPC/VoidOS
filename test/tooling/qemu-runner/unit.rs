use qemu_runner_core::cli::{self, Mode};
use qemu_runner_core::evidence::results::{Value, object, render};
use qemu_runner_core::evidence::serial::{SerialLog, split_lines, strip_ansi};
use qemu_runner_core::evidence::sha256::hash_bytes;
use qemu_runner_core::evidence::trace;
use qemu_runner_core::scenario::ordinary::{dispatcher_evidence, parse_cr4, parse_memory, parse_register, preemption_evidence};
use qemu_runner_core::vm::machine::{Inject, Limits, Paths, Plan, arguments};
use std::path::{Path, PathBuf};
use std::time::Duration;

// ====== strings ======

fn strings(items: &[&str]) -> Vec<String> {
    items.iter().map(|item| item.to_string()).collect()
}

// ====== sha256_matches_standard_vectors ======

#[test]
fn sha256_matches_standard_vectors() {
    assert_eq!(hash_bytes(b""), "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
    assert_eq!(hash_bytes(b"abc"), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
    assert_eq!(
        hash_bytes(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1"
    );
    assert_eq!(
        hash_bytes(&vec![b'a'; 1_000_000]),
        "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0"
    );
}

// ====== serial_strips_ansi_and_splits_like_python ======

#[test]
fn serial_strips_ansi_and_splits_like_python() {
    assert_eq!(strip_ansi("\u{1b}[32m[ OK ]\u{1b}[0m X\u{1b}[?25l"), "[ OK ] X");
    assert_eq!(strip_ansi("\u{1b}[\u{1b}[1mZ"), "\u{1b}[Z");
    assert_eq!(split_lines("a\r\nb\rc\nd\u{b}e\u{1c}f\u{85}g\u{2028}h\n"), ["a", "b", "c", "d", "e", "f", "g", "h"]);
    assert_eq!(split_lines("a\n\nb"), ["a", "", "b"]);
    assert!(split_lines("").is_empty());
}

// ====== serial_markers_match_whole_lines ======

#[test]
fn serial_markers_match_whole_lines() {
    let log = SerialLog::parse(
        b"\x1b[32m[ OK ]\x1b[0m KERNEL_READY\r\n[OK]  B_1\n[  WARN ]\tC\n[ INFO ] lower\n[ OK ]NOSPACE\n\
          prefix [ OK ] D\n[ OK ] E extra\n[ FAIL ] F\n",
    );
    let names: Vec<_> = log.events.iter().map(|event| (event.line, event.level.as_str(), event.name.as_str())).collect();
    assert_eq!(names, [(0, "OK", "KERNEL_READY"), (1, "OK", "B_1"), (2, "WARN", "C"), (7, "FAIL", "F")]);
    assert_eq!(log.count("KERNEL_READY", Some("OK")), 1);
    assert_eq!(log.count("KERNEL_READY", Some("FAIL")), 0);
    assert_eq!(log.failures(), ["F"]);
    assert!(log.before("KERNEL_READY", "F"));
    assert!(!log.before("F", "KERNEL_READY"));
    assert!(!log.before("KERNEL_READY", "ABSENT"));
    assert!(!log.has("KERNEL_READY_TIMEOUT"));
}

// ====== serial_fields_reject_near_misses ======

#[test]
fn serial_fields_reject_near_misses() {
    let log = SerialLog::parse(
        b"A: 0x0000000000000001\nA: 0x00000000000000FF trailing\nB: 0x00000000000000012\nC: 0x000000000000000x\n\
          D: 0x000000000000001\nlow: 0x0000000000000001\n E_1: 0x0000000000000002 \n",
    );
    assert_eq!(log.numbers("A"), [1, 255]);
    assert_eq!(log.numbers("E_1"), [2]);
    for absent in ["B", "C", "D", "low"] {
        assert!(log.numbers(absent).is_empty(), "{absent}");
    }
    assert_eq!(log.number("A", 1), Ok(255));
    assert_eq!(log.number("A", 2), Err("Missing numeric evidence: A".to_string()));
}

// ====== serial_terminal_states ======

#[test]
fn serial_terminal_states() {
    for text in [
        "x KERNEL_EXCEPTION_HALTED y",
        "aDISPATCH_FAILEDb",
        "PS2_INPUT_FAILED",
        "KERNEL_CONSOLE_DRAW_REJECTED_PM",
        "PANIC",
        "a PANIC: b",
        "KERNEL_SMP_PANIC",
        "KERNEL_SMP_PANIC_HALTED",
    ] {
        assert!(SerialLog::parse(text.as_bytes()).terminal(), "{text}");
    }
    for text in ["", "PANICKED", "NOPANIC", "PANIC_REASON", "XKERNEL_SMP_PANIC", "KERNEL_SMP_PANIC_HALTEDX", "KERNEL_READY"] {
        assert!(!SerialLog::parse(text.as_bytes()).terminal(), "{text}");
    }
}

// ====== monitor_registers ======

#[test]
fn monitor_registers() {
    let line = b"(qemu) info registers\r\nRIP=ffff800000001000 RFL=00000002 [---] CPL=3 II=0\r\n";
    assert_eq!(trace::registers(line), Some((0xffff_8000_0000_1000, 3)));
    assert_eq!(trace::registers(b"RIP=10 CPL=0 CPL=2\n"), Some((0x10, 2)));
    assert_eq!(trace::registers(b"RIP=10 CPL=0"), Some((0x10, 0)));
    assert_eq!(trace::registers(b"RIP=10\nCPL=0\n"), None);
    assert_eq!(trace::registers(b"RIP=10 CPL=4\n"), None);
    assert_eq!(trace::registers(b"RIP=10 XCPL=0\n"), None);
    assert_eq!(trace::registers(b"RIP=10 CPL=01\n"), None);
    assert_eq!(trace::registers(b"RIP=zz CPL=0\nRIP=20 CPL=1\n"), Some((0x20, 1)));
    assert_eq!(trace::registers(b""), None);
}

// ====== monitor_halt ======

#[test]
fn monitor_halt() {
    let block = |rax: &str, lines: usize, hlt: &str| {
        let mut text = format!("RAX={rax} RBX=0\r\n");
        text.push_str(&"filler\r\n".repeat(lines));
        text.push_str(&format!("RIP=1 RFL=2 CPL=0 HLT={hlt}\r\n"));
        text.into_bytes()
    };
    assert!(trace::halted(&block("000000004b354641", 3, "1")));
    assert!(trace::halted(&block("000000004B354641", 0, "1")));
    assert!(trace::halted(&block("000000004b354641", 4, "1")));
    assert!(!trace::halted(&block("000000004b354641", 5, "1")));
    assert!(!trace::halted(&block("000000004b354641", 3, "0")));
    assert!(!trace::halted(&block("000000004b354642", 3, "1")));
    assert!(trace::halted(b"rax=000000004b354641\nrip=1 hlt=1"));
    assert!(!trace::halted(b"RAX=000000004b354641\rRIP=1 HLT=1\n"));
    assert!(!trace::halted(b"RAX=000000004b354641\nRIP=1 HLT=10\n"));
    assert!(!trace::halted(b"RAX=000000004b354641\n RIP=1 HLT=1\n"));
}

// ====== trace_events ======

#[test]
fn trace_events() {
    let text = b"\
        0: v=0e e=0003 i=0 cpl=0 IP=0038:ffff800000002000 pc=ffff800000002000 SP=0030:1 CR2=0000000000406000\n\
        1: v=06 e=0000 i=0 cpl=3 IP=0033:1 pc=1\n\
        2: v=0D e=0000 i=0 CPL=3 IP=0033:1\n\
        3: v=0e e=0005 i=0 cpl=3 IP=0033:1 CR2=2\n\
        4: v=41 e=0000 i=0 cpl=3 IP=0033:1\n\
        5: v=0e e=0000 i=1 cpl=0 IP=0038:1 CR2=2\n\
        6: v=0e e=0000 i=0 cpl=0 IP=0038:1 pc=1\n\
        7: xv=06 e=0 i=0 cpl=3\n\
        8: v=06 e=0 i=0 cpl=30\n\
        9: V=0E E=0011 I=0 CPL=0 ip=0038:AB cr2=CD cr2=EF\n";
    assert_eq!(
        trace::user_faults(text),
        [
            ("06".to_string(), "0000".to_string()),
            ("0D".to_string(), "0000".to_string()),
            ("0e".to_string(), "0005".to_string()),
        ]
    );
    let faults: Vec<_> = trace::kernel_faults(text).iter().map(|fault| (fault.error, fault.rip, fault.cr2)).collect();
    assert_eq!(faults, [(3, 0xffff_8000_0000_2000, 0x406000), (0x11, 0xab, 0xef)]);
    assert!(trace::triple_fault(b"x TRIPLE Fault y"));
    assert!(!trace::triple_fault(b"triple faul"));
}

// ====== results_match_python_json ======

#[test]
fn results_match_python_json() {
    let value = object(vec![
        ("Passed", Value::Number(1)),
        ("Empty", Value::List(vec![])),
        ("Text", Value::Text("a\"\\\n\u{e8}\u{1f600}\u{7f}\u{1}".to_string())),
        ("Results", Value::List(vec![object(vec![("Smap", Value::Bool(true)), ("Faults", Value::List(vec![Value::Number(3)]))])])),
    ]);
    let expected = concat!(
        "{\n",
        "  \"Passed\": 1,\n",
        "  \"Empty\": [],\n",
        "  \"Text\": \"a\\\"\\\\\\n\\u00e8\\ud83d\\ude00\\u007f\\u0001\",\n",
        "  \"Results\": [\n",
        "    {\n",
        "      \"Smap\": true,\n",
        "      \"Faults\": [\n",
        "        3\n",
        "      ]\n",
        "    }\n",
        "  ]\n",
        "}"
    );
    assert_eq!(render(&value), expected);
}

// ====== vm_arguments_match_python ======

#[test]
fn vm_arguments_match_python() {
    let paths = Paths { qemu_dir: PathBuf::from("C:/Program Files/qemu"), loader: PathBuf::from("x") };
    let plan = Plan { memory: 128, smp: 1, inject: Inject::None, cpu: "max", machine: "q35" };
    let list = arguments(Path::new("D:\\out\\boot-1"), &paths, &plan);
    assert_eq!(
        list,
        strings(&[
            "-machine", "q35", "-cpu", "max", "-accel", "tcg", "-m", "128", "-smp", "1", "-net", "none",
            "-vga", "std", "-display", "none", "-monitor", "stdio", "-no-reboot",
            "-serial", "file:D:/out/boot-1/serial.log",
            "-d", "int,cpu_reset", "-D", "D:/out/boot-1/qemu.log",
            "-drive", "if=pflash,format=raw,readonly=on,file=C:/Program Files/qemu/share/edk2-x86_64-code.fd",
            "-drive", "if=pflash,format=raw,file=D:/out/boot-1/vars.fd",
            "-drive", "format=raw,file=fat:rw:D:/out/boot-1/esp",
        ])
    );
}

// ====== standard_limits_match_python ======

#[test]
fn standard_limits_match_python() {
    let limits = Limits::standard();
    assert_eq!(limits.wait, Duration::from_secs(40));
    assert_eq!(limits.window, Duration::from_secs(8));
    assert_eq!(limits.window_poll, Duration::from_millis(300));
    assert_eq!(limits.negative, Duration::from_secs(60));
    assert_eq!(limits.close, Duration::from_secs(5));
}

// ====== cli_mirrors_campaign_arguments ======

#[test]
fn cli_mirrors_campaign_arguments() {
    let parsed = |items: &[&str]| cli::parse(strings(items));
    let Ok(Mode::Run(run)) = parsed(&["--image", "k.elf", "--boots", "3", "--copy-negative", "23"]) else {
        panic!("negative campaign rejected");
    };
    assert_eq!((run.boots, run.negative, run.inject), (3, Some(23), Inject::None));
    let Ok(Mode::Run(run)) = parsed(&["--image", "k.elf", "--copy"]) else { panic!("copy campaign rejected") };
    assert_eq!((run.boots, run.negative), (10, None));
    for rejected in [
        &["--copy"][..],
        &["--image", "k.elf"],
        &["--image", "k.elf", "--copy", "--boots", "0"],
        &["--image", "k.elf", "--copy", "--boots", "11"],
        &["--image", "k.elf", "--copy-negative", "19"],
        &["--image", "k.elf", "--copy-negative", "26"],
        &["--image", "k.elf", "--copy", "--inject", "other"],
        &["--image", "k.elf", "--copy", "--bad"],
        &["--image"],
        &["parity", "one"],
    ] {
        assert!(parsed(rejected).is_err(), "{rejected:?}");
    }
    assert_eq!(parsed(&["dump", "d"]), Ok(Mode::Dump(PathBuf::from("d"))));
}

// ====== ordinary_options_and_cr4 ======

#[test]
fn ordinary_options_and_cr4() {
    let args = strings(&["--image", "i", "--ordinary", "--smp", "4", "--cpu", "max,-smap"]);
    let Ok(Mode::Run(options)) = cli::parse(args) else { panic!("rejected") };
    assert!(options.ordinary);
    assert_eq!((options.smp, options.cpu), (4, "max,-smap"));
    assert!(cli::parse(strings(&["--image", "i", "--ordinary", "--smp", "9"])).is_err());
    assert!(cli::parse(strings(&["--image", "i", "--ordinary", "--cpu", "other"])).is_err());
    assert!(cli::parse(strings(&["--image", "i", "--process-negative", "37"])).is_ok());
    assert!(cli::parse(strings(&["--image", "i", "--process-negative", "38"])).is_err());
    assert_eq!(parse_cr4(b"CPU#3\r\nCR3=1 CR4=00300ee8 \r\n", 3), Some(0x300ee8));
    assert_eq!(parse_cr4(b"CPU#0\nCR4=00300ee8\n", 3), None);
    assert_eq!(parse_cr4(b"CPU#3\nCPU#0\nCR4=00300ee8\n", 3), None);
    assert_eq!(parse_cr4(b"CPU#0\nCR4=1\nCPU#3\nCR4=2\n", 3), Some(2));
    let idle = b"CPU#0\nRAX=0 RBX=0\nRIP=ffffffff80001000 RFL=00000202 [-------] CPL=0 II=0 A20=1 SMM=0 HLT=1\n";
    assert_eq!(parse_register(idle, 0, b"RFL="), Some(0x202));
    assert_eq!(parse_register(idle, 0, b"HLT="), Some(1));
    assert_eq!(parse_register(idle, 1, b"HLT="), None);
    assert_eq!(parse_cr4(b"CPU#3\nCR4=0030", 3), None);
    assert_eq!(parse_cr4(b"CPU#3\nCR4=x\n", 3), None);
    assert_eq!(parse_cr4(b"CR4=00300ee8\n", 3), None);
}

// ====== monitor_memory ======

#[test]
fn monitor_memory() {
    assert_eq!(parse_register(b"CPU#0\nIDT=     ffffffff80001000 00000fff\n",0,b"IDT="),Some(0xffffffff80001000));
    let dump = b"(qemu) x /4gx 0xffffffff80000000\r\n\
        ffffffff80000000: 0xffffffff80000000 0x0000000000000040\r\n\
        ffffffff80000010: 0x0000000000000000 0x00000000000000d1\r\n(qemu) ";
    assert_eq!(parse_memory(dump, 0xffffffff80000000, 4), Some(vec![0xffffffff80000000, 0x40, 0, 0xd1]));
    assert!(parse_memory(b"ffffffff80000000: 0x0000000000000000", 0xffffffff80000000, 1).is_none());
    assert!(parse_memory(b"ffffffff80000000: 0x00000000\n", 0xffffffff80000000, 1).is_none());
    assert!(parse_memory(dump, 0xffffffff80000000, 5).is_none());
    assert!(parse_memory(dump, 0xffffffff80000008, 4).is_none());
    assert!(parse_memory(b"ffffffff80000000: Cannot access memory\n", 0xffffffff80000000, 4).is_none());
    assert!(parse_memory(b"ffffffff80000000: 0x1\nffffffff80000010: 0x2\n", 0xffffffff80000000, 2).is_none());
}

// ====== persistent_dispatcher_gate ======

#[test]
fn persistent_dispatcher_gate() {
    let text = "[ OK ] KERNEL_READY\n[ OK ] KERNEL_DISPATCHER_READY\n\
        KERNEL_DISPATCH_IDLE_CYCLES: 0x0000000000000001\nKERNEL_DISPATCH_TIMER_TICKS: 0x000000000000000a\n\
        KERNEL_DISPATCH_FREE_PAGES: 0x0000000000000100\n[ OK ] KERNEL_DISPATCH_IDLE_OK\n\
        KERNEL_DISPATCH_IDLE_CYCLES: 0x00000000000000c8\nKERNEL_DISPATCH_TIMER_TICKS: 0x0000000000000070\n\
        KERNEL_DISPATCH_FREE_PAGES: 0x0000000000000100\n[ OK ] KERNEL_DISPATCH_IDLE_OK\n";
    assert!(dispatcher_evidence(&SerialLog::parse(text.as_bytes())).is_ok());
    let partial = format!("{text}KERNEL_DISPATCH_IDLE_CYCLES: 0x0000000000000190\n");
    assert!(dispatcher_evidence(&SerialLog::parse(partial.as_bytes())).is_ok());
    for invalid in [
        String::new(),
        text.replace("IDLE_CYCLES: 0x00000000000000c8", "IDLE_CYCLES: 0x0000000000000001"),
        text.replace("TIMER_TICKS: 0x0000000000000070", "TIMER_TICKS: 0x000000000000000a"),
        text.replacen("FREE_PAGES: 0x0000000000000100", "FREE_PAGES: 0x0000000000000101", 1),
        text.replace("[ OK ] KERNEL_DISPATCHER_READY\n", ""),
        format!("{text}[ FAIL ] SCHEDULER_RUNTIME_FAILED\n"),
        format!("{text}[ OK ] KERNEL_DISPATCH_IDLE_OK\n"),
        format!("{text}[ OK ] KERNEL_SCHEDULER_CLEANUP_OK\n\
            SCHEDULER_BUDGET_BEFORE: 0x0000000000000101\n\
            SCHEDULER_BUDGET_AFTER: 0x0000000000000100\n"),
    ] {
        assert!(dispatcher_evidence(&SerialLog::parse(invalid.as_bytes())).is_err());
    }
}

// ====== production_preemption_gate ======

#[test]
fn production_preemption_gate() {
    for (outside, suspended, overruns, valid) in [
        (0, 10, 0, false),
        (1, 0, 0, false),
        (3, 2, 0, false),
        (1, 10, 1, false),
        (1, 1, 0, true),
        (20, 30, 0, true),
    ] {
        let text = format!(
            "KERNEL_PREEMPT_OUTSIDE_WINDOW: 0x{outside:016x}\nKERNEL_PREEMPT_SUSPENDED: 0x{suspended:016x}\nKERNEL_PREEMPT_OVERRUNS: 0x{overruns:016x}\n"
        );
        assert_eq!(preemption_evidence(&SerialLog::parse(text.as_bytes())).is_ok(), valid);
    }
    for text in [
        "",
        "KERNEL_PREEMPT_OUTSIDE_WINDOW: 0x1\n",
        "KERNEL_PREEMPT_OUTSIDE_WINDOW: 0x1\nKERNEL_PREEMPT_SUSPENDED: 0x1\n",
    ] {
        assert!(preemption_evidence(&SerialLog::parse(text.as_bytes())).is_err());
    }
}

// ====== host_execution_state ======

#[test]
fn host_execution_state() {
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn SetThreadExecutionState(flags: u32) -> u32;
    }
    const CONTINUOUS: u32 = 0x8000_0000;
    let previous = unsafe { SetThreadExecutionState(CONTINUOUS) };
    assert_ne!(previous, 0);
    {
        let _awake = qemu_runner_core::vm::job::SystemAwake::new().expect("system wake request");
        let state = unsafe { SetThreadExecutionState(CONTINUOUS | 1) };
        assert_eq!(state & 1, 1);
    }
    let restored = unsafe { SetThreadExecutionState(previous | CONTINUOUS) };
    assert_eq!(restored, CONTINUOUS);
}
