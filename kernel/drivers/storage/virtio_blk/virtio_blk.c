#include "virtio_blk.h"

// OASIS VirtIO 1.2 §4.1.4.3: virtio_pci_common_cfg register offsets.
#define COMMON_DFSELECT         0x00U
#define COMMON_DF               0x04U
#define COMMON_GFSELECT         0x08U
#define COMMON_GF               0x0CU
#define COMMON_MSIX_CONFIG      0x10U
#define COMMON_NUMQ             0x12U
#define COMMON_STATUS           0x14U
#define COMMON_CFGGEN           0x15U
#define COMMON_Q_SELECT         0x16U
#define COMMON_Q_SIZE           0x18U
#define COMMON_Q_MSIX_VECTOR    0x1AU
#define COMMON_Q_ENABLE         0x1CU
#define COMMON_Q_NOTIFY_OFF     0x1EU
#define COMMON_Q_DESCLO         0x20U
#define COMMON_Q_DESCHI         0x24U
#define COMMON_Q_AVAILLO        0x28U
#define COMMON_Q_AVAILHI        0x2CU
#define COMMON_Q_USEDLO         0x30U
#define COMMON_Q_USEDHI         0x34U

// OASIS VirtIO 1.2 §5.2.4: virtio_blk_config register offsets.
#define BLK_CFG_CAPACITY_LO     0x00U
#define BLK_CFG_CAPACITY_HI     0x04U
#define BLK_CFG_BLK_SIZE        0x14U

// ====== zero_bytes ======

static void zero_bytes(void *pointer, size_t bytes)
{
    uint8_t *target = (uint8_t *)pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = 0;
}

// ====== copy_bytes ======

static void copy_bytes(void *target_pointer, const void *source_pointer, size_t bytes)
{
    uint8_t *target = (uint8_t *)target_pointer;
    const uint8_t *source = (const uint8_t *)source_pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = source[index];
}

// ====== mmio_read_private ======

static int mmio_read_private(
    const VIRTIO_BLK_TRANSPORT *transport,
    uint32_t region,
    uint32_t offset,
    uint32_t width,
    uint32_t *value
)
{
    *value = 0;
    return transport->Mmio(transport->Context, region, offset, width, 0, value);
}

// ====== mmio_write_private ======

static int mmio_write_private(
    const VIRTIO_BLK_TRANSPORT *transport,
    uint32_t region,
    uint32_t offset,
    uint32_t width,
    uint32_t value
)
{
    uint32_t temp = value;
    return transport->Mmio(transport->Context, region, offset, width, 1, &temp);
}

// ====== wait_reset_private ======

// OASIS VirtIO 1.2 §4.1.4.3.1: write 0 to device_status and wait until read returns 0.
static int wait_reset_private(const VIRTIO_BLK_TRANSPORT *transport)
{
    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, 0))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t start = 0;
    if (!transport->Clock(transport->Context, &start))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t elapsed = 0;
    uint32_t last = start;
    for (uint32_t poll = 0; poll < transport->MaxPolls; ++poll) {
        uint32_t status = 0xFFU;
        if (!mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, &status))
            return KERNEL_STORAGE_ERR_IO;
        if ((status & 0xFFU) == 0)
            return KERNEL_STORAGE_OK;

        uint32_t now = 0;
        if (!transport->Clock(transport->Context, &now))
            return KERNEL_STORAGE_ERR_IO;
        uint32_t delta = (now - last) & transport->ClockMask;
        last = now;
        elapsed += delta;
        if (elapsed >= transport->TimeoutTicks)
            return KERNEL_STORAGE_ERR_TIMEOUT;
    }
    return KERNEL_STORAGE_ERR_TIMEOUT;
}

// ====== read_device_config_private ======

// OASIS VirtIO 1.2 §2.5.4: repeat config reads until config_generation is stable.
static int read_device_config_private(
    const VIRTIO_BLK_TRANSPORT *transport,
    uint32_t has_blk_size,
    uint64_t *capacity_out,
    uint32_t *blk_size_out
)
{
    for (uint32_t attempt = 0; attempt < 16U; ++attempt) {
        uint32_t gen_before = 0, gen_after = 0;
        uint32_t cap_lo = 0, cap_hi = 0, blk_sz = 512U;

        if (!mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_CFGGEN, 1, &gen_before) ||
            !mmio_read_private(transport, VIRTIO_REGION_DEVICE, BLK_CFG_CAPACITY_LO, 4, &cap_lo) ||
            !mmio_read_private(transport, VIRTIO_REGION_DEVICE, BLK_CFG_CAPACITY_HI, 4, &cap_hi))
            return KERNEL_STORAGE_ERR_IO;

        if (has_blk_size &&
            !mmio_read_private(transport, VIRTIO_REGION_DEVICE, BLK_CFG_BLK_SIZE, 4, &blk_sz))
            return KERNEL_STORAGE_ERR_IO;

        if (!mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_CFGGEN, 1, &gen_after))
            return KERNEL_STORAGE_ERR_IO;

        if ((gen_before & 0xFFU) == (gen_after & 0xFFU)) {
            uint64_t cap = ((uint64_t)cap_hi << 32) | cap_lo;
            if (!kvblk_config_validate(cap, blk_sz, has_blk_size))
                return KERNEL_STORAGE_ERR_CORRUPT;
            *capacity_out = cap;
            *blk_size_out = has_blk_size ? blk_sz : 512U;
            return KERNEL_STORAGE_OK;
        }
    }
    return KERNEL_STORAGE_ERR_TIMEOUT;
}

// ====== block_read_adapter ======

static int block_read_adapter(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, void *buffer)
{
    KERNEL_VIRTIO_BLK *driver = (KERNEL_VIRTIO_BLK *)device->Context;
    return kernel_virtio_blk_request(driver, VIRTIO_BLK_T_IN, lba, count, buffer);
}

// ====== block_write_adapter ======

static int block_write_adapter(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, const void *buffer)
{
    KERNEL_VIRTIO_BLK *driver = (KERNEL_VIRTIO_BLK *)device->Context;
    return kernel_virtio_blk_request(driver, VIRTIO_BLK_T_OUT, lba, count, (void *)buffer);
}

// ====== block_flush_adapter ======

static int block_flush_adapter(KERNEL_BLOCK_DEVICE *device)
{
    KERNEL_VIRTIO_BLK *driver = (KERNEL_VIRTIO_BLK *)device->Context;
    if (!driver || driver->Self != (uintptr_t)driver || !driver->Ready)
        return KERNEL_STORAGE_ERR_ARG;
    if (!driver->FlushSupported)
        return KERNEL_STORAGE_OK;
    return kernel_virtio_blk_request(driver, VIRTIO_BLK_T_FLUSH, 0, 0, NULL);
}

// ====== kernel_virtio_blk_init ======

int kernel_virtio_blk_init(
    KERNEL_VIRTIO_BLK *driver,
    const VIRTIO_BLK_TRANSPORT *transport,
    void *control_page,
    uint64_t control_phys,
    void *data_page,
    uint64_t data_phys
)
{
    if (!driver || !transport || !transport->Mmio || !transport->Clock || !transport->Barrier ||
        !control_page || !data_page || (control_phys & 4095ULL) || (data_phys & 4095ULL) ||
        control_phys == data_phys)
        return KERNEL_STORAGE_ERR_ARG;

    zero_bytes(driver, sizeof(*driver));
    zero_bytes(control_page, 4096U);
    zero_bytes(data_page, 4096U);

    int status = wait_reset_private(transport);
    if (status != KERNEL_STORAGE_OK)
        return status;

    uint32_t dev_status = VIRTIO_STATUS_ACKNOWLEDGE;
    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, dev_status))
        return KERNEL_STORAGE_ERR_IO;

    dev_status |= VIRTIO_STATUS_DRIVER;
    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, dev_status))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t feat_low = 0, feat_high = 0;
    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_DFSELECT, 4, 0) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_DF, 4, &feat_low) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_DFSELECT, 4, 1) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_DF, 4, &feat_high))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t drv_low = 0, drv_high = 0;
    if (!kvblk_features_select(feat_low, feat_high, &drv_low, &drv_high)) {
        (void)mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, VIRTIO_STATUS_FAILED);
        return KERNEL_STORAGE_ERR_CORRUPT;
    }

    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_GFSELECT, 4, 0) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_GF, 4, drv_low) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_GFSELECT, 4, 1) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_GF, 4, drv_high))
        return KERNEL_STORAGE_ERR_IO;

    dev_status |= VIRTIO_STATUS_FEATURES_OK;
    uint32_t readback = 0;
    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, dev_status) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, &readback) ||
        !(readback & VIRTIO_STATUS_FEATURES_OK)) {
        (void)mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, VIRTIO_STATUS_FAILED);
        return KERNEL_STORAGE_ERR_CORRUPT;
    }

    uint32_t num_queues = 0, max_qsize = 0, notify_off = 0, q_enabled = 0;
    if (!mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_NUMQ, 2, &num_queues) ||
        num_queues < 1U ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_SELECT, 2, 0) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_SIZE, 2, &max_qsize))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t qsize = kvblk_queue_size_choose(max_qsize);
    if (!qsize)
        return KERNEL_STORAGE_ERR_CORRUPT;

    uint64_t desc_phys = control_phys + offsetof(VIRTIO_BLK_DMA_CONTROL, Descriptors);
    uint64_t avail_phys = control_phys + offsetof(VIRTIO_BLK_DMA_CONTROL, AvailFlags);
    uint64_t used_phys = control_phys + offsetof(VIRTIO_BLK_DMA_CONTROL, UsedFlags);

    VIRTIO_BLK_DMA_CONTROL *ctrl = (VIRTIO_BLK_DMA_CONTROL *)control_page;
    ctrl->AvailFlags = transport->IrqEnabled ? 0U : VIRTQ_AVAIL_F_NO_INTERRUPT;

    if (transport->IrqEnabled) {
        uint32_t msix_vec = 0xFFFFU;
        // VirtIO 1.2 §4.1.4.3: map queue 0 to MSI-X table entry 0 and read back to confirm != 0xFFFF.
        if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_MSIX_CONFIG, 2, 0xFFFFU) ||
            !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_MSIX_VECTOR, 2, 0U) ||
            !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_MSIX_VECTOR, 2, &msix_vec) ||
            (msix_vec & 0xFFFFU) != 0U)
            return KERNEL_STORAGE_ERR_IO;
    }

    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_SIZE, 2, qsize) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_NOTIFY_OFF, 2, &notify_off) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_DESCLO, 4, (uint32_t)desc_phys) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_DESCHI, 4, (uint32_t)(desc_phys >> 32)) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_AVAILLO, 4, (uint32_t)avail_phys) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_AVAILHI, 4, (uint32_t)(avail_phys >> 32)) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_USEDLO, 4, (uint32_t)used_phys) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_USEDHI, 4, (uint32_t)(used_phys >> 32)) ||
        !mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_ENABLE, 2, 1U) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_Q_ENABLE, 2, &q_enabled) ||
        q_enabled != 1U)
        return KERNEL_STORAGE_ERR_IO;

    dev_status |= VIRTIO_STATUS_DRIVER_OK;
    if (!mmio_write_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, dev_status) ||
        !mmio_read_private(transport, VIRTIO_REGION_COMMON, COMMON_STATUS, 1, &readback) ||
        (readback & 0xFFU) != dev_status)
        return KERNEL_STORAGE_ERR_IO;

    uint64_t capacity = 0;
    uint32_t blk_size = 512U;
    status = read_device_config_private(
        transport,
        (drv_low & VIRTIO_BLK_F_BLK_SIZE) != 0,
        &capacity,
        &blk_size
    );
    if (status != KERNEL_STORAGE_OK)
        return status;

    driver->Transport = *transport;
    driver->Control = ctrl;
    driver->DataBuffer = (uint8_t *)data_page;
    driver->ControlPhys = control_phys;
    driver->DataPhys = data_phys;
    driver->CapacitySectors = capacity;
    driver->SectorBytes = 512U;
    driver->FeaturesLow = drv_low;
    driver->FeaturesHigh = drv_high;
    driver->QueueSize = (uint16_t)qsize;
    driver->NotifyOff = (uint16_t)notify_off;
    driver->ReadOnly = (drv_low & VIRTIO_BLK_F_RO) ? 1U : 0U;
    driver->FlushSupported = (drv_low & VIRTIO_BLK_F_FLUSH) ? 1U : 0U;
    driver->Ready = 1U;
    driver->Self = (uintptr_t)driver;

    driver->BlockDevice.Self = (uintptr_t)&driver->BlockDevice;
    driver->BlockDevice.Name = "vblk0";
    driver->BlockDevice.Context = driver;
    driver->BlockDevice.TotalSectors = capacity;
    driver->BlockDevice.SectorBytes = 512U;
    driver->BlockDevice.MaxTransferSectors = VIRTIO_BLK_MAX_SECTORS;
    driver->BlockDevice.Kind = KERNEL_BLOCK_KIND_VIRTIO_BLK;
    driver->BlockDevice.ReadOnly = driver->ReadOnly;
    driver->BlockDevice.Ready = 1U;
    driver->BlockDevice.Read = block_read_adapter;
    driver->BlockDevice.Write = block_write_adapter;
    driver->BlockDevice.Flush = block_flush_adapter;
    return KERNEL_STORAGE_OK;
}

// ====== kernel_virtio_blk_request ======

int kernel_virtio_blk_request(
    KERNEL_VIRTIO_BLK *driver,
    uint32_t type,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
)
{
    if (!driver || driver->Self != (uintptr_t)driver || !driver->Ready)
        return KERNEL_STORAGE_ERR_ARG;

    if (type == VIRTIO_BLK_T_FLUSH) {
        if (lba != 0 || sector_count != 0)
            return KERNEL_STORAGE_ERR_ARG;
    } else if (type == VIRTIO_BLK_T_IN || type == VIRTIO_BLK_T_OUT) {
        if (!buffer || !sector_count || sector_count > VIRTIO_BLK_MAX_SECTORS)
            return KERNEL_STORAGE_ERR_ARG;
        if (lba >= driver->CapacitySectors || sector_count > driver->CapacitySectors - lba)
            return KERNEL_STORAGE_ERR_RANGE;
        if (type == VIRTIO_BLK_T_OUT && driver->ReadOnly)
            return KERNEL_STORAGE_ERR_READ_ONLY;
    } else {
        return KERNEL_STORAGE_ERR_ARG;
    }

    VIRTIO_BLK_DMA_CONTROL *ctrl = driver->Control;
    size_t transfer_bytes = (size_t)sector_count * 512U;

    if (type == VIRTIO_BLK_T_OUT)
        copy_bytes(driver->DataBuffer, buffer, transfer_bytes);
    else if (type == VIRTIO_BLK_T_IN)
        zero_bytes(driver->DataBuffer, transfer_bytes);

    ctrl->Header.Type = type;
    ctrl->Header.Reserved = 0;
    ctrl->Header.Sector = lba;
    ctrl->Status = 0xFFU;

    uint64_t header_phys = driver->ControlPhys + offsetof(VIRTIO_BLK_DMA_CONTROL, Header);
    uint64_t status_phys = driver->ControlPhys + offsetof(VIRTIO_BLK_DMA_CONTROL, Status);

    ctrl->Descriptors[0].Address = header_phys;
    ctrl->Descriptors[0].Length = sizeof(VIRTIO_BLK_REQ_HEADER);
    ctrl->Descriptors[0].Flags = VIRTQ_DESC_F_NEXT;
    ctrl->Descriptors[0].Next = 1U;

    if (type == VIRTIO_BLK_T_FLUSH) {
        ctrl->Descriptors[1].Address = status_phys;
        ctrl->Descriptors[1].Length = 1U;
        ctrl->Descriptors[1].Flags = VIRTQ_DESC_F_WRITE;
        ctrl->Descriptors[1].Next = 0U;
    } else {
        ctrl->Descriptors[1].Address = driver->DataPhys;
        ctrl->Descriptors[1].Length = (uint32_t)transfer_bytes;
        ctrl->Descriptors[1].Flags = (uint16_t)(VIRTQ_DESC_F_NEXT |
            (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0U));
        ctrl->Descriptors[1].Next = 2U;

        ctrl->Descriptors[2].Address = status_phys;
        ctrl->Descriptors[2].Length = 1U;
        ctrl->Descriptors[2].Flags = VIRTQ_DESC_F_WRITE;
        ctrl->Descriptors[2].Next = 0U;
    }

    uint16_t avail_slot = driver->AvailIdx % driver->QueueSize;
    ctrl->AvailRing[avail_slot] = 0U;
    driver->Transport.Barrier(driver->Transport.Context);
    driver->AvailIdx = (uint16_t)(driver->AvailIdx + 1U);
    ctrl->AvailIdx = driver->AvailIdx;
    driver->Transport.Barrier(driver->Transport.Context);

    uint64_t irq_seq = (driver->Transport.IrqEnabled && driver->Transport.IrqSeq)
        ? driver->Transport.IrqSeq(driver->Transport.Context)
        : 0ULL;

    if (!mmio_write_private(&driver->Transport, VIRTIO_REGION_NOTIFY, driver->NotifyOff, 2, 0U))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t start = 0;
    if (!driver->Transport.Clock(driver->Transport.Context, &start))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t elapsed = 0;
    uint32_t last = start;
    uint16_t expected_used = (uint16_t)(driver->LastUsedIdx + 1U);

    for (uint32_t poll = 0; poll < driver->Transport.MaxPolls; ++poll) {
        driver->Transport.Barrier(driver->Transport.Context);
        uint16_t used_idx = *(volatile uint16_t *)&ctrl->UsedIdx;
        if (used_idx == expected_used) {
            uint16_t used_slot = driver->LastUsedIdx % driver->QueueSize;
            uint32_t head_id = ctrl->UsedRing[used_slot].Id;
            driver->LastUsedIdx = expected_used;
            if (head_id != 0U || ctrl->Status != VIRTIO_BLK_S_OK)
                return KERNEL_STORAGE_ERR_IO;
            if (type == VIRTIO_BLK_T_IN)
                copy_bytes(buffer, driver->DataBuffer, transfer_bytes);
            return KERNEL_STORAGE_OK;
        }

        if (driver->Transport.IrqEnabled && driver->Transport.Wait) {
            driver->Transport.Wait(driver->Transport.Context, irq_seq);
            if (driver->Transport.IrqSeq)
                irq_seq = driver->Transport.IrqSeq(driver->Transport.Context);
        }

        uint32_t now = 0;
        if (!driver->Transport.Clock(driver->Transport.Context, &now))
            return KERNEL_STORAGE_ERR_IO;
        uint32_t delta = (now - last) & driver->Transport.ClockMask;
        last = now;
        elapsed += delta;
        if (elapsed >= driver->Transport.TimeoutTicks)
            return KERNEL_STORAGE_ERR_TIMEOUT;
    }

    return KERNEL_STORAGE_ERR_TIMEOUT;
}
