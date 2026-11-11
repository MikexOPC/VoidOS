#![allow(dead_code)]

#[path = "../../../scripts/tooling/kernel-build/rust/depfile.rs"]
mod depfile;
#[path = "../../../scripts/tooling/kernel-build/rust/image.rs"]
mod image;
#[path = "../../../scripts/tooling/kernel-build/rust/layout.rs"]
mod layout;
#[path = "../../../scripts/tooling/kernel-build/rust/parity.rs"]
mod parity;
#[path = "../../../scripts/tooling/kernel-build/rust/plan.rs"]
mod plan;

const BASE: u64 = 0xFFFF_FFFF_8000_0000;
const PE: usize = 0x80;
const OPTIONAL: usize = PE + 24;
const SECTIONS: usize = OPTIONAL + 240;
const TEXT_RIGHTS: u32 = 0x6000_0020;
const DATA_RIGHTS: u32 = 0xC000_0040;
const READ_RIGHTS: u32 = 0x4000_0040;

// ====== put ======

fn put(data: &mut [u8], offset: usize, value: u64, width: usize) {
    data[offset..offset + width].copy_from_slice(&value.to_le_bytes()[..width]);
}

// ====== section ======

fn section(data: &mut [u8], index: usize, name: &str, spec: (u32, u32, u32, u32, u32)) {
    let header = SECTIONS + index * 40;
    data[header..header + name.len()].copy_from_slice(name.as_bytes());
    let (size, rva, raw_size, raw, rights) = spec;
    put(data, header + 8, u64::from(size), 4);
    put(data, header + 12, u64::from(rva), 4);
    put(data, header + 16, u64::from(raw_size), 4);
    put(data, header + 20, u64::from(raw), 4);
    put(data, header + 36, u64::from(rights), 4);
}

// ====== valid_pe ======

fn valid_pe() -> Vec<u8> {
    let mut data = vec![0u8; 0x4000];
    put(&mut data, 0, 0x5a4d, 2);
    put(&mut data, 60, PE as u64, 4);
    put(&mut data, PE, 0x4550, 4);
    put(&mut data, PE + 4, 0x8664, 2);
    put(&mut data, PE + 6, 3, 2);
    put(&mut data, PE + 20, 240, 2);
    put(&mut data, OPTIONAL, 0x20b, 2);
    put(&mut data, OPTIONAL + 16, 0x1010, 4);
    put(&mut data, OPTIONAL + 24, BASE, 8);
    put(&mut data, OPTIONAL + 32, 4096, 4);
    put(&mut data, OPTIONAL + 108, 16, 4);
    section(&mut data, 0, ".text", (0x200, 0x1000, 0x200, 0x1000, TEXT_RIGHTS));
    section(&mut data, 1, ".rdata", (0x100, 0x2000, 0x200, 0x1200, READ_RIGHTS));
    section(&mut data, 2, ".data", (0x3000, 0x3000, 0x200, 0x1400, DATA_RIGHTS));
    data[0x1000] = 0xAA;
    data[0x1200] = 0xBB;
    data[0x1400] = 0xCC;
    data
}

// ====== rejected ======

fn rejected(mutate: impl FnOnce(&mut Vec<u8>)) -> String {
    let mut data = valid_pe();
    mutate(&mut data);
    image::convert(&data, BASE).expect_err("the image must be rejected")
}

// ====== image_keeps_addresses_and_bss ======

#[test]
fn image_keeps_addresses_and_bss() {
    let elf = image::convert(&valid_pe(), BASE).unwrap();
    assert_eq!(elf.len(), 0x1000 + 3 * 0x1000);
    assert_eq!(&elf[..7], &[127, 69, 76, 70, 2, 1, 1]);
    assert_eq!(u64::from_le_bytes(elf[24..32].try_into().unwrap()), BASE + 0x1010);
    assert_eq!(u16::from_le_bytes(elf[56..58].try_into().unwrap()), 3);
    assert_eq!(elf[0x1000], 0xAA);
    assert_eq!(elf[0x2000], 0xBB);
    assert_eq!(elf[0x3000], 0xCC);

    let data_header = 64 + 2 * 56;
    assert_eq!(u32::from_le_bytes(elf[data_header + 4..data_header + 8].try_into().unwrap()), 6);
    let file_size = u64::from_le_bytes(elf[data_header + 32..data_header + 40].try_into().unwrap());
    let memory_size = u64::from_le_bytes(elf[data_header + 40..data_header + 48].try_into().unwrap());
    assert_eq!((file_size, memory_size), (0x200, 0x3000));
}

// ====== image_rejects_outside_the_profile ======

#[test]
fn image_rejects_outside_the_profile() {
    let cases: [(fn(&mut Vec<u8>), &str); 16] = [
        (|data| data.truncate(100), "Dimensione PE fuori profilo."),
        (|data| data.resize(16_777_217, 0), "Dimensione PE fuori profilo."),
        (|data| put(data, 0, 0, 2), "PE x64 richiesto."),
        (|data| put(data, PE + 4, 0x14c, 2), "PE x64 richiesto."),
        (|data| put(data, 60, 65537, 4), "PE x64 richiesto."),
        (|data| put(data, PE + 6, 17, 2), "Profilo kernel PE inatteso."),
        (|data| put(data, OPTIONAL + 24, 0x1000, 8), "Profilo kernel PE inatteso."),
        (|data| put(data, OPTIONAL + 32, 8192, 4), "Profilo kernel PE inatteso."),
        (|data| put(data, OPTIONAL + 112, 1, 8), "Directory runtime non consentita."),
        (|data| put(data, OPTIONAL + 112 + 5 * 8, 1, 8), "Directory runtime non consentita."),
        (|data| data[SECTIONS + 40..SECTIONS + 46].copy_from_slice(b".text\0"), "Sezione duplicata."),
        (|data| data[SECTIONS..SECTIONS + 5].copy_from_slice(b".init"), "Sezione non supportata: .init"),
        (|data| put(data, SECTIONS + 36, u64::from(0xE000_0020u32), 4), "Sezione fuori limiti o con permessi inattesi."),
        (|data| put(data, SECTIONS + 12, 0x1001, 4), "Sezione fuori limiti o con permessi inattesi."),
        (|data| put(data, OPTIONAL + 16, 0x2000, 4), "Ingresso fuori dal codice."),
        (|data| put(data, OPTIONAL + 112 + 3 * 8, 0x9000, 4), "Directory PE non contenuta nei dati presenti in sola lettura."),
    ];
    for (mutate, expected) in cases {
        assert_eq!(rejected(mutate), expected);
    }
}

// ====== image_rejects_truncated_sections ======

#[test]
fn image_rejects_truncated_sections() {
    let message = rejected(|data| put(data, SECTIONS + 16, 0x4000, 4));
    assert_eq!(message, "Sezione fuori limiti o con permessi inattesi.");
}

// ====== image_base_comes_from_the_layout ======

#[test]
fn image_base_comes_from_the_layout() {
    assert_eq!(image::image_base().unwrap(), BASE);
}

// ====== source_dependencies_are_language_independent ======

#[test]
fn source_dependencies_are_language_independent() {
    let text = r#"{"Version":"1.2","Data":{"Source":"c:\\a.c","ProvidedModule":"","Includes":["c:\\p q\\a.h","d:\\b.h"],"ImportedModules":["x"]}}"#;
    assert_eq!(
        depfile::parse_source_dependencies(text).unwrap(),
        vec![r"c:\p q\a.h".to_string(), r"d:\b.h".to_string()],
    );
    let empty = r#"{"Data":{"Includes":[]}}"#;
    assert!(depfile::parse_source_dependencies(empty).unwrap().is_empty());
    assert!(depfile::parse_source_dependencies("{}").is_err());
    assert!(depfile::parse_source_dependencies(r#"{"Includes":["c:\\a"#).is_err());
}

// ====== prerequisites_of_rustc_and_nasm ======

#[test]
fn prerequisites_of_rustc_and_nasm() {
    let rustc = "C:/out/a.obj: C:/r/a.rs C:/r/b.rs\n\nC:/out/a.d: C:/r/a.rs C:/r/b.rs\n\nC:/r/a.rs:\nC:/r/b.rs:\n\n# env-dep:X=1\n";
    let found = depfile::parse_prerequisites(rustc);
    assert_eq!(found, ["C:/r/a.rs", "C:/r/b.rs", "C:/r/a.rs", "C:/r/b.rs"]);

    let nasm = "C:/out/i.obj : \\\n  C:/p\\ q/i.asm \\\n  C:/out/ap-template.bin\n";
    assert!(depfile::parse_prerequisites(nasm).contains(&"C:/out/ap-template.bin".to_string()));
}

// ====== depfile_render_escapes_and_removes_duplicates ======

#[test]
fn depfile_render_escapes_and_removes_duplicates() {
    let paths = [r"C:\p q\a.h".to_string(), "C:/p q/a.h".to_string(), "C:/out/x.obj".to_string()];
    assert_eq!(depfile::render("x.obj", &paths), "x.obj: \\\n C:/p\\ q/a.h\n");
}

// ====== masm_includes_are_refused ======

#[test]
fn masm_includes_are_refused() {
    assert!(depfile::declares_include("  INCLUDE a.inc\n"));
    assert!(depfile::declares_include("includelib x.lib\n"));
    assert!(!depfile::declares_include("; include in a comment\nmov rax, rbx\n"));
    assert!(!depfile::declares_include("includes_more:\n"));
}

// ====== profile_gate ======

#[test]
fn profile_gate() {
    assert!(plan::profile_arguments("ordinary").unwrap().is_empty());
    assert_eq!(plan::profile_arguments("periodic").unwrap(), ["-TestSchedulerPeriodic"]);
    assert_eq!(plan::profile_arguments("ap-timer").unwrap(), ["-TestApService", "ap-timer"]);
    assert_eq!(plan::profile_arguments("k6-process").unwrap(), ["-TestK6Process"]);
    for case in 31..=37 {
        assert_eq!(
            plan::profile_arguments(&format!("k6-process-{case}")).unwrap(),
            ["-TestK6Process".to_string(), "-TestK6Negative".into(), case.to_string()],
        );
    }
    for profile in ["", "ORDINARY", "k5-copy", "ping", "k6-process-30", "k6-process-38", "k6-process-x", "k6-process-"] {
        assert!(plan::profile_arguments(profile).is_err());
    }
}

// ====== placeholders_resolve_like_the_planner_build ======

#[test]
fn placeholders_resolve_like_the_planner_build() {
    let resolved = |field: &str| plan::resolve(field, "T/bin", "S", "R", "O");
    assert_eq!(resolved("/I{repo}/plan-msvc/bin/Hostx64/x64/../../../include"), "/IT/bin/../../../include");
    assert_eq!(resolved("/I{repo}/plan-sdk/ucrt"), "/IS/ucrt");
    assert_eq!(resolved("{repo}/a.c"), "R/a.c");
    assert_eq!(resolved("/Fo{out}/a.obj"), "/FoO/a.obj");
}

// ====== outputs_and_audit_lines ======

#[test]
fn outputs_and_audit_lines() {
    let row = |fields: &[&str]| fields.iter().map(|field| field.to_string()).collect::<Vec<_>>();
    assert_eq!(plan::output_of(&row(&["C:/t/cl.exe", "/FoO/a.obj", "a.c"])).unwrap(), "a.obj");
    assert_eq!(plan::output_of(&row(&["nasm.exe", "-o", "O/a.bin", "a.asm"])).unwrap(), "a.bin");
    assert!(plan::output_of(&row(&["link.exe", "/nologo"])).is_none());
    assert_eq!(plan::audit_line(&row(&["C:/t/cl.exe", "/c", "a.c"])), "cl.exe\t/c\ta.c");
}

// ====== parity_detects_each_kind_of_drift ======

#[test]
fn parity_detects_each_kind_of_drift() {
    let lines = |items: &[&str]| items.iter().map(|item| item.to_string()).collect::<Vec<_>>();
    let plan = lines(&["cl a", "cl b", "link a b"]);
    assert!(parity::compare(&plan, &plan).is_ok());

    let flag = parity::compare(&plan, &lines(&["cl a /O1", "cl b", "link a b"])).unwrap_err();
    assert!(flag.contains("command 1"));
    let order = parity::compare(&plan, &lines(&["cl a", "cl b", "link b a"])).unwrap_err();
    assert!(order.contains("command 3"));
    let missing = parity::compare(&plan, &lines(&["cl a", "cl b"])).unwrap_err();
    assert!(missing.contains("Different command counts"));
    let extra = parity::compare(&plan, &lines(&["cl a", "cl b", "link a b", "cl c"])).unwrap_err();
    assert!(extra.contains("Different command counts"));
}

// ====== layout_copies_match_the_repository ======

#[test]
fn layout_copies_match_the_repository() {
    let repo = std::env::var("VOIDOS_REPO").unwrap();
    layout::check_copies(&repo).unwrap();
}

// ====== layout_copy_gate_rejects_each_kind_of_drift ======

#[test]
fn layout_copy_gate_rejects_each_kind_of_drift() {
    let good = "const WINDOWS: u64 = 0xFFFF_8000_0000_0000;
const RECURSIVE: u64 = 510;
";
    assert!(layout::check_source("a.rs", "WINDOWS", 0xFFFF_8000_0000_0000, good).is_ok());
    assert!(layout::check_source("a.rs", "RECURSIVE", 510, good).is_ok());

    let message = layout::check_source("a.rs", "WINDOWS", 0xFFFF_8000_0000_1000, good).unwrap_err();
    assert_eq!(message, "a.rs: WINDOWS differs from layout.h");
    assert!(layout::check_source("a.rs", "RECURSIVE", 511, good).is_err());
    assert!(layout::check_source("a.rs", "VIDEO", 1, good).is_err());
    assert!(layout::check_source("a.rs", "WINDOWS", 1, "const WINDOWS: u32 = 1;").is_err());
}

// ====== layout_values_are_computed_like_the_build ======

#[test]
fn layout_values_are_computed_like_the_build() {
    let text = "#define X86_KERNEL_IMAGE_BASE 0xFFFFFFFF80000000ULL
#define X86_LAYOUT_WINDOWS_SLOT 400U
#define X86_LAYOUT_FB_SLOT 100U
#define X86_LAYOUT_RECURSIVE_SLOT 510U
";
    let copies = layout::expected_copies(text).unwrap();
    let value = |name: &str| copies.iter().find(|copy| copy.1 == name).unwrap().2;
    assert_eq!(value("RECURSIVE"), 510);
    assert_eq!(value("KERNEL_IMAGE_BASE"), BASE);
    assert_eq!(value("WINDOWS"), 0xFFFF_0000_0000_0000 | (400u64 << 39));
    assert_eq!(value("LAPIC_VIRTUAL"), (0xFFFF_0000_0000_0000 | (400u64 << 39)) + 0x12_0000);
    assert_eq!(value("VIDEO"), (100u64 << 39) + 4096);
    assert!(layout::expected_copies("#define X86_LAYOUT_FB_SLOT 1U
").is_err());
}
