use std::collections::HashSet;
use std::io::{self, Write};
use std::process::ExitCode;

const CATALOG: &str = include_str!("commands.tsv");
const LAYOUT: &str = include_str!("../../../../../arch/x86/configs/layout.h");
const K6_NEGATIVES: std::ops::RangeInclusive<u8> = 31..=37;
const UNSUPPORTED: &str = "Unsupported plan-only options.";
const K6_PROGRAMS: &[&str] = &[
    "hello", "null", "readonly", "privileged", "kernel_jump", "bad_pointer", "bss",
    "irq", "spin", "syscall_spin", "flags_spin", "flags_ac", "write_flood", "spurious", "yield_a", "yield_b",
    "ipc_client", "ipc_server", "bridge_client", "bridge_server",
];

#[derive(Clone, Copy, PartialEq, Eq)]
enum Profile {
    Ordinary,
    Periodic,
    ApTimer,
    Process(u8),
}

// ====== parse_profile ======

fn parse_profile(args: impl IntoIterator<Item = String>) -> Result<Profile, &'static str> {
    let mut args = args.into_iter();
    let mut options = HashSet::new();
    let mut service = None;
    let mut negative = None;
    while let Some(arg) = args.next() {
        let key = arg.to_ascii_lowercase();
        if !key.starts_with('-') || !options.insert(key.clone()) {
            return Err(UNSUPPORTED);
        }
        if key == "-testapservice" {
            service = Some(args.next().ok_or(UNSUPPORTED)?.to_ascii_lowercase());
        }
        if key == "-testk6negative" {
            negative = Some(args.next().ok_or(UNSUPPORTED)?);
        }
    }

    if options.contains("-testk6process") {
        let allowed = 1 + usize::from(negative.is_some());
        if options.len() != allowed {
            return Err("K6 process requires an isolated diagnostic profile.");
        }
        return match negative.map(|value| value.parse::<u8>()) {
            None => Ok(Profile::Process(0)),
            Some(Ok(case)) if K6_NEGATIVES.contains(&case) => Ok(Profile::Process(case)),
            Some(_) => Err("Invalid K6 negative case."),
        };
    }
    if negative.is_some() {
        return Err("Invalid K6 negative case.");
    }
    let periodic = options.contains("-testschedulerperiodic");
    let preemption = options.contains("-testschedulerpreemption");
    if preemption {
        return Err("TestSchedulerPreemption is retired; use the ordinary image.");
    }
    if periodic && options.len() > 1 {
        return Err("Preemption requires an isolated diagnostic profile.");
    }
    match (periodic, service.as_deref(), options.len()) {
        (true, None, 1) => Ok(Profile::Periodic),
        (false, None, 0) | (false, Some("none"), 1) => Ok(Profile::Ordinary),
        (false, Some("ap-timer"), 1) => Ok(Profile::ApTimer),
        (false, Some("ap-timer"), 2..) => {
            Err("AP service requires an isolated diagnostic profile.")
        }
        _ => Err(UNSUPPORTED),
    }
}

// ====== c_command ======

fn c_command(profile: Profile, source: &str, object: Option<&str>, optimize: bool) -> Vec<String> {
    let mut command: Vec<_> = [
        "cl.exe", "/nologo", "/TC", "/std:c11", "/W4", "/WX", "/GS-", "/Zl",
    ]
    .into_iter()
    .chain(optimize.then_some("/O2"))
    .chain([
        "/c",
        "/I{repo}",
        "/I{repo}/plan-msvc/bin/Hostx64/x64/../../../include",
        "/I{repo}/plan-sdk/ucrt",
        "/DKERNEL_SCHEDULER_IRQ=1",
    ])
    .map(String::from)
    .collect();
    if profile == Profile::Ordinary {
        command.extend(
            [
                "/DKERNEL_CONSOLE_SHELL=1",
                "/DKERNEL_INPUT_INTERACTIVE=1",
                "/DKERNEL_PS2_INPUT=1",
                "/DKERNEL_PS2_MOUSE=1",
            ]
            .map(String::from),
        );
    }
    if profile == Profile::Periodic {
        command.extend([
            "/DKERNEL_TEST_SCHEDULER_PERIODIC=1".into(),
        ]);
    }
    command.push("/DKERNEL_AP_RUNTIME=1".into());
    if profile == Profile::Ordinary {
        command.push("/DKERNEL_USER_PROCESS=1".into());
        command.push("/DKERNEL_AP_TASKS=1".into());
    }
    if profile == Profile::ApTimer {
        command.push("/DKERNEL_TEST_AP_SERVICE=7".into());
    }
    command.push("/DKERNEL_PANIC_RUNTIME=1".into());
    if let Profile::Process(case) = profile {
        command.push("/DKERNEL_TEST_K6_PROCESS=1".into());
        if case != 0 {
            command.push(format!("/DKERNEL_K6_NEGATIVE={case}"));
        }
    }
    if !optimize {
        command.push("/Od".into());
    }
    let output = object.map_or_else(
        || format!("{}.obj", source.trim_end_matches(".c").replace('/', "-")),
        String::from,
    );
    command.extend([format!("{{repo}}/{source}"), format!("/Fo{{out}}/{output}")]);
    command
}

// ====== compile_commands ======

fn compile_commands(profile: Profile) -> Result<Vec<Vec<String>>, &'static str> {
    let mut commands = Vec::new();
    for line in CATALOG
        .lines()
        .filter(|line| !line.starts_with('#') && !line.is_empty())
    {
        let mut fields: Vec<_> = line.split('\t').collect();
        let wanted = match fields[0] {
            "ordinary" => Some(profile == Profile::Ordinary),
            "diagnostic" => Some(profile != Profile::Ordinary),
            "user" => Some(matches!(profile, Profile::Ordinary | Profile::Process(_))),
            "process" | "each-program" => Some(matches!(profile, Profile::Process(_))),
            _ => None,
        };
        if wanted == Some(false) {
            continue;
        }
        if fields[0] == "each-program" {
            for program in K6_PROGRAMS {
                commands.push(fields[1..].iter().map(|field| field.replace("{program}", program)).collect());
            }
            continue;
        }
        if wanted.is_some() {
            fields.remove(0);
        }
        let mut command = match fields.as_slice() {
            ["c", source] => c_command(profile, source, None, true),
            ["c", source, object] => c_command(profile, source, Some(object), true),
            ["c-od", source] => c_command(profile, source, None, false),
            ["ml64.exe" | "nasm.exe" | "rustc.exe" | "rustc", _, ..] => {
                fields.iter().map(|field| (*field).into()).collect()
            }
            _ => return Err("Invalid build catalog row."),
        };
        if profile == Profile::Ordinary
            && fields.contains(&"{out}/ipi-runtime-idle.obj")
        {
            let output = command
                .iter()
                .position(|arg| arg == "-o")
                .ok_or("Missing NASM output.")?;
            command.insert(output, "-DKERNEL_AP_ADMISSION=1".into());
        }
        if matches!(profile, Profile::Process(_)) && fields.contains(&"/Fo{out}/k6-transition.obj") {
            command.insert(command.len() - 2, "/DK6_DIAGNOSTICS=1".into());
        }
        if profile == Profile::ApTimer {
            if fields.contains(&"{out}/ipi-mailbox.obj") {
                commands.push(c_command(
                    profile,
                    "test/arch/x86/kernel/cpu/percpu/service/integration/probe.c",
                    None,
                    true,
                ));
            }
            if fields.contains(&"{out}/irq-entry-return.obj")
                || fields.contains(&"{out}/ipi-runtime-idle.obj")
            {
                let output = fields
                    .iter()
                    .position(|arg| *arg == "-o")
                    .ok_or("Missing NASM output.")?;
                command.insert(output, "-DKERNEL_TEST_AP_SERVICE=1".into());
            }
        }
        commands.push(command);
    }
    Ok(commands)
}

// ====== link_command ======

fn link_command(commands: &[Vec<String>]) -> Result<Vec<String>, &'static str> {
    let base = LAYOUT
        .lines()
        .find_map(|line| {
            let mut fields = line.split_whitespace();
            match (fields.next(), fields.next(), fields.next()) {
                (Some("#define"), Some("X86_KERNEL_IMAGE_BASE"), Some(value)) => {
                    u64::from_str_radix(value.strip_prefix("0x")?.strip_suffix("ULL")?, 16).ok()
                }
                _ => None,
            }
        })
        .ok_or("Invalid kernel image base.")?;

    let first = [
        "{out}/io.obj",
        "{out}/validation.obj",
        "{out}/window-layout.obj",
        "{out}/object-bounds.obj",
    ];
    let mut link: Vec<_> = [
        "link.exe",
        "/nologo",
        "/machine:x64",
        "/subsystem:native",
        "/entry:kernel_entry",
        "/nodefaultlib",
        "/incremental:no",
        "/dynamicbase:no",
        "/fixed",
        "/Brepro",
    ]
    .into_iter()
    .map(String::from)
    .collect();
    link.push(format!("/base:0x{base:X}"));
    link.extend(
        ["/out:{out}/kernel.exe", "/map:{out}/kernel.map"]
            .into_iter()
            .chain(first)
            .map(String::from),
    );

    let mut outputs = HashSet::new();
    for command in commands {
        let output = command
            .iter()
            .find_map(|arg| arg.strip_prefix("/Fo"))
            .or_else(|| {
                command
                    .windows(2)
                    .find(|args| args[0] == "-o")
                    .map(|args| args[1].as_str())
            })
            .ok_or("Compiler command has no output.")?;
        if !outputs.insert(output) {
            return Err("Duplicate compiler output.");
        }
        if !first.contains(&output) && output != "{out}/ap-template.bin" && !output.ends_with(".elf") {
            link.push(output.into());
        }
    }
    if !first.into_iter().all(|output| outputs.contains(output)) {
        return Err("Missing early linker input.");
    }
    Ok(link)
}

// ====== run ======

fn run() -> Result<(), String> {
    let profile = parse_profile(std::env::args().skip(1))?;
    let mut commands = compile_commands(profile)?;
    commands.push(link_command(&commands)?);
    commands.push(
        [
            "convert-image.ps1",
            "-Image",
            "{out}/kernel.exe",
            "-Output",
            "{out}/kernel.elf",
        ]
        .into_iter()
        .map(String::from)
        .collect(),
    );
    let mut output = io::BufWriter::new(io::stdout().lock());
    for command in commands {
        writeln!(output, "{}", command.join("\t")).map_err(|error| error.to_string())?;
    }
    output.flush().map_err(|error| error.to_string())
}

// ====== main ======

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
