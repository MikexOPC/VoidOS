pub const PAGE: u64 = 4096;
pub const USER_BASE: u64 = 0x400000;
pub const IMAGE_LIMIT: u64 = 0x3000_0000;
pub const MAX_SEGMENTS: usize = 8;
pub const MAX_PAGES: u64 = 1024;

const HEADER_BYTES: usize = 64;
const PROGRAM_BYTES: usize = 56;
const MAX_PROGRAM_HEADERS: usize = 32;
const PT_NULL: u32 = 0;
const PT_LOAD: u32 = 1;
const PT_NOTE: u32 = 4;
const PF_X: u32 = 1;
const PF_W: u32 = 2;
const PF_R: u32 = 4;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    TooSmall,
    Identity,
    Type,
    Machine,
    Header,
    ProgramTable,
    SegmentType,
    SegmentCount,
    Flags,
    WriteExecute,
    Alignment,
    FileRange,
    MemorySize,
    Range,
    Order,
    Entry,
    Pages,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Segment {
    pub virt: u64,
    pub memory_bytes: u64,
    pub file_offset: u64,
    pub file_bytes: u64,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Image {
    pub entry: u64,
    pub count: usize,
    pub page_count: u64,
    pub segments: [Segment; MAX_SEGMENTS],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PagePlan {
    pub virt: u64,
    pub file_offset: u64,
    pub file_bytes: u64,
    pub writable: bool,
    pub executable: bool,
}

// ====== read_u16 ======

fn read_u16(bytes: &[u8], offset: usize) -> Option<u16> {
    let end = offset.checked_add(2)?;
    let field: [u8; 2] = bytes.get(offset..end)?.try_into().ok()?;
    Some(u16::from_le_bytes(field))
}

// ====== read_u32 ======

fn read_u32(bytes: &[u8], offset: usize) -> Option<u32> {
    let end = offset.checked_add(4)?;
    let field: [u8; 4] = bytes.get(offset..end)?.try_into().ok()?;
    Some(u32::from_le_bytes(field))
}

// ====== read_u64 ======

fn read_u64(bytes: &[u8], offset: usize) -> Option<u64> {
    let end = offset.checked_add(8)?;
    let field: [u8; 8] = bytes.get(offset..end)?.try_into().ok()?;
    Some(u64::from_le_bytes(field))
}

// ====== page_span ======

fn page_span(memory_bytes: u64) -> Option<u64> {
    Some(memory_bytes.checked_add(PAGE - 1)? / PAGE)
}

// ====== check_identity ======

fn check_identity(bytes: &[u8]) -> Result<(), Error> {
    if bytes.len() < HEADER_BYTES {
        return Err(Error::TooSmall);
    }
    let identity = &bytes[..16];
    let valid = identity[..4] == [0x7f, b'E', b'L', b'F']
        && identity[4] == 2
        && identity[5] == 1
        && identity[6] == 1
        && identity[7] == 0;
    if !valid {
        return Err(Error::Identity);
    }
    if read_u16(bytes, 16) != Some(2) {
        return Err(Error::Type);
    }
    if read_u16(bytes, 18) != Some(62) {
        return Err(Error::Machine);
    }
    if read_u32(bytes, 20) != Some(1) || read_u16(bytes, 52) != Some(HEADER_BYTES as u16) {
        return Err(Error::Header);
    }
    Ok(())
}

// ====== read_segment ======

fn read_segment(bytes: &[u8], offset: usize, limit: u64) -> Result<Segment, Error> {
    let field = |position: usize| {
        read_u64(bytes, offset + position).ok_or(Error::ProgramTable)
    };
    let flags = read_u32(bytes, offset + 4).ok_or(Error::ProgramTable)?;
    let segment = Segment {
        virt: field(16)?,
        memory_bytes: field(40)?,
        file_offset: field(8)?,
        file_bytes: field(32)?,
        flags,
    };
    let alignment = field(48)?;
    if flags & !(PF_R | PF_W | PF_X) != 0 || flags & PF_R == 0 {
        return Err(Error::Flags);
    }
    if flags & PF_W != 0 && flags & PF_X != 0 {
        return Err(Error::WriteExecute);
    }
    if alignment < PAGE || !alignment.is_power_of_two() || !segment.virt.is_multiple_of(PAGE) || !segment.file_offset.is_multiple_of(PAGE) {
        return Err(Error::Alignment);
    }
    let file_end = segment.file_offset.checked_add(segment.file_bytes).ok_or(Error::FileRange)?;
    if file_end > limit {
        return Err(Error::FileRange);
    }
    if segment.memory_bytes == 0 || segment.file_bytes > segment.memory_bytes {
        return Err(Error::MemorySize);
    }
    let end = segment.virt.checked_add(segment.memory_bytes).ok_or(Error::Range)?;
    if segment.virt < USER_BASE || end > IMAGE_LIMIT {
        return Err(Error::Range);
    }
    Ok(segment)
}

// ====== parse ======

pub fn parse(bytes: &[u8]) -> Result<Image, Error> {
    check_identity(bytes)?;
    let entry = read_u64(bytes, 24).ok_or(Error::Header)?;
    let table = read_u64(bytes, 32).ok_or(Error::Header)?;
    let entry_bytes = read_u16(bytes, 54).ok_or(Error::Header)? as usize;
    let headers = read_u16(bytes, 56).ok_or(Error::Header)? as usize;
    if entry_bytes != PROGRAM_BYTES || headers == 0 || headers > MAX_PROGRAM_HEADERS {
        return Err(Error::ProgramTable);
    }
    let table_end = table.checked_add((headers * PROGRAM_BYTES) as u64).ok_or(Error::ProgramTable)?;
    if table < HEADER_BYTES as u64 || table_end > bytes.len() as u64 {
        return Err(Error::ProgramTable);
    }
    let mut image = Image { entry, count: 0, page_count: 0, segments: [Segment::default(); MAX_SEGMENTS] };
    let mut floor = 0u64;
    let mut entry_valid = false;
    for index in 0..headers {
        let offset = table as usize + index * PROGRAM_BYTES;
        let kind = read_u32(bytes, offset).ok_or(Error::ProgramTable)?;
        match kind {
            PT_NULL | PT_NOTE => continue,
            PT_LOAD => {}
            _ => return Err(Error::SegmentType),
        }
        if image.count == MAX_SEGMENTS {
            return Err(Error::SegmentCount);
        }
        let segment = read_segment(bytes, offset, bytes.len() as u64)?;
        if segment.virt < floor {
            return Err(Error::Order);
        }
        let pages = page_span(segment.memory_bytes).ok_or(Error::Pages)?;
        image.page_count = image.page_count.checked_add(pages).ok_or(Error::Pages)?;
        if image.page_count > MAX_PAGES {
            return Err(Error::Pages);
        }
        floor = segment.virt + pages * PAGE;
        entry_valid |= segment.flags & PF_X != 0
            && entry >= segment.virt
            && entry - segment.virt < segment.file_bytes;
        image.segments[image.count] = segment;
        image.count += 1;
    }
    if image.count == 0 {
        return Err(Error::SegmentCount);
    }
    if !entry_valid {
        return Err(Error::Entry);
    }
    Ok(image)
}

// ====== Image::pages ======

impl Image {
    pub fn pages(&self) -> impl Iterator<Item = PagePlan> + '_ {
        self.segments[..self.count].iter().flat_map(|segment| {
            let total = segment.memory_bytes.div_ceil(PAGE);
            (0..total).map(move |index| {
                let offset = index * PAGE;
                PagePlan {
                    virt: segment.virt + offset,
                    file_offset: segment.file_offset + offset,
                    file_bytes: segment.file_bytes.saturating_sub(offset).min(PAGE),
                    writable: segment.flags & PF_W != 0,
                    executable: segment.flags & PF_X != 0,
                }
            })
        })
    }
}
