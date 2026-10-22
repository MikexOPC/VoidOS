#include "mock.h"
static unsigned char table[60];
static unsigned calls,fail_at,mutate;

// ====== mocked ACPI reader and directory validation ======

int kernel_acpi_directory_valid(const KERNEL_ACPI_DIRECTORY *directory,BOOT_U32 bits)
{ return directory && directory->Count<=256 && bits>=32 && bits<=52; }
BOOT_U32 kernel_acpi_physical_span(BOOT_U64 address,BOOT_U32 bytes,BOOT_U32 bits)
{ return bits>=32 && bits<=52 && bytes && address && address<=(1ULL<<bits)-bytes; }
static int read_private(void *state,BOOT_U64 address,BOOT_U32 bytes,unsigned char *out)
{
    CHECK(state==table && address>=0x100000 && address<=0x101000 && bytes<=sizeof(table));
    if (++calls==fail_at) return 0;
    memcpy(out,table,bytes);
    if (mutate && bytes>36) out[8]=2;
    return 1;
}

// ====== finder_cases ======

void finder_cases(void)
{
    KERNEL_ACPI_DIRECTORY directory={0}; X86_MCFG out,before; unsigned char scratch[X86_MCFG_BYTES];
    memset(table,0,sizeof(table)); memcpy(table,"MCFG",4); table[4]=60; table[8]=1; table[47]=0xe0; table[55]=255;
    unsigned sum=0; for (unsigned i=0; i<60; ++i) sum+=table[i]; table[9]=(unsigned char)(0-sum);
    directory.Count=1; directory.Entries[0]=0x100000;
    CHECK(x86_mcfg_find(&directory,52,read_private,table,scratch,sizeof(scratch),&out)==1 && out.Physical==0x100000);
    before=out;
    for (fail_at=1; fail_at<=2; ++fail_at) {
        calls=0; CHECK(!x86_mcfg_find(&directory,52,read_private,table,scratch,sizeof(scratch),&out) && !memcmp(&out,&before,sizeof(out)));
    }
    fail_at=0; calls=0; mutate=1;
    CHECK(!x86_mcfg_find(&directory,52,read_private,table,scratch,sizeof(scratch),&out) && !memcmp(&out,&before,sizeof(out)));
    mutate=0; directory.Count=2; directory.Entries[1]=0x101000; calls=0;
    CHECK(!x86_mcfg_find(&directory,52,read_private,table,scratch,sizeof(scratch),&out) && !memcmp(&out,&before,sizeof(out)));
    directory.Count=0;
    CHECK(x86_mcfg_find(&directory,52,read_private,table,scratch,sizeof(scratch),&out)==2 && !memcmp(&out,&before,sizeof(out)));
}
