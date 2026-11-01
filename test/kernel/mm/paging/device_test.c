#include "fixture.h"

// ====== video_authority ======

static int video_authority(const X86_PAGING_VIDEO_AUTH *owner)
{
    return ++video_calls != video_failure && owner == &video && owner->Root == 4096;
}

// ====== video_fixture ======

static int video_fixture(void)
{
    BOOT_U32 index;
    if (!fixture()) return 0;
    clear(&video, sizeof(video)); video_calls = video_failure = 0;
    mapping.Self = (BOOT_U64)&mapping; mapping.Ready = 1; mapping.Root = 4096;
    mapping.Physical = 0xfee00000; mapping.Cache = 0x18; mapping.PhysicalBits = 32;
    tables[3][288] = 0x80000000fee0001bULL;
    tables[3][1] = 0x8000000000300003ULL;
    video.Self = (BOOT_U64)&video; video.Root = 4096; video.Ready = 1; video.PhysicalBits = 32;
    video.Region.Virtual = X86_FB_VIRTUAL;
    video.Region.Physical = 0xc0000000; video.Region.Pages = 3; video.Region.AperturePages = 8;
    video.Region.Cache = 0x18; video.Region.Pat = 0x0007040600070406ULL;
    video.Region.Owner = (BOOT_U64)&video; video.Region.TableCount = 3;
    context.Info.Framebuffer.Base = video.Region.Physical;
    context.Info.Framebuffer.Capacity = 32768; context.Info.Framebuffer.Used = 12288;
    context.Info.Framebuffer.Width = context.Info.Framebuffer.Stride = 1024;
    context.Info.Framebuffer.Height = 3;
    table_count = 8;
    for (index = 0; index < 3; ++index) {
        if (!kernel_physical_allocate(&pool, 1, &video.Tables[index]) ||
            !kernel_physical_pin(&pool, &video.Tables[index], 1)) return 0;
        if (!index) {
            tables[0][X86_FB_SLOT] = video.Tables[index].Base | 3;
            views[5] = x86_paging_child_view(views[0], 0, X86_FB_SLOT);
        } else {
            tables[4 + index][0] = video.Tables[index].Base | 3;
            views[5 + index] = x86_paging_child_view(views[4 + index], index, 0);
        }
    }
    for (index = 0; index < 3; ++index)
        tables[7][index + 1] = (video.Region.Physical + (BOOT_U64)index * 4096) | 0x800000000000001bULL;
    return 1;
}

// ====== video_owned ======

int video_owned(void)
{
    BOOT_U32 index;
    if (!video_fixture() || x86_paging_inventory_capture_lapic(&context, &pool, &mapping, &ledger) ||
        !x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger) ||
        !x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger) ||
        ledger.Count != 8 || ledger.MmioPages != 4 || ledger.RamPages != 2 || pool.FreePages != 125) return 1;
    for (index = 0; index < 3; ++index) {
        BOOT_U32 record, found = 0;
        for (record = 0; record < ledger.Count; ++record)
            if (ledger.Tables[record].Reserved == index + 1 &&
                ledger.Tables[record].Physical == video.Tables[index].Base) ++found;
        if (found != 1) return 2;
    }
    save_ledger(); video_calls = 0; video_failure = 2;
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 3;
    for (index = 0; index < sizeof(ledger); ++index)
        if (((unsigned char *)&ledger)[index] != ((unsigned char *)&saved)[index]) return 4;
    video_failure = 0;
    for (index = 0; index < 3; ++index) {
        if (!kernel_physical_pin(&pool, &video.Tables[index], 0)) return 5;
        reads = 0;
        if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger) || reads) return 6;
        if (!kernel_physical_pin(&pool, &video.Tables[index], 1)) return 7;
    }
    if (!kernel_physical_pin(&pool, &video.Tables[0], 0) ||
        !kernel_physical_release(&pool, &video.Tables[0]) ||
        !kernel_physical_allocate(&pool, 1, &video.Tables[0]) ||
        !kernel_physical_pin(&pool, &video.Tables[0], 1)) return 10;
    if (video.Tables[0].Base != 0x100000 ||
        video.Tables[0].Id == ledger.VideoTableIds[0]) return 12;
    if (x86_paging_inventory_verify_video(&context, &pool, &mapping,
        &video, video_authority, &ledger)) return 11;
    tables[0][X86_FB_SLOT] = 0;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_pin(&pool, &video.Tables[index], 0) ||
            !kernel_physical_release(&pool, &video.Tables[index])) return 8;
    return pool.FreePages == 128 ? 0 : 9;
}

// ====== video_corruption ======

int video_corruption(void)
{
    BOOT_U32 bit;
    if (!video_fixture() || !x86_paging_inventory_capture_video(&context, &pool, &mapping,
        &video, video_authority, &ledger)) return 1;
    for (bit = 0; bit < 64; ++bit) {
        tables[7][2] ^= 1ULL << bit;
        if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger) !=
            (bit == 5 || bit == 6)) return 2;
        tables[7][2] ^= 1ULL << bit;
    }
    for (bit = 0; bit < 2; ++bit) {
        BOOT_U32 slot = bit ? 4 : 0;
        tables[7][slot] = 0x80000000c000001bULL;
        if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 3;
        tables[7][slot] = 0;
    }
    tables[3][2] = 0x80000000c0007003ULL;
    if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 4;
    tables[3][2] = 0x8000000000100003ULL;
    if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 5;
    tables[3][2] = 0; tables[7][3] = 0;
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 6;
    if (!video_fixture()) return 7;
    video.Tables[1] = video.Tables[0];
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 8;
    if (!video_fixture() || !x86_paging_inventory_capture_video(&context, &pool, &mapping,
        &video, video_authority, &ledger)) return 9;
    for (bit = 0; bit < sizeof(ledger.Video) * 8; ++bit) {
        ((unsigned char *)&ledger.Video)[bit / 8] ^= (unsigned char)(1U << (bit % 8));
        if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 10;
        ((unsigned char *)&ledger.Video)[bit / 8] ^= (unsigned char)(1U << (bit % 8));
    }
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, 0, &ledger) ||
        x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority,
            (X86_PAGING_INVENTORY *)&video)) return 11;
    return 0;
}

// ====== dma_arena ======

static int dma_check(const X86_DMA_AUTH *owner) { return owner && (tables[0][X86_DMA_SLOT] & ~0x60ULL) == (owner->Tables[0].Base | 3); }

// Slot 229 arena: three owned tables, two WB/RW/XD leaves; scan, verify, corruption, alias, removal.
int dma_arena(void)
{
    static X86_DMA_AUTH arena;
    BOOT_U32 index;
    if (!fixture()) return 1;
    // The fixture leaf maps 0x100000, the first free page the arena receives: drop it, then check
    // below that a leaf naming arena RAM outside the slot is refused as an alias.
    tables[3][1] = 0;
    clear(&arena, sizeof(arena));
    if (!kernel_physical_allocate(&pool, 2, &arena.Data) || !kernel_physical_pin(&pool, &arena.Data, 1)) return 2;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_allocate(&pool, 1, &arena.Tables[index]) || !kernel_physical_pin(&pool, &arena.Tables[index], 1)) return 3;
    arena.Self = (BOOT_U64)&arena; arena.Root = 4096; arena.Pat = 6; arena.PhysicalBits = 32; arena.Ready = 1;
    tables[0][X86_DMA_SLOT] = arena.Tables[0].Base | 3;
    views[5] = x86_paging_child_view(views[0], 0, X86_DMA_SLOT);
    tables[5][0] = arena.Tables[1].Base | 3;
    views[6] = x86_paging_child_view(views[5], 1, 0);
    tables[6][0] = arena.Tables[2].Base | 3;
    views[7] = x86_paging_child_view(views[6], 2, 0);
    tables[7][1] = arena.Data.Base | 0x8000000000000003ULL;
    tables[7][2] = (arena.Data.Base + 4096) | 0x8000000000000003ULL;
    table_count = 8;
    // Unregistered tables are not trusted: the scan refuses them.
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 4;
    if (!x86_dma_register(0, &arena, dma_check) || !x86_paging_inventory_capture(&context, &pool, &ledger) ||
        ledger.Count != 8 || ledger.LeafPages != 3 || ledger.Dma.Pages != 2 || ledger.Dma.Owner != arena.Self ||
        !x86_paging_inventory_verify(&context, &pool, &ledger)) return 5;
    {
        BOOT_U32 tags = 0;
        for (index = 0; index < ledger.Count; ++index)
            if (ledger.Tables[index].Reserved >= 40) tags |= 1U << (ledger.Tables[index].Reserved - 40);
        if (tags != 7) return 14;
    }
    tables[7][2] |= 0x10;
    if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 6;
    tables[7][2] &= ~0x10ULL; tables[7][3] = (arena.Data.Base + 8192) | 0x8000000000000003ULL;
    if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 7;
    tables[7][3] = 0; tables[3][2] = arena.Data.Base | 0x8000000000000003ULL;
    if (x86_paging_inventory_capture(&context, &pool, &ledger) || x86_paging_inventory_verify(&context, &pool, &ledger)) return 8;
    tables[3][2] = 0;
    if (!x86_paging_inventory_capture(&context, &pool, &ledger) || !x86_dma_register(&arena, 0, 0) ||
        x86_paging_inventory_verify(&context, &pool, &ledger)) return 9;
    tables[0][X86_DMA_SLOT] = 0; table_count = 5;
    if (!x86_paging_inventory_capture(&context, &pool, &ledger) || ledger.Dma.Owner) return 10;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_pin(&pool, &arena.Tables[index], 0) || !kernel_physical_release(&pool, &arena.Tables[index])) return 11;
    if (!kernel_physical_pin(&pool, &arena.Data, 0) || !kernel_physical_release(&pool, &arena.Data)) return 12;
    return pool.FreePages == 128 ? 0 : 13;
}

// ====== video_arena ======

// Framebuffer authority and DMA arena together (V2b ordinary session): the video variant knows the
// arena tags, and with the framebuffer registered the LAPIC variants delegate to the video rule.
int video_arena(void)
{
    static X86_DMA_AUTH arena;
    BOOT_U32 index;
    if (!video_fixture()) return 1;
    clear(&arena, sizeof(arena));
    if (!kernel_physical_allocate(&pool, 1, &arena.Data) || !kernel_physical_pin(&pool, &arena.Data, 1)) return 2;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_allocate(&pool, 1, &arena.Tables[index]) || !kernel_physical_pin(&pool, &arena.Tables[index], 1)) return 3;
    arena.Self = (BOOT_U64)&arena; arena.Root = 4096; arena.Pat = 6; arena.PhysicalBits = 32; arena.Ready = 1;
    tables[0][X86_DMA_SLOT] = arena.Tables[0].Base | 3;
    views[8] = x86_paging_child_view(views[0], 0, X86_DMA_SLOT);
    tables[8][0] = arena.Tables[1].Base | 3;
    views[9] = x86_paging_child_view(views[8], 1, 0);
    tables[9][0] = arena.Tables[2].Base | 3;
    views[10] = x86_paging_child_view(views[9], 2, 0);
    tables[10][1] = arena.Data.Base | 0x8000000000000003ULL;
    table_count = 11;
    if (!x86_dma_register(0, &arena, dma_check) ||
        !x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger) ||
        !x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger) ||
        ledger.Count != 11 || ledger.Dma.Pages != 1) return 4;
    // Without the registration the LAPIC variant does not know the framebuffer leaves.
    if (x86_paging_inventory_verify_lapic(&context, &pool, &mapping, &ledger)) return 5;
    if (!x86_paging_video_register(0, &video, video_authority) || x86_paging_video_owner() != &video ||
        x86_paging_video_register(0, &video, video_authority) ||
        !x86_paging_inventory_verify_lapic(&context, &pool, &mapping, &ledger)) return 6;
    save_ledger();
    if (!x86_paging_inventory_capture_lapic(&context, &pool, &mapping, &ledger)) return 7;
    for (index = 0; index < sizeof(ledger); ++index)
        if (((unsigned char *)&ledger)[index] != ((unsigned char *)&saved)[index]) return 8;
    if (!x86_paging_video_register(&video, 0, 0) || x86_paging_video_owner() ||
        x86_paging_inventory_verify_lapic(&context, &pool, &mapping, &ledger)) return 9;
    if (!x86_dma_register(&arena, 0, 0)) return 10;
    tables[0][X86_DMA_SLOT] = 0; tables[0][X86_FB_SLOT] = 0;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_pin(&pool, &arena.Tables[index], 0) || !kernel_physical_release(&pool, &arena.Tables[index]) ||
            !kernel_physical_pin(&pool, &video.Tables[index], 0) || !kernel_physical_release(&pool, &video.Tables[index])) return 11;
    if (!kernel_physical_pin(&pool, &arena.Data, 0) || !kernel_physical_release(&pool, &arena.Data)) return 12;
    return pool.FreePages == 128 ? 0 : 13;
}
