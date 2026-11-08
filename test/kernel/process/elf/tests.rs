#[path = "../../../../kernel/process/rust/elf/parser.rs"]
mod parser;

use parser::{parse, Error, Image, IMAGE_LIMIT, MAX_PAGES, MAX_SEGMENTS, PAGE, USER_BASE};

const R: u32 = 4;
const RW: u32 = 6;
const RX: u32 = 5;

#[derive(Clone, Copy)]
struct Program {
    kind: u32,
    flags: u32,
    offset: u64,
    virt: u64,
    file: u64,
    memory: u64,
    align: u64,
}

// ====== load ======

fn load(flags: u32, offset: u64, virt: u64, file: u64, memory: u64) -> Program {
    Program { kind: 1, flags, offset, virt, file, memory, align: PAGE }
}

// ====== build ======

fn build(entry: u64, programs: &[Program]) -> Vec<u8> {
    let table_end = 64 + programs.len() as u64 * 56;
    let end = programs.iter().filter(|p| p.offset < 1 << 32 && p.file < 1 << 32)
        .map(|p| p.offset + p.file).max().unwrap_or(0).max(table_end);
    let mut bytes = vec![0u8; end as usize];
    bytes[..8].copy_from_slice(&[0x7f, b'E', b'L', b'F', 2, 1, 1, 0]);
    bytes[16..18].copy_from_slice(&2u16.to_le_bytes());
    bytes[18..20].copy_from_slice(&62u16.to_le_bytes());
    bytes[20..24].copy_from_slice(&1u32.to_le_bytes());
    bytes[24..32].copy_from_slice(&entry.to_le_bytes());
    bytes[32..40].copy_from_slice(&64u64.to_le_bytes());
    bytes[52..54].copy_from_slice(&64u16.to_le_bytes());
    bytes[54..56].copy_from_slice(&56u16.to_le_bytes());
    bytes[56..58].copy_from_slice(&(programs.len() as u16).to_le_bytes());
    for (index, program) in programs.iter().enumerate() {
        let base = 64 + index * 56;
        bytes[base..base + 4].copy_from_slice(&program.kind.to_le_bytes());
        bytes[base + 4..base + 8].copy_from_slice(&program.flags.to_le_bytes());
        for (position, value) in [
            (8, program.offset), (16, program.virt), (24, program.virt),
            (32, program.file), (40, program.memory), (48, program.align),
        ] {
            bytes[base + position..base + position + 8].copy_from_slice(&value.to_le_bytes());
        }
    }
    bytes
}

// ====== minimal ======

fn minimal() -> Vec<u8> {
    build(USER_BASE, &[load(RX, PAGE, USER_BASE, 16, 16)])
}

// ====== invariants ======

fn invariants(image: &Image) {
    assert!((1..=MAX_SEGMENTS).contains(&image.count));
    assert!(image.page_count <= MAX_PAGES);
    let mut floor = USER_BASE;
    let mut entry_found = false;
    let mut pages = 0;
    for segment in &image.segments[..image.count] {
        assert_eq!(segment.virt % PAGE, 0);
        assert!(segment.virt >= floor);
        assert!(segment.file_bytes <= segment.memory_bytes);
        assert!(segment.virt + segment.memory_bytes <= IMAGE_LIMIT);
        assert!(!(segment.flags & 2 != 0 && segment.flags & 1 != 0));
        floor = segment.virt + segment.memory_bytes.div_ceil(PAGE) * PAGE;
        entry_found |= segment.flags & 1 != 0 && image.entry >= segment.virt && image.entry < segment.virt + segment.file_bytes;
    }
    assert!(entry_found);
    for page in image.pages() {
        assert_eq!(page.virt % PAGE, 0);
        assert!(page.file_bytes <= PAGE);
        assert!(!(page.writable && page.executable));
        pages += 1;
    }
    assert_eq!(pages, image.page_count);
}

// ====== fixture ======

fn fixture() -> Vec<u8> {
    let path = std::env::var("VOIDOS_ELF_FIXTURE").expect("fixture path from meson");
    std::fs::read(path).expect("fixture bytes")
}

// ====== real_toolchain_elf ======

#[test]
fn real_toolchain_elf() {
    let bytes = fixture();
    let image = parse(&bytes).expect("real ELF accepted");
    invariants(&image);
    assert_eq!(image.count, 3);
    assert_eq!(image.segments[0].flags, RX);
    assert_eq!(image.segments[1].flags, R);
    assert_eq!(image.segments[2].flags, RW);
    assert_eq!(image.segments[0].virt, USER_BASE);
    assert_eq!(image.entry, USER_BASE);
    let data = image.segments[2];
    assert!(data.memory_bytes > data.file_bytes && data.memory_bytes >= 8192);
    let message = image.segments[1];
    let start = message.file_offset as usize;
    assert!(bytes[start..start + message.file_bytes as usize].windows(13).any(|w| w == b"hello, ring3\n"));
    let bss_pages = image.pages().filter(|page| page.file_bytes == 0).count();
    assert!(bss_pages >= 1);
}

// ====== accepted_boundaries ======

#[test]
fn accepted_boundaries() {
    invariants(&parse(&minimal()).unwrap());
    let top = IMAGE_LIMIT - PAGE;
    invariants(&parse(&build(top, &[load(RX, PAGE, top, 1, PAGE)])).unwrap());
    let full: Vec<_> = (0..MAX_SEGMENTS as u64).map(|i| load(RX, PAGE, USER_BASE + i * PAGE, 16, PAGE)).collect();
    invariants(&parse(&build(USER_BASE, &full)).unwrap());
    let pages = load(RX, PAGE, USER_BASE, 16, MAX_PAGES * PAGE);
    invariants(&parse(&build(USER_BASE, &[pages])).unwrap());
    let mut ignored = load(RX, PAGE, USER_BASE, 16, 16);
    ignored.kind = 4;
    invariants(&parse(&build(USER_BASE, &[ignored, load(RX, PAGE, USER_BASE, 16, 16)])).unwrap());
    let plan: Vec<_> = parse(&build(USER_BASE, &[load(RX, PAGE, USER_BASE, PAGE + 1, 3 * PAGE)])).unwrap().pages().collect();
    assert_eq!(plan.iter().map(|page| page.file_bytes).collect::<Vec<_>>(), vec![PAGE, 1, 0]);
}

// ====== corrupted_identity ======

#[test]
fn corrupted_identity() {
    assert_eq!(parse(&[]), Err(Error::TooSmall));
    assert_eq!(parse(&minimal()[..63]), Err(Error::TooSmall));
    for (offset, value, error) in [
        (0, 0, Error::Identity), (3, b'X', Error::Identity), (4, 1, Error::Identity),
        (5, 2, Error::Identity), (6, 0, Error::Identity), (7, 3, Error::Identity),
        (16, 3, Error::Type), (18, 3, Error::Machine), (20, 2, Error::Header), (52, 63, Error::Header),
    ] {
        let mut bytes = minimal();
        bytes[offset] = value;
        assert_eq!(parse(&bytes), Err(error), "offset {offset}");
    }
}

// ====== corrupted_program_table ======

#[test]
fn corrupted_program_table() {
    for (offset, value) in [(54, 55u8), (56, 0), (56, 33)] {
        let mut bytes = minimal();
        bytes[offset] = value;
        assert_eq!(parse(&bytes), Err(Error::ProgramTable), "offset {offset}");
    }
    for table in [0u64, 63, u64::MAX, u64::MAX - 40, 1 << 40] {
        let mut bytes = minimal();
        bytes[32..40].copy_from_slice(&table.to_le_bytes());
        assert_eq!(parse(&bytes), Err(Error::ProgramTable), "table {table:x}");
    }
}

// ====== rejected_segments ======

#[test]
fn rejected_segments() {
    let base = USER_BASE;
    let mut bad_align = load(RX, PAGE, base, 16, 16);
    bad_align.align = 0x1800;
    let mut small_align = load(RX, PAGE, base, 16, 16);
    small_align.align = 16;
    let mut interp = load(RX, PAGE, base, 16, 16);
    interp.kind = 3;
    let mut dynamic = load(RX, PAGE, base, 16, 16);
    dynamic.kind = 2;
    let mut tls = load(RX, PAGE, base, 16, 16);
    tls.kind = 7;
    for (program, error) in [
        (interp, Error::SegmentType),
        (dynamic, Error::SegmentType),
        (tls, Error::SegmentType),
        (load(7, PAGE, base, 16, 16), Error::WriteExecute),
        (load(1, PAGE, base, 16, 16), Error::Flags),
        (load(RX | 8, PAGE, base, 16, 16), Error::Flags),
        (load(RX, PAGE + 1, base, 16, 16), Error::Alignment),
        (load(RX, PAGE, base + 1, 16, 16), Error::Alignment),
        (bad_align, Error::Alignment),
        (small_align, Error::Alignment),
        (load(RX, 1 << 40, base, 16, 16), Error::FileRange),
        (load(RX, PAGE, base, 17, 16), Error::MemorySize),
        (load(RX, PAGE, base, 0, 0), Error::MemorySize),
        (load(RX, PAGE, base - PAGE, 16, 16), Error::Range),
        (load(RX, PAGE, 0, 16, 16), Error::Range),
        (load(RX, PAGE, IMAGE_LIMIT, 16, 16), Error::Range),
        (load(RX, PAGE, IMAGE_LIMIT - PAGE, 16, PAGE + 1), Error::Range),
        (load(RX, PAGE, 0xffffffff80000000, 16, 16), Error::Range),
        (load(RX, PAGE, 0xfffffffffffff000, 16, 0x2000), Error::Range),
        (load(RX, PAGE, base, 16, (MAX_PAGES + 1) * PAGE), Error::Pages),
    ] {
        assert_eq!(parse(&build(base, &[program])), Err(error), "{error:?}");
    }
    let mut overflow = minimal();
    overflow[64 + 8..64 + 16].copy_from_slice(&(u64::MAX - PAGE + 1).to_le_bytes());
    overflow[64 + 32..64 + 40].copy_from_slice(&PAGE.to_le_bytes());
    overflow[64 + 40..64 + 48].copy_from_slice(&PAGE.to_le_bytes());
    assert_eq!(parse(&overflow), Err(Error::FileRange));
    let mut huge = minimal();
    huge[64 + 40..64 + 48].copy_from_slice(&u64::MAX.to_le_bytes());
    assert_eq!(parse(&huge), Err(Error::Range));
}

// ====== rejected_layout ======

#[test]
fn rejected_layout() {
    let base = USER_BASE;
    let first = load(RX, PAGE, base, 16, PAGE + 1);
    assert_eq!(parse(&build(base, &[first, load(R, PAGE, base + PAGE, 16, 16)])), Err(Error::Order));
    assert_eq!(parse(&build(base + PAGE, &[load(RX, PAGE, base + PAGE, 16, 16), load(R, PAGE, base, 16, 16)])), Err(Error::Order));
    let many: Vec<_> = (0..=MAX_SEGMENTS as u64).map(|i| load(RX, PAGE, base + i * PAGE, 16, PAGE)).collect();
    assert_eq!(parse(&build(base, &many)), Err(Error::SegmentCount));
    let mut note = load(RX, PAGE, base, 16, 16);
    note.kind = 4;
    assert_eq!(parse(&build(base, &[note])), Err(Error::SegmentCount));
    let split = [load(RX, PAGE, base, 16, PAGE), load(R, PAGE, base + 2 * PAGE, 16, PAGE)];
    assert!(parse(&build(base + 2 * PAGE, &split)).is_err());
    for entry in [base + 16, base - 1, 0, u64::MAX] {
        assert_eq!(parse(&build(entry, &[load(RX, PAGE, base, 16, PAGE)])), Err(Error::Entry), "{entry:x}");
    }
    assert_eq!(parse(&build(base, &[load(RW, PAGE, base, 16, 16)])), Err(Error::Entry));
    assert_eq!(parse(&build(base, &[load(R, PAGE, base, 16, 16)])), Err(Error::Entry));
}

// ====== truncated_real_elf ======

#[test]
fn truncated_real_elf() {
    let bytes = fixture();
    let image = parse(&bytes).unwrap();
    let needed = image.segments[..image.count].iter().map(|s| s.file_offset + s.file_bytes).max().unwrap() as usize;
    for length in 0..bytes.len() {
        let result = parse(&bytes[..length]);
        if length < needed {
            assert!(result.is_err(), "length {length}");
        } else {
            assert_eq!(result, Ok(image));
        }
    }
}

// ====== every_header_bit ======

#[test]
fn every_header_bit() {
    let bytes = fixture();
    let table = u64::from_le_bytes(bytes[32..40].try_into().unwrap()) as usize;
    let count = u16::from_le_bytes(bytes[56..58].try_into().unwrap()) as usize;
    let limit = table + count * 56;
    for position in (0..64).chain(table..limit) {
        for bit in 0..8 {
            let mut mutated = bytes.clone();
            mutated[position] ^= 1 << bit;
            if let Ok(image) = parse(&mutated) {
                invariants(&image);
            }
        }
    }
}

// ====== random_header_mutations ======

#[test]
fn random_header_mutations() {
    let bytes = fixture();
    let limit = (64 + 3 * 56).min(bytes.len());
    let mut state = 0x9e3779b97f4a7c15u64;
    let mut next = move || {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        state
    };
    let mut accepted = 0;
    for _ in 0..200_000 {
        let mut mutated = bytes.clone();
        for _ in 0..1 + next() % 8 {
            let position = (next() % limit as u64) as usize;
            mutated[position] = match next() % 4 {
                0 => 0,
                1 => 0xff,
                2 => mutated[position] ^ (1 << (next() % 8)),
                _ => next() as u8,
            };
        }
        if let Ok(image) = parse(&mutated) {
            invariants(&image);
            accepted += 1;
        }
    }
    assert!(accepted < 200_000);
}
