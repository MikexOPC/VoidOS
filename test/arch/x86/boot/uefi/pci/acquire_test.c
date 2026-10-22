#include "arch/x86/boot/uefi/pci/acquire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BOOT_PCI_SERVICES services;
static EFI_SYSTEM_TABLE table;
static BOOT_PCI_IO pci;
static EFI_HANDLE handles[2];
static unsigned char config[256], descriptor[48];
static UINT32 checks, live, mode, reads, freed, bar_calls;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#value); exit(1); } } while (0)

// ====== put_private ======

static void put_private(unsigned char *buffer, UINT32 at, UINT64 value, UINT32 bytes)
{
    for (UINT32 byte = 0; byte < bytes; ++byte) buffer[at+byte] = (unsigned char)(value >> (byte*8));
}

// ====== free_private ======

static EFI_STATUS EFIAPI free_private(void *buffer)
{
    CHECK(buffer == handles || buffer == descriptor);
    if (mode == 7 || (mode == 8 && buffer == handles)) return EFI_DEVICE_ERROR;
    CHECK(live); --live; ++freed;
    return EFI_SUCCESS;
}

// ====== locate_private ======

static EFI_STATUS EFIAPI locate_private(UINT32 search, EFI_GUID *guid, void *key, UINTN *count, EFI_HANDLE **out)
{
    CHECK(search == 2 && guid->Data1 == 0x4cf5b200 && !key);
    if (mode == 1) return EFI_NOT_FOUND;
    *count = mode == 2 ? 257 : mode == 3 ? 2 : 1;
    *out = handles; ++live;
    return EFI_SUCCESS;
}

// ====== handle_private ======

static EFI_STATUS EFIAPI handle_private(EFI_HANDLE handle, EFI_GUID *guid, void **out)
{
    CHECK(handle == &pci && guid->Data1 == 0x4cf5b200);
    *out = &pci;
    return mode == 4 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

// ====== read_private ======

static EFI_STATUS EFIAPI read_private(BOOT_PCI_IO *self, UINT32 width, UINT32 offset, UINTN count, void *out)
{
    CHECK(self == &pci && !width && offset<=256 && count<=256-offset);
    memcpy(out,config+offset,(size_t)count);
    if (!offset && count==64) ++reads;
    if (mode == 5) return EFI_DEVICE_ERROR;
    if (mode == 6 && reads == 2 && !offset && count==64) ((unsigned char *)out)[8] ^= 1;
    return EFI_SUCCESS;
}

// ====== location_private ======

static EFI_STATUS EFIAPI location_private(BOOT_PCI_IO *self, UINTN *segment, UINTN *bus, UINTN *device, UINTN *function)
{
    CHECK(self == &pci);
    *segment = mode == 9 ? 1 : 0; *bus = 0; *device = 1; *function = 0;
    return EFI_SUCCESS;
}

// ====== bar_private ======

static EFI_STATUS EFIAPI bar_private(BOOT_PCI_IO *self, UINT8 bar, UINT64 *supports, void **out)
{
    CHECK(self == &pci && (bar == 4 || (mode == 11 && bar == 0)) && !supports);
    ++bar_calls;
    if (bar == 0) return EFI_UNSUPPORTED;
    if (mode == 10) { *out = (void *)(UINTN)1; return EFI_UNSUPPORTED; }
    *out = descriptor; ++live;
    return EFI_SUCCESS;
}

// ====== fixture_private ======

static void fixture_private(UINT32 selected)
{
    const UINT32 positions[4] = {0x40,0x50,0x64,0x74};
    const UINT32 lengths[4] = {56,4096,1,16};
    CHECK(!live);
    mode = selected; reads = freed = bar_calls = 0;
    memset(&services,0,sizeof(services)); memset(&table,0,sizeof(table)); memset(&pci,0,sizeof(pci));
    memset(config,0,sizeof(config)); memset(descriptor,0,sizeof(descriptor));
    services.Prefix.Hdr.HeaderSize = sizeof(services); services.Prefix.HandleProtocol = handle_private;
    services.Prefix.FreePool = free_private; services.Locate = locate_private;
    table.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE; table.Hdr.HeaderSize = sizeof(table); table.BootServices = &services;
    pci.Read = read_private; pci.Location = location_private; pci.Bar = bar_private;
    handles[0] = handles[1] = &pci;
    put_private(config,0,0x10501af4,4); config[6]=16; config[11]=3; config[0x34]=0x40;
    put_private(config,0x20,4,4); put_private(config,0x24,1,4);
    if (mode == 11) put_private(config,0x10,0x80000008,4);
    for (UINT32 index = 0; index < 4; ++index) {
        UINT32 p = positions[index];
        config[p]=9; config[p+1]=(unsigned char)(index==3?0:positions[index+1]);
        config[p+2]=(unsigned char)(index==1?20:16); config[p+3]=(unsigned char)(index+1); config[p+4]=4;
        put_private(config,p+8,index*4096,4); put_private(config,p+12,lengths[index],4);
    }
    put_private(config,0x60,4,4);
    descriptor[0]=0x8a; descriptor[1]=43; descriptor[46]=0x79;
    put_private(descriptor,6,64,8); put_private(descriptor,14,UINT64_C(0x100000000),8);
    put_private(descriptor,38,16384,8);
}

// ====== main ======

int main(void)
{
    for (UINT32 selected = 0; selected <= 11; ++selected) {
        BOOT_PCI_OWNER owner = {0};
        EFI_STATUS status;
        fixture_private(selected);
        status = boot_vgpu_acquire(&table,48,&owner);
        CHECK((status == EFI_SUCCESS) == (selected == 0 || selected == 11));
        CHECK(owner.Valid == (UINT32)(selected == 0 || selected == 11));
        if (selected == 11) CHECK(owner.UnavailableBars == 1 && owner.Result.Sizes[0] == 0);
        CHECK(kernel_vgpu_boot_valid(&owner.Result,48));
        if (selected == 7 || selected == 8) {
            CHECK(live && (owner.Resources || owner.Handles));
            mode = 0; CHECK(boot_vgpu_release(&owner) == EFI_SUCCESS);
        }
        CHECK(!live && !owner.Handles && !owner.Resources);
        CHECK(boot_vgpu_acquire(&table,48,&owner) == EFI_INVALID_PARAMETER);
        if (!selected) {
            CHECK(owner.Result.Device == 1 && owner.Result.Sizes[4] == 16384 && reads == 2);
            CHECK(freed == 2 && bar_calls == 1);
            {
                KERNEL_VGPU_BOOT changed = owner.Result;
                changed.Bus=255; changed.Device=31; changed.Function=7;
                CHECK(kernel_vgpu_boot_valid(&changed,48));
                changed.Bus=256; CHECK(!kernel_vgpu_boot_valid(&changed,48));
                changed=owner.Result; changed.Segment=1; CHECK(!kernel_vgpu_boot_valid(&changed,48));
                changed=owner.Result; changed.Reserved=1; CHECK(!kernel_vgpu_boot_valid(&changed,48));
                changed=owner.Result; changed.Present=2; CHECK(!kernel_vgpu_boot_valid(&changed,48));
                changed=owner.Result; changed.Sizes[4]=0; CHECK(!kernel_vgpu_boot_valid(&changed,48));
                changed=owner.Result; changed.Config[0x41]=0x40; CHECK(!kernel_vgpu_boot_valid(&changed,48));
            }
            for (UINT32 byte = 0; byte < sizeof(owner.Result); ++byte) {
                KERNEL_VGPU_BOOT changed = owner.Result;
                ((unsigned char *)&changed)[byte] ^= 255;
                CHECK(kernel_vgpu_boot_valid(&changed,48) == 0 || changed.Present == 1);
            }
        }
    }
    fixture_private(0);
    for (UINT32 byte = 0; byte < 48; ++byte) for (UINT32 value = 0; value < 256; ++value) {
        UINT64 size = 0xa5a5;
        unsigned char saved = descriptor[byte];
        descriptor[byte] = (unsigned char)value;
        if (boot_vgpu_descriptor(descriptor,48,4,1,48,&size)) CHECK(size >= 16 && !(size & (size-1)));
        else CHECK(size == 0xa5a5);
        descriptor[byte] = saved;
    }
    for (UINTN capacity = 0; capacity < 48; ++capacity) {
        UINT64 size = 0;
        CHECK(!boot_vgpu_descriptor(descriptor,capacity,4,1,48,&size));
    }
    {
        UINT64 size = 99;
        CHECK(!boot_vgpu_descriptor(NULL,48,4,1,48,&size) && size == 99);
        CHECK(!boot_vgpu_descriptor(descriptor,48,4,1,48,NULL));
        CHECK(!boot_vgpu_descriptor(descriptor,4097,4,1,48,&size));
        put_private(descriptor,6,32,8); put_private(descriptor,14,0x80000000,8);
        CHECK(boot_vgpu_descriptor(descriptor,48,0x80000008,0,32,&size) && size == 16384);
        put_private(descriptor,30,1,8);
        CHECK(!boot_vgpu_descriptor(descriptor,48,0x80000008,0,32,&size));
    }
    printf("PASS: UEFI PCI acquisition %u checks; allocations balanced; cleanup failure retry verified\n",checks);
    return 0;
}
