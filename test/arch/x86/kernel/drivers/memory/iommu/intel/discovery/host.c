#include "arch/x86/kernel/drivers/memory/iommu/intel/discovery/find.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,reads,fail_read;
static unsigned char fixture[1024],scratch[X86_DMAR_MAX_BYTES],file_bytes[X86_DMAR_MAX_BYTES];
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL DMAR:%u\n",(unsigned)__LINE__);exit(1);} } while (0)

// ====== word_private ======

static void word_private(unsigned char *data,unsigned at,uint64_t value,unsigned bytes)
{
    for (unsigned index=0; index<bytes; ++index) data[at+index]=(unsigned char)(value>>(8*index));
}

// ====== checksum_private ======

static void checksum_private(unsigned length)
{
    unsigned sum=0;
    fixture[9]=0; word_private(fixture,4,length,4);
    for (unsigned index=0; index<length; ++index) sum+=fixture[index];
    fixture[9]=(unsigned char)(0U-sum);
}

// ====== prepare_private ======

static void prepare_private(void)
{
    memset(fixture,0,sizeof(fixture)); memcpy(fixture,"DMAR",4); fixture[8]=1; fixture[36]=39;
    word_private(fixture,50,32,2); fixture[52]=1; word_private(fixture,56,0xfed90000,8);
    fixture[64]=3; fixture[65]=8; fixture[70]=31;
    fixture[72]=4; fixture[73]=8; fixture[78]=31;
    checksum_private(80); reads=fail_read=0;
}

// ====== kernel_acpi_directory_valid ======

int kernel_acpi_directory_valid(const KERNEL_ACPI_DIRECTORY *directory,OS_ACPI_U32 bits)
{
    return directory && bits>=32 && bits<=52 && directory->Count<=256;
}

// ====== kernel_acpi_physical_span ======

OS_ACPI_U32 kernel_acpi_physical_span(OS_ACPI_U64 base,OS_ACPI_U32 bytes,OS_ACPI_U32 bits)
{
    return bits>=32 && bits<=52 && base>=4096 && bytes && base<=(1ULL<<bits)-bytes;
}

// ====== read_private ======

static int read_private(void *context,OS_ACPI_U64 address,OS_ACPI_U32 bytes,unsigned char *output)
{
    (void)context;
    if (++reads==fail_read || address!=0x100000 || bytes>sizeof(fixture)) return 0;
    memcpy(output,fixture,bytes);
    return 1;
}

// ====== rejected_private ======

static void rejected_private(unsigned length)
{
    X86_DMAR_INFO output,saved;
    memset(&output,0xa5,sizeof(output)); saved=output;
    CHECK(!x86_dmar_decode(fixture,length,40,&output) && !memcmp(&saved,&output,sizeof(output)));
}

// ====== file_private ======

static int file_private(const char *path)
{
    FILE *file=NULL;
    X86_DMAR_INFO output;
    if (fopen_s(&file,path,"rb") || !file) return 1;
    size_t length=fread(file_bytes,1,sizeof(file_bytes),file);
    int extra=fgetc(file),bad=ferror(file); int closed=fclose(file);
    if (extra!=EOF || bad || closed || !x86_dmar_decode(file_bytes,length,52,&output)) return 1;
    printf("READY host=%u flags=%u units=%u scopes=%u other=%u\n",output.HostBits,output.Flags,output.Units,output.Scopes,output.OtherRecords);
    for (unsigned index=0; index<output.Units; ++index)
        printf("UNIT base=%llx bytes=%llx segment=%u flags=%u scopes=%u\n",(unsigned long long)output.Unit[index].Base,
            (unsigned long long)output.Unit[index].Bytes,output.Unit[index].Segment,output.Unit[index].Flags,output.Unit[index].ScopeCount);
    return 0;
}

// ====== main ======

int main(int argc,char **argv)
{
    if (argc==2) return file_private(argv[1]);
    for (unsigned cycle=0; cycle<100; ++cycle) {
        X86_DMAR_INFO output,saved;
        prepare_private(); CHECK(x86_dmar_decode(fixture,80,40,&output));
        CHECK(output.HostBits==40 && output.Units==1 && output.Scopes==2 && output.Unit[0].Base==0xfed90000 && output.Unit[0].Bytes==4096);
        CHECK(output.Scope[0].Type==3 && output.Scope[1].Type==4 && output.Scope[0].Path[0]==31);
        CHECK(!x86_dmar_decode(fixture,80,40,(X86_DMAR_INFO *)fixture));
        const unsigned offsets[]={0,8,36,37,38,47,50,52,53,56,64,65,66,67,70,71,73,78};
        const unsigned values[]={0,2,64,8,1,1,0,2,16,1,0,7,1,1,32,8,10,32};
        for (unsigned index=0; index<sizeof(offsets)/sizeof(*offsets); ++index) {
            prepare_private(); fixture[offsets[index]]=(unsigned char)values[index]; checksum_private(80); rejected_private(80);
        }
        prepare_private(); fixture[12]^=1; rejected_private(80);
        prepare_private(); rejected_private(47); rejected_private(79); rejected_private(65537);
        prepare_private(); fixture[64]=1; checksum_private(80); rejected_private(80);
        prepare_private(); word_private(fixture,80,7,2); word_private(fixture,82,4,2); checksum_private(84);
        CHECK(x86_dmar_decode(fixture,84,40,&output) && output.OtherRecords==1);
        for (unsigned units=1; units<=9; ++units) {
            prepare_private(); memset(fixture+48,0,sizeof(fixture)-48);
            for (unsigned index=0; index<units; ++index) {
                unsigned at=48+index*16;
                word_private(fixture,at+2,16,2); fixture[at+4]=1;
                word_private(fixture,at+6,index,2); word_private(fixture,at+8,0x100000+4096*index,8);
            }
            checksum_private(48+units*16);
            CHECK(x86_dmar_decode(fixture,48+units*16,40,&output)==(units<=8));
            if (units==2) {
                word_private(fixture,72,0x100000,8); checksum_private(80); rejected_private(80);
                word_private(fixture,72,0x101000,8); word_private(fixture,70,0,2); checksum_private(80); rejected_private(80);
            }
        }
        for (unsigned scopes=1; scopes<=33; ++scopes) {
            prepare_private(); memset(fixture+64,0,sizeof(fixture)-64);
            word_private(fixture,50,16+scopes*8,2);
            for (unsigned index=0; index<scopes; ++index) {
                unsigned at=64+index*8; fixture[at]=3; fixture[at+1]=8; fixture[at+4]=(unsigned char)index;
            }
            checksum_private(64+scopes*8);
            CHECK(x86_dmar_decode(fixture,64+scopes*8,40,&output)==(scopes<=32));
        }
        prepare_private(); CHECK(!x86_dmar_decode(fixture,80,31,&output));
        CHECK(!x86_dmar_decode(fixture,80,40,(X86_DMAR_INFO *)(scratch+1)));
        KERNEL_ACPI_DIRECTORY directory={0}; directory.Count=1; directory.Entries[0]=0x100000;
        prepare_private(); CHECK(x86_dmar_find(&directory,40,read_private,NULL,scratch,sizeof(scratch),&output)==X86_DMAR_FOUND && output.Physical==0x100000);
        for (fail_read=1; fail_read<=2; ++fail_read) {
            reads=0; saved=output;
            CHECK(!x86_dmar_find(&directory,40,read_private,NULL,scratch,sizeof(scratch),&output) && !memcmp(&output,&saved,sizeof(output)));
        }
        prepare_private(); directory.Count=2; directory.Entries[1]=0x100000; saved=output;
        CHECK(!x86_dmar_find(&directory,40,read_private,NULL,scratch,sizeof(scratch),&output) && !memcmp(&output,&saved,sizeof(output)));
        prepare_private(); directory.Count=1; fixture[0]='X'; saved=output;
        CHECK(x86_dmar_find(&directory,40,read_private,NULL,scratch,sizeof(scratch),&output)==X86_DMAR_ABSENT && !memcmp(&output,&saved,sizeof(output)));
        CHECK(!x86_dmar_find(&directory,40,read_private,NULL,scratch,100,&output));
        CHECK(!x86_dmar_find(&directory,40,read_private,NULL,scratch,sizeof(scratch),(X86_DMAR_INFO *)scratch));
    }
    printf("PASS %u DMAR checks, 100 cycles; reader/directory mocked, no allocation or MMIO\n",checks);
    return 0;
}
