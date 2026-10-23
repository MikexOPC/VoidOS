#include "model.h"

// ====== identify_cases ======

static void identify_cases(void)
{
    EDU_BUS bus = model_bus();

    model_reset();
    CHECK(edu_identify(&bus) && model.Writes == 1 && model.Log[0].Offset == 4 && model.Log[0].Width == 4);

    model_reset(); model.Identity = 0x020000ed; CHECK(!edu_identify(&bus) && !model.Writes);
    model_reset(); model.Invert = 1; CHECK(!edu_identify(&bus));
    model_reset(); model.FailRead = 1; CHECK(!edu_identify(&bus) && !model.Writes);
    model_reset(); model.FailWrite = 1; CHECK(!edu_identify(&bus));
    model_reset(); model.FailRead = 2; CHECK(!edu_identify(&bus));

    EDU_BUS broken = bus;
    broken.Read = 0; CHECK(!edu_identify(&broken));
    broken = bus; broken.Mask = 0xfffff; CHECK(!edu_identify(&broken));
    broken = bus; broken.Hz = 0; CHECK(!edu_identify(&broken));
}

// ====== transfer_cases ======

static void transfer_cases(void)
{
    EDU_BUS bus = model_bus();

    // Programming order and values: source, destination, count (8 bytes), then the command (4 bytes).
    for (BOOT_U32 direction = 0; direction < 2; ++direction) {
        BOOT_U64 source = direction ? EDU_BUFFER + 64 : 0x1001000, target = direction ? 0x1003000 : EDU_BUFFER;

        model_reset();
        CHECK(edu_transfer(&bus, source, target, 64, direction) && model.Writes == 4 && !(model.Command & 1));
        CHECK(model.Log[0].Offset == 0x80 && model.Log[0].Width == 8 && model.Log[0].Value == source);
        CHECK(model.Log[1].Offset == 0x88 && model.Log[1].Width == 8 && model.Log[1].Value == target);
        CHECK(model.Log[2].Offset == 0x90 && model.Log[2].Width == 8 && model.Log[2].Value == 64);
        CHECK(model.Log[3].Offset == 0x98 && model.Log[3].Width == 4 && model.Log[3].Value == (1U | (direction << 1)));
    }

    // Device side bounds of the 4096-byte buffer at 0x40000, RAM side under the 28-bit dma_mask.
    model_reset(); CHECK(edu_transfer(&bus, 0x1000, EDU_BUFFER, 4096, EDU_TO_DEVICE));
    model_reset(); CHECK(edu_transfer(&bus, EDU_BUFFER + 4095, 0x1000, 1, EDU_TO_RAM));
    model_reset(); CHECK(edu_transfer(&bus, (1ULL << 28) - 64, EDU_BUFFER, 64, EDU_TO_DEVICE));

    static const BOOT_U64 refused[][4] = {
        {0x1000, EDU_BUFFER, 0, EDU_TO_DEVICE},
        {0x1000, EDU_BUFFER, 4097, EDU_TO_DEVICE},
        {0x1000, EDU_BUFFER + 1, 4096, EDU_TO_DEVICE},
        {0x1000, EDU_BUFFER - 1, 64, EDU_TO_DEVICE},
        {EDU_BUFFER + 4096, 0x1000, 1, EDU_TO_RAM},
        {(1ULL << 28) - 63, EDU_BUFFER, 64, EDU_TO_DEVICE},
        {1ULL << 28, EDU_BUFFER, 1, EDU_TO_DEVICE},
        {~0ULL, EDU_BUFFER, 64, EDU_TO_DEVICE},
        {EDU_BUFFER, ~0ULL, 64, EDU_TO_RAM},
        {0x1000, EDU_BUFFER, 64, 2},
    };

    for (BOOT_U32 index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        model_reset();
        CHECK(!edu_transfer(&bus, refused[index][0], refused[index][1], (BOOT_U32)refused[index][2], (BOOT_U32)refused[index][3]));
        CHECK(!model.Writes && !model.Reads);
    }

    // A running transfer is never reprogrammed.
    model_reset(); model.Command = 1; model.BusyReads = 0;
    CHECK(!edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && !model.Writes);

    // Every failed register access stops the sequence.
    for (BOOT_U32 fail = 1; fail <= 4; ++fail) {
        model_reset(); model.FailWrite = fail;
        CHECK(!edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && model.Writes == fail);
    }

    model_reset(); model.FailRead = 1; CHECK(!edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && !model.Writes);
    model_reset(); model.FailRead = 2; CHECK(!edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && model.Writes == 4);
}

// ====== deadline_cases ======

static void deadline_cases(void)
{
    EDU_BUS bus = model_bus();

    // Never completes: one second of PM clock ends the wait.
    model_reset(); model.BusyReads = 0; model.Step = 3579;
    CHECK(!edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && (model.Command & 1));
    CHECK(model.Reads >= 990 && model.Reads <= 1002);

    // A clock that stops is refused after 256 identical samples.
    model_reset(); model.BusyReads = 0; model.Stall = 1;
    CHECK(!edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && model.Reads == 256);

    // The 24-bit PM counter wraps: elapsed time is taken modulo the mask.
    model_reset(); model.Ticks = 0xfffff0; model.BusyReads = 40;
    CHECK(edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && !(model.Command & 1));

    // Slow completion inside the deadline.
    model_reset(); model.BusyReads = 900; model.Step = 3579;
    CHECK(edu_transfer(&bus, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE));

    EDU_BUS broken = bus;
    broken.Ticks = 0; model_reset(); CHECK(!edu_transfer(&broken, 0x1000, EDU_BUFFER, 64, EDU_TO_DEVICE) && !model.Writes);
}

// ====== main ======

int main(void)
{
    identify_cases();
    transfer_cases();
    deadline_cases();
    printf("PASS %u edu checks; identity, programming order, buffer and dma_mask bounds, failures and deadlines\n", checks);
    return 0;
}
