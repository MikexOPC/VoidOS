#include "mock.h"
void finder_cases(void);

// ====== put_private ======

static void put_private(unsigned char *out,BOOT_U64 value,unsigned bytes)
{ for (unsigned i=0; i<bytes; ++i) out[i]=(unsigned char)(value>>(8*i)); }

// ====== table_private ======

static void table_private(unsigned char *raw,unsigned count)
{
    memset(raw,0,X86_MCFG_BYTES); put_private(raw,0x4746434d,4); put_private(raw+4,44+16*count,4); raw[8]=1;
    for (unsigned i=0; i<count; ++i) {
        put_private(raw+44+i*16,0x100000000ULL+(BOOT_U64)i*0x10000000ULL,8);
        put_private(raw+52+i*16,i%8,2); raw[54+i*16]=(unsigned char)(i/8*16); raw[55+i*16]=(unsigned char)(i/8*16+15);
    }
    raw[9]=0; unsigned sum=0; for (unsigned i=0; i<44+16*count; ++i) sum+=raw[i]; raw[9]=(unsigned char)(0-sum);
}

// ====== main ======

int main(void)
{
    finder_cases();
    unsigned char raw[X86_MCFG_BYTES]; X86_MCFG info,unchanged;
    for (unsigned count=1; count<=32; ++count) {
        table_private(raw,count); CHECK(x86_mcfg_decode(raw,44+16*count,52,&info) && info.Count==count && x86_mcfg_valid(&info));
        unchanged=info; raw[9]^=1; CHECK(!x86_mcfg_decode(raw,44+16*count,52,&info) && !memcmp(&info,&unchanged,sizeof(info)));
    }
    table_private(raw,1);
    for (unsigned size=0; size<X86_MCFG_BYTES; ++size) if (size!=60) CHECK(!x86_mcfg_decode(raw,size,52,&info));
    info=sample(); info.Ranges[0].First=64; info.Ranges[0].Last=127;
    CHECK(x86_mcfg_valid(&info)); CHECK(x86_ecam_page(info.Ranges[0].Base,64,0,0)==0xe4000000);
    info.Count=2; info.Ranges[1]=(X86_ECAM_RANGE){0xe0000000,0,128,255,0}; CHECK(x86_mcfg_valid(&info));
    info.Ranges[1].First=127; CHECK(!x86_mcfg_valid(&info));
    info.Ranges[1]=(X86_ECAM_RANGE){0xe0000000,1,64,127,0}; CHECK(!x86_mcfg_valid(&info));
    fixture(); info=sample(); info.Count=2; info.Ranges[0].First=64; info.Ranges[0].Last=127;
    info.Ranges[1]=(X86_ECAM_RANGE){0xf0000000,1,0,255,0};
    CHECK(x86_ecam_open(&context,&window,0,&inventory,&info,&owner)); BOOT_U32 segment_value=0;
    CHECK(!x86_ecam_read(&owner,0,63,0,0,0,&segment_value));
    CHECK(x86_ecam_read(&owner,0,64,2,3,0,&segment_value) && last_page==0xe4013000);
    CHECK(x86_ecam_read(&owner,1,1,2,3,0,&segment_value) && last_page==0xf0113000);
    CHECK(x86_ecam_close(&owner));
    for (unsigned cycle=0; cycle<100; ++cycle) {
        fixture(); info=sample(); BOOT_U64 budget=pool.FreePages;
        CHECK(x86_ecam_open(&context,&window,0,&inventory,&info,&owner));
        for (unsigned bus=0; bus<256; bus+=17) for (unsigned fn=0; fn<8; ++fn) {
            BOOT_U32 value=0;
            CHECK(x86_ecam_read(&owner,0,bus,31,fn,0,&value) && value==0x10501af4);
            CHECK(last_page==0xe0000000ULL+((BOOT_U64)bus<<20)+(31ULL<<15)+((BOOT_U64)fn<<12));
        }
        for (unsigned offset=0; offset<4097; ++offset) if (offset!=0 && offset!=8 && offset!=12 && offset!=24) {
            BOOT_U32 value=0xaabbccdd; CHECK(!x86_ecam_read(&owner,0,0,0,0,offset,&value) && value==0xaabbccdd);
        }
        CHECK(!x86_ecam_read(&owner,1,0,0,0,0,&owner.Ready));
        CHECK(!x86_ecam_read(&owner,0,0,0,0,0,&owner.Ready));
        fail_mode=3; BOOT_U32 value=7; CHECK(!x86_ecam_read(&owner,0,0,0,0,0,&value) && value==7);
        fail_mode=0; CHECK(x86_ecam_close(&owner) && !owner.Self && pool.FreePages==budget && !pool.Live[0].Id);
    }
    for (unsigned fault=1; fault<=2; ++fault) {
        fixture(); info=sample(); fail_mode=fault; CHECK(!x86_ecam_open(&context,&window,0,&inventory,&info,&owner) && !owner.Self);
    }
    fixture(); info=sample(); info.Ranges[0].Base=(BOOT_U64)pool.Free[0].Base&~0xfffffULL;
    CHECK(!x86_ecam_open(&context,&window,0,&inventory,&info,&owner));
    fixture(); info=sample(); leaves[0]=0xe0000001; CHECK(!x86_ecam_open(&context,&window,0,&inventory,&info,&owner));
    fixture(); info=sample(); CHECK(x86_ecam_open(&context,&window,0,&inventory,&info,&owner));
    fail_mode=4; BOOT_U32 value=7;
    CHECK(!x86_ecam_read(&owner,0,0,0,0,0,&value) && value==7 && owner.Ready==2 && !x86_ecam_close(&owner));
    restore_borrow(); CHECK(x86_ecam_close(&owner) && !owner.Self);
    fixture(); info=sample(); CHECK(x86_ecam_open(&context,&window,0,&inventory,&info,&owner)); fail_mode=5;
    CHECK(!x86_ecam_read(&owner,0,0,0,0,0,&value) && value==7 && !x86_ecam_close(&owner));
    --pool.FreePages; fail_mode=0; CHECK(x86_ecam_close(&owner));
    fixture(); info=sample(); CHECK(x86_ecam_open(&context,&window,0,&inventory,&info,&owner)); fail_mode=6;
    CHECK(!x86_ecam_read(&owner,0,0,0,0,0,&value) && value==7); fail_mode=0; CHECK(x86_ecam_close(&owner));
    printf("PASS %u ECAM checks; 100 sessions, native load backend, malformed tables, immutable failures, alias rejection and failed-revoke retention\n",checks);
    return 0;
}
