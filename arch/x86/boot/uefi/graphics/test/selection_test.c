#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arch/x86/boot/uefi/graphics/gop.h"

typedef struct {
    BOOT_GOP_SERVICES Services;
    EFI_SYSTEM_TABLE Table;
    BOOT_GOP Protocol;
    BOOT_GOP_MODE Mode;
    BOOT_GOP_INFO Modes[3];
    UINT32 Scenario, Allocations, Frees, Outstanding, FreeCalls, SetCalls, LocateCalls;
} MOCK;
static MOCK mock;
static UINT32 checks;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== mock_locate ======

static EFI_STATUS EFIAPI mock_locate(EFI_GUID *guid, void *registration, void **output)
{
    ++mock.LocateCalls;
    CHECK(guid->Data1 == 0x9042a9de && guid->Data2 == 0x23dc && !registration);
    if (mock.Scenario == 1) return EFI_NOT_FOUND;
    *output = mock.Scenario == 2 ? 0 : &mock.Protocol;
    return EFI_SUCCESS;
}

// ====== mock_query ======

static EFI_STATUS EFIAPI mock_query(BOOT_GOP *protocol, UINT32 index, UINTN *size, BOOT_GOP_INFO **info)
{
    CHECK(protocol == &mock.Protocol && index < 3 && !*info && !*size);
    if (mock.Scenario == 3 && index == 1) { *info = (void *)0x1234; return EFI_DEVICE_ERROR; }
    if (mock.Scenario == 4) { *info = 0; *size = 36; return EFI_SUCCESS; }
    if (mock.Scenario == 5) { *info = mock.Mode.Info; *size = 36; return EFI_SUCCESS; }
    *info = malloc(sizeof(**info));
    CHECK(*info);
    **info = mock.Modes[index];
    *size = mock.Scenario == 6 ? 35 : sizeof(**info);
    if (mock.Scenario == 7) (*info)->Format = 3;
    ++mock.Allocations; ++mock.Outstanding;
    return EFI_SUCCESS;
}

// ====== mock_free ======

static EFI_STATUS EFIAPI mock_free(void *buffer)
{
    ++mock.FreeCalls;
    CHECK(buffer && buffer != mock.Mode.Info && mock.Outstanding == 1);
    if (mock.Scenario == 8) return EFI_DEVICE_ERROR;
    free(buffer);
    ++mock.Frees; --mock.Outstanding;
    return EFI_SUCCESS;
}

// ====== mock_set ======

static EFI_STATUS EFIAPI mock_set(BOOT_GOP *protocol, UINT32 index)
{
    CHECK(protocol == &mock.Protocol && index < 3 && !mock.Outstanding);
    ++mock.SetCalls;
    if ((mock.Scenario == 9 || mock.Scenario == 10) && mock.SetCalls == 1) {
        mock.Mode.Index = 2; return EFI_DEVICE_ERROR;
    }
    if (mock.Scenario == 10 && mock.SetCalls == 2) return EFI_DEVICE_ERROR;
    mock.Mode.Index = index; mock.Mode.Info = &mock.Modes[index];
    mock.Mode.Base = 0xe0000000ULL;
    mock.Mode.Capacity = fb_mode_bytes(mock.Mode.Info->Width,mock.Mode.Info->Height,
        mock.Mode.Info->Stride,mock.Mode.Info->Format);
    if (mock.Scenario == 11 && index == 1) mock.Mode.Capacity = 1;
    if (mock.Scenario == 12 && index == 1) mock.Mode.Info = &mock.Modes[2];
    if (mock.Scenario == 13 && index == 1) mock.Mode.Base = ~0ULL;
    return EFI_SUCCESS;
}

// ====== initialize_mock ======

static void initialize_mock(UINT32 scenario)
{
    CHECK(!mock.Outstanding);
    memset(&mock,0,sizeof(mock));
    mock.Scenario = scenario;
    mock.Services.Prefix.Hdr.Signature = 0x56524553544f4f42ULL;
    mock.Services.Prefix.Hdr.HeaderSize = sizeof(mock.Services);
    mock.Services.Locate = mock_locate; mock.Services.Prefix.FreePool = mock_free;
    mock.Table.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    mock.Table.Hdr.HeaderSize = sizeof(mock.Table);
    mock.Table.BootServices = &mock.Services;
    mock.Protocol.Query = mock_query; mock.Protocol.Set = mock_set; mock.Protocol.Mode = &mock.Mode;
    mock.Mode.MaxMode = 3; mock.Mode.Index = 0; mock.Mode.SizeOfInfo = sizeof(BOOT_GOP_INFO);
    mock.Modes[0] = (BOOT_GOP_INFO){0,800,600,3,0,0,0,0,800};
    mock.Modes[1] = (BOOT_GOP_INFO){0,1024,768,1,0,0,0,0,1088};
    mock.Modes[2] = (BOOT_GOP_INFO){0,1280,720,0,0,0,0,0,1280};
    mock.Mode.Info = &mock.Modes[0];
}

// ====== successful_selection_and_reuse ======

static void successful_selection_and_reuse(void)
{
    for (UINT32 cycle = 0; cycle < 10000; ++cycle) {
        BOOT_GOP_OWNER owner = {0};
        initialize_mock(0);
        CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_SUCCESS);
        CHECK(owner.Valid && !owner.Dirty && !owner.Pending && owner.Selected == 1);
        CHECK(owner.Framebuffer.Width == 1024 && owner.Framebuffer.Height == 768 &&
            owner.Framebuffer.Stride == 1088 && owner.Framebuffer.Used == 3342336);
        CHECK(owner.Queries == 3 && owner.Released == 3 && mock.Allocations == mock.Frees);
        CHECK(mock.SetCalls == 1 && !mock.Outstanding);
        CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_INVALID_PARAMETER && mock.SetCalls == 1);
        CHECK(boot_gop_release_pending(&owner) == EFI_SUCCESS);
        CHECK(boot_gop_abandon(&owner) == EFI_SUCCESS && !owner.Valid && !owner.Dirty && !owner.Framebuffer.Base);
        CHECK(mock.SetCalls == 2 && mock.Mode.Index == owner.Original);
        CHECK(boot_gop_abandon(&owner) == EFI_SUCCESS && mock.SetCalls == 2);
    }
}

// ====== failures_and_rollback ======

static void failures_and_rollback(void)
{
    for (UINT32 scenario = 1; scenario <= 13; ++scenario) {
        BOOT_GOP_OWNER owner = {0};
        initialize_mock(scenario);
        CHECK(boot_gop_choose(&mock.Table,32,&owner) != EFI_SUCCESS);
        CHECK(!owner.Valid && !owner.Framebuffer.Base && !owner.Framebuffer.Used);
        if (scenario == 8) {
            CHECK(owner.Pending && mock.Outstanding == 1 && !mock.SetCalls);
            CHECK(boot_gop_abandon(&owner) == EFI_INVALID_PARAMETER && owner.Pending && !mock.SetCalls);
            mock.Scenario = 0;
            CHECK(boot_gop_release_pending(&owner) == EFI_SUCCESS && !owner.Pending);
        }
        if (scenario == 10) CHECK(owner.RestoreFailed && owner.Dirty);
        else CHECK(!owner.RestoreFailed && !owner.Dirty);
        if (scenario >= 9 && scenario != 10) CHECK(mock.SetCalls == 2 && mock.Mode.Index == 0);
        CHECK(!mock.Outstanding && mock.Allocations == mock.Frees);
    }
}

// ====== limits_and_ties ======

static void limits_and_ties(void)
{
    BOOT_GOP_OWNER owner = {0};
    initialize_mock(0); mock.Mode.MaxMode = 257;
    CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_UNSUPPORTED && !mock.Allocations);
    owner = (BOOT_GOP_OWNER){0}; initialize_mock(0); mock.Mode.Index = 3;
    CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_UNSUPPORTED && !mock.Allocations);
    owner = (BOOT_GOP_OWNER){0}; initialize_mock(0); mock.Modes[2] = mock.Modes[1];
    CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_SUCCESS && owner.Selected == 1);
    owner = (BOOT_GOP_OWNER){0}; initialize_mock(0); mock.Services.Prefix.Hdr.HeaderSize = 320;
    CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_UNSUPPORTED && !mock.LocateCalls);
    CHECK(boot_gop_choose(&mock.Table,31,&owner) == EFI_INVALID_PARAMETER && !mock.LocateCalls);
    CHECK(boot_gop_choose(0,32,&owner) == EFI_INVALID_PARAMETER);
    CHECK(boot_gop_choose(&mock.Table,32,0) == EFI_INVALID_PARAMETER);
    CHECK(boot_gop_release_pending(0) == EFI_INVALID_PARAMETER);
    CHECK(boot_gop_abandon(0) == EFI_INVALID_PARAMETER);
}

// ====== abandon_failure_and_retry ======

static void abandon_failure_and_retry(void)
{
    BOOT_GOP_OWNER owner = {0};
    initialize_mock(0);
    CHECK(boot_gop_choose(&mock.Table,32,&owner) == EFI_SUCCESS);
    mock.Scenario = 10;
    CHECK(boot_gop_abandon(&owner) == EFI_DEVICE_ERROR && owner.Valid && owner.Dirty && owner.RestoreFailed);
    CHECK(owner.Framebuffer.Base && mock.Mode.Index == 1);
    CHECK(boot_gop_abandon(&owner) == EFI_SUCCESS && !owner.Valid && !owner.Dirty && !owner.RestoreFailed);
    CHECK(!owner.Framebuffer.Base && mock.Mode.Index == 0);
}

// ====== selection_host_main ======

int main(void)
{
    abandon_failure_and_retry();
    CHECK(fb_pixel_offset(3,2,5,0,40,2,1) == 28);
    CHECK(fb_pixel_offset(3,2,5,0,40,3,1) == UINT64_MAX);
    CHECK(fb_pixel_offset(3,2,5,0,39,0,0) == UINT64_MAX);
    CHECK(fb_pixel_offset(3,2,5,0,40,0,2) == UINT64_MAX);
    CHECK(fb_color_word(0,0x12,0x34,0x56) == 0x00563412);
    CHECK(fb_color_word(1,0x12,0x34,0x56) == 0x00123456);
    CHECK(fb_color_word(2,0x12,0x34,0x56) == UINT32_MAX);
    limits_and_ties();
    failures_and_rollback();
    successful_selection_and_reuse();
    printf("PASS: GOP %u checks,10000 sessions,30000 balanced host allocations,rollback\n",checks);
    return 0;
}
