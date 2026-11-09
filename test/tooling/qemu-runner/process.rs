use qemu_runner_core::evidence::sha256::hash_bytes;
use qemu_runner_core::parity;
use qemu_runner_core::scenario::copy::run_copy;
use qemu_runner_core::vm::job::process_alive;
use qemu_runner_core::vm::machine::{Inject, Limits, Paths, Plan};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::{Duration, Instant};

static NEXT: AtomicU32 = AtomicU32::new(0);

struct Bench {
    root: PathBuf,
    image: PathBuf,
    paths: Paths,
}

// ====== bench ======

fn bench(name: &str) -> Bench {
    let index = NEXT.fetch_add(1, Ordering::Relaxed);
    let scratch = PathBuf::from(std::env::var("VOIDOS_SCRATCH").expect("scratch"));
    let root = std::path::absolute(scratch.join(format!("{name}-{}-{index}", std::process::id()))).expect("absolute");
    let qemu = root.join("qemu");
    fs::create_dir_all(qemu.join("share")).expect("fake qemu dir");
    fs::copy(std::env::var("VOIDOS_FAKE_QEMU").expect("fake"), qemu.join("qemu-system-x86_64.exe")).expect("fake exe");
    fs::write(qemu.join("share/edk2-i386-vars.fd"), b"vars").expect("vars");
    let image = root.join("kernel.elf");
    let loader = root.join("BOOTX64.EFI");
    fs::write(&image, b"image bytes").expect("image");
    fs::write(&loader, b"loader bytes").expect("loader");
    Bench { root, image, paths: Paths { qemu_dir: qemu, loader } }
}

// ====== short_limits ======

fn short_limits() -> Limits {
    Limits {
        wait: Duration::from_millis(1500),
        window: Duration::from_millis(1500),
        window_poll: Duration::from_millis(300),
        negative: Duration::from_millis(1500),
        close: Duration::from_secs(5),
    }
}

// ====== run_case ======

fn run_case(name: &str, case: &str, negative: Option<u32>) -> (Bench, PathBuf, Result<String, String>) {
    run_injected(name, case, negative, Inject::None)
}

// ====== run_injected ======

fn run_injected(name: &str, case: &str, negative: Option<u32>, inject: Inject) -> (Bench, PathBuf, Result<String, String>) {
    let bench = bench(name);
    let directory = bench.root.join("boot-1");
    unsafe { std::env::set_var("VOIDOS_FAKE_CASE", case) };
    let plan = Plan { memory: 128, smp: 1, inject, cpu: "max" };
    let outcome = run_copy(&directory, &bench.image, &bench.paths, &plan, short_limits(), negative);
    let text = outcome.map(|value| qemu_runner_core::evidence::results::render(&value));
    (bench, directory, text)
}

// ====== record_of ======

fn record_of(directory: &Path) -> String {
    fs::read_to_string(directory.join("vm.json")).expect("vm record")
}

// ====== pid_of ======

fn pid_of(directory: &Path) -> u32 {
    let record = record_of(directory);
    let tail = record.split("\"Pid\": ").nth(1).expect("pid field");
    tail.split(|c: char| !c.is_ascii_digit()).next().expect("pid").parse().expect("pid number")
}

// ====== assert_cleaned ======

fn assert_cleaned(directory: &Path) {
    let record = record_of(directory);
    assert!(record.contains("\"Terminated\": true"), "{record}");
    assert!(!process_alive(pid_of(directory)), "VM survived");
    for file in ["monitor.log", "stderr.log", "vm.json", "vars.fd", "esp/EFI/BOOT/kernel.elf", "esp/EFI/BOOT/BOOTX64.EFI", "esp/EFI/BOOT/boot-check.txt"] {
        assert!(directory.join(file).is_file(), "{file}");
    }
}

// ====== wait_until ======

fn wait_until(limit: Duration, mut ready: impl FnMut() -> bool) -> bool {
    let deadline = Instant::now() + limit;
    while Instant::now() < deadline {
        if ready() {
            return true;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    false
}

// ====== scenarios_on_a_fake_vm ======

#[test]
fn scenarios_on_a_fake_vm() {
    let (_bench, directory, outcome) = run_case("ok", "copy-ok", None);
    let text = outcome.expect("copy passes");
    assert!(text.contains("\"RecoveredFaults\": 4"), "{text}");
    assert_cleaned(&directory);
    let record = record_of(&directory);
    assert!(record.contains("\"KilledByRunner\": true") && record.contains("\"ExitedAlone\": false"), "{record}");

    let (_bench, directory, outcome) = run_case("late", "copy-late-window", None);
    outcome.expect("second window poll passes");
    assert_cleaned(&directory);

    let (_bench, directory, outcome) = run_case("wrong", "copy-wrong-fault", None);
    let error = outcome.expect_err("wrong CR2 rejected");
    assert!(error.starts_with("Unexpected fault sequence [(0, 4198400), (3, 4218880), (0, 4198400), (3, 4222976)]"), "{error}");
    assert_cleaned(&directory);

    let (_bench, directory, outcome) = run_case("triple", "triple", None);
    assert_eq!(outcome.expect_err("triple fault rejected"), "Unexpected triple fault");
    assert_cleaned(&directory);

    let (_bench, directory, outcome) = run_case("panic", "panic", None);
    assert_eq!(outcome.expect_err("panic rejected"), format!("Kernel failure: {}", directory.join("serial.log").display()));
    assert_cleaned(&directory);

    let (_bench, directory, outcome) = run_case("neg", "negative-ok", Some(20));
    let text = outcome.expect("negative stops with its fault");
    assert!(text.contains("\"ExpectedRejection\": 20") && text.contains("\"Faults\": [\n    3\n  ]"), "{text}");
    assert_cleaned(&directory);

    let (_bench, _directory, outcome) = run_case("negrip", "negative-rip", Some(23));
    outcome.expect("isolated RIP accepted");

    let (_bench, _directory, outcome) = run_case("negwrong", "negative-wrong-rip", Some(23));
    assert!(outcome.expect_err("wrong RIP rejected").starts_with("RIP negative did not fault"));

    let (_bench, _directory, outcome) = run_case("negclean", "negative-clean", Some(25));
    assert!(outcome.expect("no fault expected").contains("\"Faults\": []"));

    let (_bench, _directory, outcome) = run_case("negbad", "negative-clean", Some(20));
    assert_eq!(outcome.expect_err("missing fault rejected"), "Unexpected kernel faults []");

    let (_bench, _directory, outcome) = run_case("negextra", "negative-ok", Some(25));
    assert_eq!(outcome.expect_err("unexpected fault rejected"), "Unexpected kernel faults [3]");

    let (_bench, directory, outcome) = run_case("negacc", "negative-accepted", Some(20));
    assert_eq!(outcome.expect_err("acceptance rejected"), "Negative copy case was accepted");
    assert_cleaned(&directory);
}

// ====== deadlines_on_a_blocked_vm ======

#[test]
fn deadlines_on_a_blocked_vm() {
    for case in ["silent", "deaf"] {
        let started = Instant::now();
        let (_bench, directory, outcome) = run_case("block", case, None);
        let expected = format!("Missing KERNEL_K5_NMI_ARMED: {}", directory.join("serial.log").display());
        assert_eq!(outcome.expect_err("deadline"), expected);
        assert!(started.elapsed() < Duration::from_secs(10), "{case}");
        assert_cleaned(&directory);

        let started = Instant::now();
        let (_bench, directory, outcome) = run_case("blockneg", case, Some(20));
        assert_eq!(outcome.expect_err("deadline"), "Negative copy case was not rejected");
        assert!(started.elapsed() < Duration::from_secs(10), "{case}");
        assert_cleaned(&directory);
    }
    let (_bench, directory, outcome) = run_injected("nowindow", "copy-ok", None, Inject::Drop);
    let error = outcome.expect_err("no NMI, no continuation");
    assert_eq!(error, format!("Missing KERNEL_READY: {}", directory.join("serial.log").display()));
    assert_cleaned(&directory);
}

// ====== dead_vm ======

#[test]
fn dead_vm() {
    let (_bench, directory, outcome) = run_case("early", "exit-early", None);
    let error = outcome.expect_err("dead VM");
    assert!(
        error == "Missing diagnostic window CPL=0, RIP=ffff800000001000..ffff800000001040"
            || error == "Owned VM exited before KERNEL_K5_NMI_ARMED",
        "{error}"
    );
    let record = record_of(&directory);
    assert!(record.contains("\"ExitedAlone\": true") && record.contains("\"ExitCode\": 3"), "{record}");
    assert!(record.contains("\"KilledByRunner\": false"), "{record}");

    let (_bench, directory, outcome) = run_case("immediate", "exit-immediately", None);
    assert_eq!(outcome.expect_err("dead VM"), "Owned VM exited before KERNEL_K5_NMI_ARMED");
    assert!(record_of(&directory).contains("\"ExitCode\": 7"));

    let (_bench, _directory, outcome) = run_case("immediateneg", "exit-immediately", Some(20));
    assert_eq!(outcome.expect_err("dead VM"), "Negative copy case was not rejected");

    let bench = bench("nostart");
    fs::remove_file(bench.paths.qemu_dir.join("qemu-system-x86_64.exe")).expect("remove");
    let plan = Plan { memory: 128, smp: 1, inject: Inject::None, cpu: "max" };
    let outcome = run_copy(&bench.root.join("boot-1"), &bench.image, &bench.paths, &plan, short_limits(), None);
    assert!(outcome.expect_err("no VM").starts_with("QEMU start:"));

    let bench2 = self::bench("reused");
    let directory = bench2.root.join("boot-1");
    fs::create_dir(&directory).expect("existing");
    let outcome = run_copy(&directory, &bench2.image, &bench2.paths, &plan, short_limits(), None);
    assert!(outcome.expect_err("reused directory").starts_with("Boot directory:"));
}

// ====== write_campaign ======

fn write_campaign(directory: &Path, serial: &str) {
    fs::create_dir_all(directory.join("boot-1")).expect("campaign");
    fs::write(directory.join("results.json"), "{}").expect("results");
    fs::write(directory.join("boot-1/serial.log"), serial).expect("serial");
}

// ====== parity_ignores_only_guest_timing ======

#[test]
fn parity_ignores_only_guest_timing() {
    let bench = bench("parity");
    let base = "[ OK ] A\nK5_NMI_RIP: 0x0000000000000001\n[ OK ] B\nK5_COPY_COUNT: 0x000000000000000a\n";
    let first = bench.root.join("first");
    write_campaign(&first, base);
    let compare_with = |name: &str, serial: &str| {
        let other = bench.root.join(name);
        write_campaign(&other, serial);
        parity::compare(&first, &other).expect("parity")
    };
    assert!(compare_with("rip", &base.replace("0000000000000001", "0000000000000002")).is_empty());
    assert!(compare_with("retry", &format!("[ WARN ] TIMER_CALIBRATION_RETRY\n{base}")).is_empty());
    assert_eq!(compare_with("count", &base.replace("000000000000000a", "000000000000000b")).len(), 1);
    assert_eq!(compare_with("level", &base.replace("[ OK ] B", "[ WARN ] B")).len(), 1);
    assert!(!compare_with("order", &base.replace("[ OK ] A", "[ OK ] C")).is_empty());
    assert!(!compare_with("absent", "[ OK ] A\n[ OK ] B\nK5_COPY_COUNT: 0x000000000000000a\n").is_empty());
}

// ====== spawn_runner ======

fn spawn_runner(bench: &Bench, out: &Path, case: &str, extra: &[&str]) -> Child {
    Command::new(std::env::var("VOIDOS_RUNNER").expect("runner"))
        .args(["--image".as_ref(), bench.image.as_os_str()])
        .args(["--qemu-dir".as_ref(), bench.paths.qemu_dir.as_os_str()])
        .args(["--loader".as_ref(), bench.paths.loader.as_os_str()])
        .args(["--root".as_ref(), bench.root.as_os_str()])
        .args(["--out".as_ref(), out.as_os_str()])
        .args(extra)
        .env("VOIDOS_FAKE_CASE", case)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("runner start")
}

// ====== runner_writes_results_and_exit_codes ======

#[test]
fn runner_writes_results_and_exit_codes() {
    let bench = bench("cli");
    let out = bench.root.join("out");
    let output = spawn_runner(&bench, &out, "copy-ok", &["--copy", "--boots", "2"]).wait_with_output().expect("runner");
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    assert_eq!(output.status.code(), Some(0), "{stdout}");
    assert!(stdout.contains("# ====== Boot 2 ====== #\n[ Pass ]\nRESULT PASS k5-privilege 2/2\n") || stdout.contains("# ====== Boot 2 ====== #\r\n"), "{stdout}");
    let results = fs::read_to_string(out.join("results.json")).expect("results");
    assert!(results.contains(&format!("\"Image\": \"{}\"", hash_bytes(b"image bytes"))), "{results}");
    assert!(results.contains(&format!("\"Loader\": \"{}\"", hash_bytes(b"loader bytes"))), "{results}");
    assert!(results.contains("\"Passed\": 2") && results.contains("\"Total\": 2"), "{results}");
    assert_eq!(fs::read(out.join("boot-1/esp/EFI/BOOT/kernel.elf")).expect("esp"), b"image bytes");
    assert_eq!(fs::read(out.join("boot-1/esp/EFI/BOOT/boot-check.txt")).expect("esp"), b"OS boot check\r\n");
    assert_cleaned(&out.join("boot-1"));
    assert_cleaned(&out.join("boot-2"));

    let out_fail = bench.root.join("out-fail");
    let output = spawn_runner(&bench, &out_fail, "exit-immediately", &["--copy", "--boots", "3"]).wait_with_output().expect("runner");
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    assert_eq!(output.status.code(), Some(1), "{stdout}");
    assert!(stdout.contains("Causa: Owned VM exited before KERNEL_K5_NMI_ARMED"), "{stdout}");
    assert!(stdout.contains("Boot non eseguiti: 2") && stdout.contains("RESULT FAIL k5-privilege 0/3"), "{stdout}");
    let results = fs::read_to_string(out_fail.join("results.json")).expect("results");
    assert!(results.contains("\"Passed\": 0") && results.contains("\"Failure\""), "{results}");
    assert!(!out_fail.join("boot-2").exists());

    let differences = parity::compare(&out, &out).expect("parity");
    assert!(differences.is_empty(), "{differences:?}");
    let differences = parity::compare(&out, &out_fail).expect("parity");
    assert!(differences.iter().any(|item| item == "results.json differs"), "{differences:?}");
    assert!(differences.iter().any(|item| item.contains("KERNEL_K5_NMI_ARMED")), "{differences:?}");

    let status = Command::new(std::env::var("VOIDOS_RUNNER").expect("runner")).arg("--copy").output().expect("runner");
    assert_eq!(status.status.code(), Some(2));
}

// ====== killed_runner_takes_only_its_vm ======

#[test]
fn killed_runner_takes_only_its_vm() {
    let bench = bench("orphan");
    let mut foreign = Command::new(bench.paths.qemu_dir.join("qemu-system-x86_64.exe"))
        .args(["-serial", &format!("file:{}", bench.root.join("foreign.serial").display())])
        .args(["-D", &bench.root.join("foreign.trace").display().to_string()])
        .env("VOIDOS_FAKE_CASE", "silent")
        .stdin(Stdio::piped())
        .spawn()
        .expect("foreign VM");

    let out = bench.root.join("out");
    let mut runner = spawn_runner(&bench, &out, "silent", &["--copy", "--boots", "1"]);
    let directory = out.join("boot-1");
    assert!(wait_until(Duration::from_secs(10), || directory.join("vm.json").is_file()), "no VM record");
    let pid = pid_of(&directory);
    assert!(process_alive(pid));
    runner.kill().expect("kill runner");
    runner.wait().expect("runner exit");
    assert!(wait_until(Duration::from_secs(10), || !process_alive(pid)), "orphan VM survived its runner");
    assert!(matches!(foreign.try_wait(), Ok(None)), "foreign VM touched");

    let out = bench.root.join("out-finished");
    let output = spawn_runner(&bench, &out, "exit-immediately", &["--copy", "--boots", "1"]).wait_with_output().expect("runner");
    assert_eq!(output.status.code(), Some(1));
    assert!(matches!(foreign.try_wait(), Ok(None)), "foreign VM touched");
    foreign.kill().expect("foreign kill");
    foreign.wait().expect("foreign exit");
}
