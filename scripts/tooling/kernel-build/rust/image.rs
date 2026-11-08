const LAYOUT: &str = include_str!("../../../../arch/x86/configs/layout.h");
const PAGE: u64 = 4096;
const MAX_IMAGE: usize = 16_777_216;
const MAX_SECTION: u64 = 134_217_728;
const IMAGE_SPAN: u64 = 0x800_0000;
const SECTION_RIGHTS: u32 = 0xE000_0000;
const CODE_RIGHTS: u32 = 0x6000_0000;
const DATA_RIGHTS: u32 = 0xC000_0000;
const READ_RIGHTS: u32 = 0x4000_0000;
const METADATA_DIRECTORIES: [usize; 2] = [3, 6];

struct Section {
    address: u64,
    offset: u64,
    raw: u64,
    bytes: u64,
    memory: u64,
    flags: u32,
}

// ====== image_base ======

pub fn image_base() -> Result<u64, String> {
    LAYOUT
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
        .ok_or_else(|| "Base immagine kernel non valida.".to_string())
}

// ====== field ======

fn field(data: &[u8], offset: u64, width: usize) -> Result<u64, String> {
    let start = usize::try_from(offset).map_err(|_| "PE troncato.")?;
    let end = start.checked_add(width).ok_or("PE troncato.")?;
    let bytes = data.get(start..end).ok_or("PE troncato.")?;
    Ok(bytes
        .iter()
        .rev()
        .fold(0u64, |value, byte| (value << 8) | u64::from(*byte)))
}

// ====== page_round ======

fn page_round(value: u64) -> u64 {
    value.div_ceil(PAGE) * PAGE
}

// ====== expected_rights ======

fn expected_rights(name: &str) -> Result<u32, String> {
    match name {
        ".text" => Ok(CODE_RIGHTS),
        ".data" => Ok(DATA_RIGHTS),
        ".rdata" | ".pdata" => Ok(READ_RIGHTS),
        _ => Err(format!("Sezione non supportata: {name}")),
    }
}

// ====== check_header ======

fn check_header(data: &[u8], base: u64) -> Result<(u64, u64, u64), String> {
    if data.len() < 256 || data.len() > MAX_IMAGE {
        return Err("Dimensione PE fuori profilo.".into());
    }
    let pe = field(data, 60, 4)?;
    if pe > 65536
        || field(data, 0, 2)? != 0x5a4d
        || field(data, pe, 4)? != 0x4550
        || field(data, pe + 4, 2)? != 0x8664
    {
        return Err("PE x64 richiesto.".into());
    }
    let optional = pe + 24;
    let count = field(data, pe + 6, 2)?;
    if !(1..=16).contains(&count)
        || field(data, pe + 20, 2)? != 240
        || field(data, optional, 2)? != 0x20b
        || field(data, optional + 24, 8)? != base
        || field(data, optional + 32, 4)? != PAGE
        || field(data, optional + 108, 4)? != 16
    {
        return Err("Profilo kernel PE inatteso.".into());
    }
    for index in 0..16 {
        if !METADATA_DIRECTORIES.contains(&index)
            && field(data, optional + 112 + 8 * index as u64, 8)? != 0
        {
            return Err("Directory runtime non consentita.".into());
        }
    }
    Ok((optional, count, base + field(data, optional + 16, 4)?))
}

// ====== read_sections ======

fn read_sections(
    data: &[u8],
    base: u64,
    optional: u64,
    count: u64,
    entry: u64,
) -> Result<(Vec<Section>, u64), String> {
    let mut sections = Vec::new();
    let mut names = Vec::new();
    let mut file_end = PAGE;
    let mut previous_end = base;
    let mut entry_found = false;
    let length = data.len() as u64;

    for index in 0..count {
        let header = optional + 240 + index * 40;
        field(data, header + 36, 4)?;
        let start = header as usize;
        let name: String = data[start..start + 8]
            .iter()
            .map(|byte| *byte as char)
            .collect::<String>()
            .trim_end_matches('\0')
            .to_string();
        if names.contains(&name) {
            return Err("Sezione duplicata.".into());
        }
        names.push(name.clone());

        let size = field(data, header + 8, 4)?;
        let rva = field(data, header + 12, 4)?;
        let raw_size = field(data, header + 16, 4)?;
        let raw = field(data, header + 20, 4)?;
        let rights = field(data, header + 36, 4)? as u32 & SECTION_RIGHTS;
        let expected = expected_rights(&name)?;
        let address = base + rva;
        let memory = size.max(raw_size);
        if rights != expected
            || memory == 0
            || memory > MAX_SECTION
            || address & (PAGE - 1) != 0
            || address < previous_end
            || address + memory > base + IMAGE_SPAN
            || raw > length
            || raw_size > length - raw
        {
            return Err("Sezione fuori limiti o con permessi inattesi.".into());
        }

        let flags = match name.as_str() {
            ".text" => 5,
            ".data" => 6,
            _ => 4,
        };
        for directory in METADATA_DIRECTORIES {
            let place = optional + 112 + 8 * directory as u64;
            let directory_rva = field(data, place, 4)?;
            let directory_size = field(data, place + 4, 4)?;
            if directory_size != 0
                && directory_rva >= rva
                && directory_rva < rva + memory
                && (flags != 4 || directory_rva + directory_size > rva + memory)
            {
                return Err("Metadati PE fuori dalla sezione in sola lettura.".into());
            }
        }

        sections.push(Section {
            address,
            offset: file_end,
            raw,
            bytes: raw_size,
            memory,
            flags,
        });
        if flags == 5 && entry >= address && entry < address + raw_size {
            entry_found = true;
        }
        previous_end = address + page_round(memory);
        file_end += page_round(raw_size);
    }
    if !entry_found {
        return Err("Ingresso fuori dal codice.".into());
    }
    Ok((sections, file_end))
}

// ====== check_directories ======

fn check_directories(
    data: &[u8],
    base: u64,
    optional: u64,
    sections: &[Section],
) -> Result<(), String> {
    for directory in METADATA_DIRECTORIES {
        let place = optional + 112 + 8 * directory as u64;
        let rva = field(data, place, 4)?;
        let size = field(data, place + 4, 4)?;
        if rva == 0 && size == 0 {
            continue;
        }
        let contained = sections.iter().any(|section| {
            let start = section.address - base;
            rva != 0
                && size != 0
                && section.flags == 4
                && rva >= start
                && rva + size <= start + section.bytes
        });
        if !contained {
            return Err("Directory PE non contenuta nei dati presenti in sola lettura.".into());
        }
    }
    Ok(())
}

// ====== put ======

fn put(elf: &mut [u8], offset: usize, value: u64, width: usize) {
    elf[offset..offset + width].copy_from_slice(&value.to_le_bytes()[..width]);
}

// ====== convert ======

pub fn convert(data: &[u8], base: u64) -> Result<Vec<u8>, String> {
    let (optional, count, entry) = check_header(data, base)?;
    let (sections, file_end) = read_sections(data, base, optional, count, entry)?;
    check_directories(data, base, optional, &sections)?;

    let mut elf = vec![0u8; file_end as usize];
    elf[..7].copy_from_slice(&[127, 69, 76, 70, 2, 1, 1]);
    put(&mut elf, 16, 2, 2);
    put(&mut elf, 18, 62, 2);
    put(&mut elf, 20, 1, 4);
    put(&mut elf, 24, entry, 8);
    put(&mut elf, 32, 64, 8);
    put(&mut elf, 52, 64, 2);
    put(&mut elf, 54, 56, 2);
    put(&mut elf, 56, count, 2);

    for (index, section) in sections.iter().enumerate() {
        let header = 64 + index * 56;
        put(&mut elf, header, 1, 4);
        put(&mut elf, header + 4, u64::from(section.flags), 4);
        put(&mut elf, header + 8, section.offset, 8);
        put(&mut elf, header + 16, section.address, 8);
        put(&mut elf, header + 24, section.address, 8);
        put(&mut elf, header + 32, section.bytes, 8);
        put(&mut elf, header + 40, section.memory, 8);
        put(&mut elf, header + 48, PAGE, 8);
        let target = section.offset as usize;
        let source = section.raw as usize;
        let bytes = section.bytes as usize;
        elf[target..target + bytes].copy_from_slice(&data[source..source + bytes]);
    }
    Ok(elf)
}
