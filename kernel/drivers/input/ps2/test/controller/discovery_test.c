#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/drivers/input/ps2/controller/controller.h"

typedef struct {
    unsigned char Table[4096];
    uint32_t      Reads, Fail, Mutate;
} FIRMWARE;
static uint32_t checks;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== firmware_read ======

static int firmware_read(void *context, OS_ACPI_U64 address, OS_ACPI_U32 bytes, unsigned char *output)
{
    FIRMWARE *firmware = context;
    if (++firmware->Reads == firmware->Fail || address < 0x2000 ||
        address - 0x2000 > sizeof(firmware->Table) || bytes > sizeof(firmware->Table) - (address - 0x2000)) return 0;
    memcpy(output, firmware->Table + (size_t)(address - 0x2000), bytes);
    if (firmware->Mutate && firmware->Reads == 3) output[0] ^= 1;
    return 1;
}

// ====== fixture ======

static void fixture(FIRMWARE *firmware, KERNEL_ACPI_DIRECTORY *directory, uint32_t length)
{
    memset(firmware, 0, sizeof(*firmware)); memset(directory, 0, sizeof(*directory));
    directory->Physical = 0x1000; directory->Length = 44;
    directory->Count = 1; directory->Width = 8; directory->Entries[0] = 0x2000;
    memcpy(firmware->Table, "FACP", 4);
    firmware->Table[4] = (unsigned char)length; firmware->Table[5] = (unsigned char)(length >> 8);
    firmware->Table[8] = 3; firmware->Table[109] = 3;
}

// ====== checksum ======

static void checksum(FIRMWARE *firmware, uint32_t length)
{
    uint32_t sum = 0;
    firmware->Table[9] = 0;
    for (uint32_t byte = 0; byte < length; ++byte) sum += firmware->Table[byte];
    firmware->Table[9] = (unsigned char)(0U - sum);
}

// ====== valid_and_absent_tables ======

static void valid_and_absent_tables(void)
{
    FIRMWARE firmware;
    KERNEL_ACPI_DIRECTORY directory;
    KERNEL_PS2_DESCRIPTION description;
    for (uint32_t length = 116; length <= 4096; length += 1) {
        fixture(&firmware, &directory, length); checksum(&firmware, length);
        CHECK(kernel_ps2_discover(&directory, 48, firmware_read, &firmware, &description));
        CHECK(description.Physical == 0x2000 && description.Length == length && description.Present == 1);
    }
    fixture(&firmware, &directory, 116);
    firmware.Table[109] = 0; checksum(&firmware, 116);
    CHECK(kernel_ps2_discover(&directory, 48, firmware_read, &firmware, &description) && !description.Present);
    firmware.Table[8] = 1; firmware.Table[109] = 3; checksum(&firmware, 116);
    CHECK(kernel_ps2_discover(&directory, 48, firmware_read, &firmware, &description) && !description.Present);
    firmware.Table[0] = 'X';
    CHECK(kernel_ps2_discover(&directory, 48, firmware_read, &firmware, &description) &&
        !description.Physical && !description.Present);
}

// ====== failures_preserve_output ======

static void failures_preserve_output(void)
{
    FIRMWARE firmware;
    KERNEL_ACPI_DIRECTORY directory;
    KERNEL_PS2_DESCRIPTION description, sentinel;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    for (uint32_t scenario = 0; scenario < 10; ++scenario) {
        fixture(&firmware, &directory, 116); checksum(&firmware, 116); description = sentinel;
        if (scenario == 0) firmware.Table[9] ^= 1;
        if (scenario == 1) firmware.Table[4] = 115;
        if (scenario == 2) { firmware.Table[4] = 1; firmware.Table[5] = 16; }
        if (scenario == 3) { firmware.Table[110] = 1; checksum(&firmware, 116); }
        if (scenario == 4) {
            directory.Count = 2; directory.Length = 52; directory.Entries[1] = 0x2000;
        }
        if (scenario == 5) firmware.Mutate = 1;
        if (scenario >= 6) firmware.Fail = scenario - 5;
        CHECK(!kernel_ps2_discover(&directory, 48, firmware_read, &firmware, &description));
        CHECK(!memcmp(&description, &sentinel, sizeof(description)));
    }
    CHECK(!kernel_ps2_discover(0, 48, firmware_read, &firmware, &description));
    CHECK(!kernel_ps2_discover(&directory, 48, 0, &firmware, &description));
    CHECK(!kernel_ps2_discover(&directory, 48, firmware_read, &firmware, 0));
    CHECK(!kernel_ps2_discover(&directory, 48, firmware_read, &firmware,
        (KERNEL_PS2_DESCRIPTION *)(void *)&directory));
}

// ====== discovery_host_main ======

int main(void)
{
    valid_and_absent_tables(); failures_preserve_output();
    printf("PASS: discovery %u checks, every length 116..4096, output rollback\n", checks);
    return 0;
}
