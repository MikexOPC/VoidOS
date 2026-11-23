use crate::parser::{parse, Error, Image, USER_BASE};

pub const IMAGE_END: u64 = 0xbef000;
pub const STACK_BASE: u64 = 0xbf0000;
pub const STACK_TOP: u64 = 0xc00000;
pub const USER_END: u64 = 0xc00000;
// 1536 pages = 6 MiB image ceiling inside 4 consecutive user PTs [0x400000, 0xbef000).
pub const MAX_IMAGE_PAGES: usize = 1536;
pub const FLAG_WRITE: u64 = 1;
pub const FLAG_EXECUTE: u64 = 2;

pub const REJECT_RANGE: u32 = 100;
pub const REJECT_PAGES: u32 = 101;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PlanPage {
    pub virt: u64,
    pub file_offset: u64,
    pub file_bytes: u64,
    pub flags: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProcessPlan {
    pub entry: u64,
    pub count: u64,
    pub pages: [PlanPage; MAX_IMAGE_PAGES],
}

impl Default for ProcessPlan {
    fn default() -> Self {
        Self {
            entry: 0,
            count: 0,
            pages: [PlanPage { virt: 0, file_offset: 0, file_bytes: 0, flags: 0 }; MAX_IMAGE_PAGES],
        }
    }
}

// ====== error_code ======

fn error_code(error: Error) -> u32 {
    error as u32 + 1
}

// ====== fill ======

fn fill(image: &Image, plan: &mut ProcessPlan) -> Result<(), u32> {
    plan.entry = image.entry;
    plan.count = 0;
    for page in image.pages() {
        if page.virt < USER_BASE || page.virt >= IMAGE_END {
            return Err(REJECT_RANGE);
        }
        let slot = plan.pages.get_mut(plan.count as usize).ok_or(REJECT_PAGES)?;
        let write = if page.writable { FLAG_WRITE } else { 0 };
        let execute = if page.executable { FLAG_EXECUTE } else { 0 };
        *slot = PlanPage {
            virt: page.virt,
            file_offset: page.file_offset,
            file_bytes: page.file_bytes,
            flags: write | execute,
        };
        plan.count += 1;
    }
    Ok(())
}

// ====== build_with_limit ======

pub fn build_with_limit(bytes: &[u8], max_pages: u64, plan: &mut ProcessPlan) -> Result<(), u32> {
    if max_pages == 0 || max_pages > MAX_IMAGE_PAGES as u64 {
        return Err(REJECT_PAGES);
    }
    let image = parse(bytes).map_err(error_code)?;
    if image.page_count > max_pages {
        return Err(REJECT_PAGES);
    }
    fill(&image, plan)
}

// ====== build ======

pub fn build(bytes: &[u8], plan: &mut ProcessPlan) -> Result<(), u32> {
    build_with_limit(bytes, MAX_IMAGE_PAGES as u64, plan)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::PAGE;

    // ====== elf ======

    fn elf(entry: u64, flags: u32, virt: u64, memory: u64) -> Vec<u8> {
        let mut bytes = vec![0u8; 0x2000];
        bytes[..8].copy_from_slice(&[0x7f, b'E', b'L', b'F', 2, 1, 1, 0]);
        bytes[16..18].copy_from_slice(&2u16.to_le_bytes());
        bytes[18..20].copy_from_slice(&62u16.to_le_bytes());
        bytes[20..24].copy_from_slice(&1u32.to_le_bytes());
        bytes[24..32].copy_from_slice(&entry.to_le_bytes());
        bytes[32..40].copy_from_slice(&64u64.to_le_bytes());
        bytes[52..54].copy_from_slice(&64u16.to_le_bytes());
        bytes[54..56].copy_from_slice(&56u16.to_le_bytes());
        bytes[56..58].copy_from_slice(&1u16.to_le_bytes());
        bytes[64..68].copy_from_slice(&1u32.to_le_bytes());
        bytes[68..72].copy_from_slice(&flags.to_le_bytes());
        for (position, value) in [(8, 0x1000), (16, virt), (24, virt), (32, 16), (40, memory), (48, PAGE)] {
            bytes[64 + position..64 + position + 8].copy_from_slice(&value.to_le_bytes());
        }
        bytes
    }

    // ====== accepted_plan ======

    #[test]
    fn accepted_plan() {
        let mut plan = ProcessPlan::default();
        assert_eq!(build(&elf(USER_BASE, 5, USER_BASE, 2 * PAGE), &mut plan), Ok(()));
        assert_eq!((plan.entry, plan.count), (USER_BASE, 2));
        assert_eq!(plan.pages[0], PlanPage { virt: USER_BASE, file_offset: 0x1000, file_bytes: 16, flags: FLAG_EXECUTE });
        assert_eq!(plan.pages[1].file_bytes, 0);
        let last = IMAGE_END - PAGE;
        assert_eq!(build(&elf(last, 5, last, PAGE), &mut plan), Ok(()));
        let full = MAX_IMAGE_PAGES as u64 * PAGE;
        assert_eq!(build(&elf(USER_BASE, 5, USER_BASE, full), &mut plan), Ok(()));
        assert_eq!(plan.count, MAX_IMAGE_PAGES as u64);
    }

    // ====== rejected_plan ======

    #[test]
    fn rejected_plan() {
        let mut plan = ProcessPlan::default();
        assert_eq!(build(&elf(IMAGE_END, 5, IMAGE_END, PAGE), &mut plan), Err(REJECT_RANGE));
        assert_eq!(build(&elf(STACK_BASE, 5, STACK_BASE, PAGE), &mut plan), Err(REJECT_RANGE));
        let over = (MAX_IMAGE_PAGES as u64 + 1) * PAGE;
        assert_eq!(build(&elf(USER_BASE, 5, USER_BASE, over), &mut plan), Err(REJECT_PAGES));
        assert_eq!(build(&[], &mut plan), Err(Error::TooSmall as u32 + 1));
    }

    // ====== budgeted_plan ======

    #[test]
    fn budgeted_plan() {
        let mut plan = ProcessPlan::default();
        let four_pages = elf(USER_BASE, 5, USER_BASE, 4 * PAGE);
        assert_eq!(build_with_limit(&four_pages, 4, &mut plan), Ok(()));
        assert_eq!(plan.count, 4);
        assert_eq!(build_with_limit(&four_pages, 3, &mut plan), Err(REJECT_PAGES));
        assert_eq!(build_with_limit(&four_pages, 0, &mut plan), Err(REJECT_PAGES));
        assert_eq!(build_with_limit(&four_pages, MAX_IMAGE_PAGES as u64 + 1, &mut plan), Err(REJECT_PAGES));
        let sixty_four = elf(USER_BASE, 5, USER_BASE, 64 * PAGE);
        assert_eq!(build_with_limit(&sixty_four, 64, &mut plan), Ok(()));
        assert_eq!(plan.count, 64);
    }
}
