#include "mock.h"

// ====== walk_private ======

static BOOT_U64 walk_private(BOOT_U64 iova,unsigned *access)
{
    const BOOT_U64 *root=(const BOOT_U64 *)domain.Tables[0].Base;
    BOOT_U64 context_entry=root[request.Bus*2];
    CHECK(context_entry==domain.Tables[1].Base+1);
    const BOOT_U64 *contexts=(const BOOT_U64 *)(context_entry&~4095ULL);
    unsigned offset=(request.Device*8+request.Function)*2;
    CHECK(contexts[offset+1]==0x102);
    const BOOT_U64 *table=(const BOOT_U64 *)(contexts[offset]&~4095ULL);
    *access=3;
    for (unsigned level=4; level; --level) {
        BOOT_U64 entry=table[(iova>>(12+9*(level-1)))&511];
        CHECK(!(entry&0xfff0000000000ffcULL));
        *access&=(unsigned)(entry&3);
        if (!*access) return 0;
        if (level==1) return (entry&~4095ULL)|(iova&4095);
        table=(const BOOT_U64 *)(entry&~4095ULL);
    }
    return 0;
}

// ====== domain_walk_check ======

void domain_walk_check(void)
{
    const BOOT_U64 *root=(const BOOT_U64 *)domain.Tables[0].Base;
    const BOOT_U64 *contexts=(const BOOT_U64 *)domain.Tables[1].Base;
    for (unsigned slot=0; slot<512; ++slot) {
        CHECK(slot==request.Bus*2?root[slot]==domain.Tables[1].Base+1:root[slot]==0);
        unsigned selected=(request.Device*8+request.Function)*2;
        CHECK(slot==selected?contexts[slot]==domain.Tables[2].Base+1:slot==selected+1?contexts[slot]==0x102:contexts[slot]==0);
    }
    for (BOOT_U64 address=X86_VTD_DMA_BASE; address<X86_VTD_DMA_BASE+X86_VTD_DMA_BYTES; address+=4096) {
        BOOT_U64 expected=0; unsigned rights=0,actual=0;
        for (unsigned index=0; index<request.Count; ++index)
            if (!((domain.Revoked>>index)&1) && address>=domain.Iova[index] && address-domain.Iova[index]<request.Buffers[index].Lease.Pages*4096) {
                expected=request.Buffers[index].Lease.Base+address-domain.Iova[index]; rights=request.Buffers[index].Access;
            }
        CHECK(walk_private(address,&actual)==expected && actual==rights);
    }
}
