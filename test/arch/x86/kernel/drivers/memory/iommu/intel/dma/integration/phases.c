#include "probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"

// Buffer contents: byte i of a page filled with seed s is (s + i) & 255, seed 0 is all zero.
typedef struct { BOOT_U32 Head, Tail, Write; } DMA_PAGE;

// ====== byte_private ======

static unsigned char byte_private(BOOT_U32 seed, BOOT_U32 index)
{
    return seed ? (unsigned char)((seed + index) & 255) : 0;
}

// ====== page_private ======

// Head covers the first X86_VTD_DMA_MOVE moved by a transfer, Tail the rest of the page.
static int page_private(void *opaque)
{
    const DMA_PAGE *page = opaque;
    volatile unsigned char *bytes = x86_window_bytes();

    if (!bytes) return 0;

    for (BOOT_U32 index = 0; index < 4096; ++index) {
        unsigned char expected = byte_private(index < X86_VTD_DMA_MOVE ? page->Head : page->Tail, index);

        if (page->Write) bytes[index] = expected;

        if (bytes[index] != expected) return 0;
    }

    // Written lines leave the CPU cache before the device reads them (VT-d5.0 6.8 covers tables only).
    return !page->Write || x86_qi_clean_page();
}

// ====== access_private ======

static int access_private(X86_VTD_DMA_SESSION *session, BOOT_U32 index, BOOT_U32 head, BOOT_U32 tail, BOOT_U32 write)
{
    DMA_PAGE page = {head, tail, write};

    if (index >= session->Domain->Request.Count) return 0;

    return x86_apic_borrow(session->Reader->Window, session->Domain->Request.Buffers[index].Lease.Base, 0,
        (int)write, page_private, &page) && x86_page_window_idle_valid(session->Reader->Window);
}

// ====== x86_vtd_dma_fill ======

int x86_vtd_dma_fill(X86_VTD_DMA_SESSION *session, BOOT_U32 index, BOOT_U32 seed)
{
    return access_private(session, index, seed, seed, 1);
}

// ====== iova_private ======

static BOOT_U64 iova_private(const X86_VTD_DMA_SESSION *session, BOOT_U32 index)
{
    return session->Domain->Iova[index];
}

// ====== move_private ======

// RAM buffer to the device buffer and back out to another IOVA, no fault allowed.
static int move_private(X86_VTD_DMA_SESSION *session, BOOT_U64 from, BOOT_U64 to)
{
    const EDU_BUS *bus = &session->Bus;

    return edu_transfer(bus, from, EDU_BUFFER, X86_VTD_DMA_MOVE, EDU_TO_DEVICE) &&
        x86_vtd_activation_faults(session->Activation) &&
        (!to || (edu_transfer(bus, EDU_BUFFER, to, X86_VTD_DMA_MOVE, EDU_TO_RAM) &&
            x86_vtd_activation_faults(session->Activation)));
}

// ====== blocked_private ======

// A provoked fault: the transfer completes (edu ignores DMA errors), the fault record proves the block.
static int blocked_private(X86_VTD_DMA_SESSION *session, BOOT_U64 address, BOOT_U32 direction)
{
    BOOT_U64 source = direction == EDU_TO_RAM ? EDU_BUFFER : address;
    BOOT_U64 target = direction == EDU_TO_RAM ? address : EDU_BUFFER;

    return edu_transfer(&session->Bus, source, target, X86_VTD_DMA_MOVE, direction) &&
        x86_vtd_activation_expect(session->Activation, direction == EDU_TO_RAM ? 5 : 6, direction == EDU_TO_RAM ? 0 : 1,
            address);
}

// ====== phase_private ======

static int phase_private(const char *name)
{
    return kernel_text("KERNEL_DMA_PHASE: ") && kernel_text(name) && kernel_text("\n");
}

// ====== revoke_private ======

// Leaf cleared, page walk caches flushed, then the global context/IOTLB invalidation and wait.
static int revoke_private(X86_VTD_DMA_SESSION *session, BOOT_U32 index)
{
    return x86_vtd_domain_revoke(session->Domain, index) &&
        x86_qi_clean_domain(session->Domain) &&
        x86_qi_submit(session->Qi) && x86_qi_finish(session->Qi);
}

// ====== x86_vtd_dma_phases ======

// Intel VT-d5.0 7.1.3 / QEMU v11.1.0 VTDFaultReason: 5 write without W, 6 read without R;
// an absent leaf has R=W=0. Buffers enter with A=0xa1, B=0, C=0x5c, D=0xd4.
int x86_vtd_dma_phases(X86_VTD_DMA_SESSION *session)
{
    BOOT_U64 a = iova_private(session, X86_VTD_DMA_A), b = iova_private(session, X86_VTD_DMA_B);
    BOOT_U64 c = iova_private(session, X86_VTD_DMA_C), d = iova_private(session, X86_VTD_DMA_D);
    BOOT_U64 absent = X86_VTD_DMA_BASE;

    // P1: allowed read of A and write of B.
    if (!move_private(session, a, b) ||
        !access_private(session, X86_VTD_DMA_B, 0xa1, 0, 0) ||
        !phase_private("P1_ALLOWED")
    )
    return 0;

    // P2: the device buffer now holds C; writing it into the read-only D is blocked.
    if (!move_private(session, c, 0) ||
        !blocked_private(session, d, EDU_TO_RAM) ||
        !access_private(session, X86_VTD_DMA_D, 0xd4, 0xd4, 0) ||
        !phase_private("P2_READ_ONLY")
    )
    return 0;

    // P3: the guard page below the first buffer has no leaf, for writes and reads.
    if (!blocked_private(session, absent, EDU_TO_RAM) ||
        !blocked_private(session, absent, EDU_TO_DEVICE) ||
        !phase_private("P3_UNMAPPED")
    )
    return 0;

    // P4: B worked in P1 and is cached in the IOTLB; after revocation the same write is blocked.
    if (!move_private(session, c, 0) ||
        !revoke_private(session, X86_VTD_DMA_B) ||
        !blocked_private(session, b, EDU_TO_RAM) ||
        !access_private(session, X86_VTD_DMA_B, 0xa1, 0, 0) ||
        !phase_private("P4_REVOKED")
    )
    return 0;

    // P5: the rest of the domain still translates after the revocation.
    if (!move_private(session, a, c) ||
        !access_private(session, X86_VTD_DMA_C, 0xa1, 0x5c, 0) ||
        !phase_private("P5_STILL_MAPPED")
    )
    return 0;

    return 1;
}
