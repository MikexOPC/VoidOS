#include "ahci.h"

// Intel Serial ATA AHCI 1.3.1 §3.1: Generic Host Control register offsets.
#define HBA_CAP             0x00U
#define HBA_GHC             0x04U
#define HBA_IS              0x08U
#define HBA_PI              0x0CU
#define HBA_VS              0x10U

// Intel Serial ATA AHCI 1.3.1 §3.3: Port register offsets from port base (0x100 + port * 0x80).
#define PORT_BASE(p)        (0x100U + (p) * 0x80U)
#define PX_CLB              0x00U
#define PX_CLBU             0x04U
#define PX_FB               0x08U
#define PX_FBU              0x0CU
#define PX_IS               0x10U
#define PX_IE               0x14U
#define PX_CMD              0x18U
#define PX_TFD              0x20U
#define PX_SIG              0x24U
#define PX_SSTS             0x28U
#define PX_SERR             0x30U
#define PX_CI               0x38U

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

// ====== mmio_read32 ======

static int mmio_read32(const AHCI_TRANSPORT *transport, uint32_t offset, uint32_t *value)
{
    *value = 0;
    return transport->Mmio(transport->Context, offset, 0, value);
}

// ====== mmio_write32 ======

static int mmio_write32(const AHCI_TRANSPORT *transport, uint32_t offset, uint32_t value)
{
    uint32_t temp = value;
    return transport->Mmio(transport->Context, offset, 1, &temp);
}

// ====== wait_mask_clear_private ======

static int wait_mask_clear_private(
    const AHCI_TRANSPORT *transport,
    uint32_t reg_offset,
    uint32_t mask
)
{
    uint32_t start = 0;
    if (!transport->Clock(transport->Context, &start))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t elapsed = 0;
    uint32_t last = start;

    for (uint32_t poll = 0; poll < transport->MaxPolls; ++poll) {
        uint32_t val = 0;
        if (!mmio_read32(transport, reg_offset, &val))
            return KERNEL_STORAGE_ERR_IO;
        if ((val & mask) == 0U)
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

// ====== stop_port_private ======

// Intel AHCI 1.3.1 §10.1.2: clear PxCMD.ST, wait PxCMD.CR == 0; clear PxCMD.FRE, wait PxCMD.FR == 0.
static int stop_port_private(const AHCI_TRANSPORT *transport, uint32_t port)
{
    uint32_t base = PORT_BASE(port);
    uint32_t cmd = 0;
    if (!mmio_read32(transport, base + PX_CMD, &cmd))
        return KERNEL_STORAGE_ERR_IO;

    if (cmd & AHCI_PXCMD_ST) {
        cmd &= ~AHCI_PXCMD_ST;
        if (!mmio_write32(transport, base + PX_CMD, cmd))
            return KERNEL_STORAGE_ERR_IO;
    }
    int status = wait_mask_clear_private(transport, base + PX_CMD, AHCI_PXCMD_CR);
    if (status != KERNEL_STORAGE_OK)
        return status;

    if (!mmio_read32(transport, base + PX_CMD, &cmd))
        return KERNEL_STORAGE_ERR_IO;
    if (cmd & AHCI_PXCMD_FRE) {
        cmd &= ~AHCI_PXCMD_FRE;
        if (!mmio_write32(transport, base + PX_CMD, cmd))
            return KERNEL_STORAGE_ERR_IO;
    }
    return wait_mask_clear_private(transport, base + PX_CMD, AHCI_PXCMD_FR);
}

// ====== start_port_private ======

// Intel AHCI 1.3.1 §10.1.2: program CLB/FB, enable FRE, clear SERR/IS, wait BSY|DRQ clear, set ST.
static int start_port_private(
    const AHCI_TRANSPORT *transport,
    uint32_t port,
    uint64_t clb_phys,
    uint64_t fb_phys
)
{
    uint32_t base = PORT_BASE(port);
    int status = stop_port_private(transport, port);
    if (status != KERNEL_STORAGE_OK)
        return status;

    if (!mmio_write32(transport, base + PX_IE, 0U) ||
        !mmio_write32(transport, base + PX_CLB, (uint32_t)clb_phys) ||
        !mmio_write32(transport, base + PX_CLBU, (uint32_t)(clb_phys >> 32)) ||
        !mmio_write32(transport, base + PX_FB, (uint32_t)fb_phys) ||
        !mmio_write32(transport, base + PX_FBU, (uint32_t)(fb_phys >> 32)) ||
        !mmio_write32(transport, base + PX_SERR, 0xFFFFFFFFU) ||
        !mmio_write32(transport, base + PX_IS, 0xFFFFFFFFU))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t cmd = 0;
    if (!mmio_read32(transport, base + PX_CMD, &cmd))
        return KERNEL_STORAGE_ERR_IO;
    cmd |= AHCI_PXCMD_FRE;
    if (!mmio_write32(transport, base + PX_CMD, cmd))
        return KERNEL_STORAGE_ERR_IO;

    status = wait_mask_clear_private(
        transport,
        base + PX_TFD,
        AHCI_PXTFD_BSY | AHCI_PXTFD_DRQ
    );
    if (status != KERNEL_STORAGE_OK)
        return status;

    if (!mmio_read32(transport, base + PX_CMD, &cmd))
        return KERNEL_STORAGE_ERR_IO;
    cmd |= AHCI_PXCMD_ST;
    if (!mmio_write32(transport, base + PX_CMD, cmd) ||
        !mmio_read32(transport, base + PX_CMD, &cmd) ||
        !(cmd & AHCI_PXCMD_ST))
        return KERNEL_STORAGE_ERR_IO;

    return KERNEL_STORAGE_OK;
}

// ====== block_read_adapter ======

static int block_read_adapter(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, void *buffer)
{
    KERNEL_AHCI_DEVICE *driver = (KERNEL_AHCI_DEVICE *)device->Context;
    return kernel_ahci_command(driver, ATA_CMD_READ_DMA_EXT, lba, count, buffer);
}

// ====== block_write_adapter ======

static int block_write_adapter(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, const void *buffer)
{
    KERNEL_AHCI_DEVICE *driver = (KERNEL_AHCI_DEVICE *)device->Context;
    return kernel_ahci_command(driver, ATA_CMD_WRITE_DMA_EXT, lba, count, (void *)buffer);
}

// ====== block_flush_adapter ======

static int block_flush_adapter(KERNEL_BLOCK_DEVICE *device)
{
    KERNEL_AHCI_DEVICE *driver = (KERNEL_AHCI_DEVICE *)device->Context;
    return kernel_ahci_command(driver, ATA_CMD_FLUSH_CACHE_EXT, 0, 0, NULL);
}

// ====== kernel_ahci_init ======

int kernel_ahci_init(
    KERNEL_AHCI_DEVICE *driver,
    const AHCI_TRANSPORT *transport,
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

    // Intel AHCI 1.3.1 §3.1.2: set GHC.AE (bit 31) and disable global interrupts GHC.IE (bit 1 = 0).
    uint32_t ghc = 0, cap = 0, pi = 0, vs = 0;
    if (!mmio_read32(transport, HBA_GHC, &ghc))
        return KERNEL_STORAGE_ERR_IO;
    ghc = (ghc | AHCI_GHC_AE) & ~AHCI_GHC_IE;
    if (!mmio_write32(transport, HBA_GHC, ghc) ||
        !mmio_read32(transport, HBA_GHC, &ghc) ||
        !(ghc & AHCI_GHC_AE) ||
        !mmio_read32(transport, HBA_CAP, &cap) ||
        !mmio_read32(transport, HBA_PI, &pi) ||
        !mmio_read32(transport, HBA_VS, &vs))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t found_port = UINT32_MAX;
    for (uint32_t port = 0; port < 30U; ++port) {
        if (!(pi & (1U << port)))
            continue;
        uint32_t base = PORT_BASE(port);
        uint32_t ssts = 0, sig = 0;
        if (!mmio_read32(transport, base + PX_SSTS, &ssts) ||
            !mmio_read32(transport, base + PX_SIG, &sig))
            continue;
        if (kahci_port_usable(ssts, sig)) {
            found_port = port;
            break;
        }
    }
    if (found_port == UINT32_MAX)
        return KERNEL_STORAGE_ERR_NOT_FOUND;

    uint64_t clb_phys = control_phys + offsetof(AHCI_DMA_CONTROL, CommandList);
    uint64_t fb_phys = control_phys + offsetof(AHCI_DMA_CONTROL, ReceivedFis);
    int status = start_port_private(transport, found_port, clb_phys, fb_phys);
    if (status != KERNEL_STORAGE_OK)
        return status;

    driver->Transport = *transport;
    driver->Control = (AHCI_DMA_CONTROL *)control_page;
    driver->DataBuffer = (uint8_t *)data_page;
    driver->ControlPhys = control_phys;
    driver->DataPhys = data_phys;
    driver->PortIndex = found_port;
    driver->Version = vs;
    driver->Capabilities = cap;
    driver->Ready = 1U;
    driver->Self = (uintptr_t)driver;

    if (transport->IrqEnabled) {
        // Intel AHCI 1.3.1 §3.1.2 (GHC.IE), §3.1.3 (IS W1C), §3.3.5 (PxIS W1C), §3.3.6 (PxIE).
        uint32_t port_base = PORT_BASE(driver->PortIndex);
        uint32_t ghc_val = 0;
        if (!mmio_write32(transport, port_base + PX_IS, 0xFFFFFFFFU) ||
            !mmio_write32(transport, HBA_IS, 1U << driver->PortIndex) ||
            !mmio_write32(transport, port_base + PX_IE, AHCI_PXIE_DHRE | AHCI_PXIE_DSE | AHCI_PXIE_TFEE) ||
            !mmio_read32(transport, HBA_GHC, &ghc_val) ||
            !mmio_write32(transport, HBA_GHC, ghc_val | AHCI_GHC_IE) ||
            !mmio_read32(transport, HBA_GHC, &ghc_val) ||
            !(ghc_val & AHCI_GHC_IE)) {
            driver->Ready = 0;
            return KERNEL_STORAGE_ERR_IO;
        }
    }

    status = kernel_ahci_command(
        driver,
        ATA_CMD_IDENTIFY,
        0ULL,
        1U,
        driver->Control->IdentifyBuffer
    );
    if (status != KERNEL_STORAGE_OK) {
        driver->Ready = 0;
        return status;
    }

    uint64_t sectors = kahci_identify_sectors(driver->Control->IdentifyBuffer, 512U);
    if (!sectors) {
        driver->Ready = 0;
        return KERNEL_STORAGE_ERR_CORRUPT;
    }

    driver->CapacitySectors = sectors;
    driver->BlockDevice.Self = (uintptr_t)&driver->BlockDevice;
    driver->BlockDevice.Name = "sata0";
    driver->BlockDevice.Context = driver;
    driver->BlockDevice.TotalSectors = sectors;
    driver->BlockDevice.SectorBytes = 512U;
    driver->BlockDevice.MaxTransferSectors = AHCI_MAX_TRANSFER_SECTORS;
    driver->BlockDevice.Kind = KERNEL_BLOCK_KIND_AHCI_SATA;
    driver->BlockDevice.ReadOnly = 0U;
    driver->BlockDevice.Ready = 1U;
    driver->BlockDevice.Read = block_read_adapter;
    driver->BlockDevice.Write = block_write_adapter;
    driver->BlockDevice.Flush = block_flush_adapter;
    return KERNEL_STORAGE_OK;
}

// ====== kernel_ahci_command ======

int kernel_ahci_command(
    KERNEL_AHCI_DEVICE *driver,
    uint8_t ata_cmd,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
)
{
    if (!driver || driver->Self != (uintptr_t)driver || !driver->Ready)
        return KERNEL_STORAGE_ERR_ARG;

    int is_write = 0;
    int has_data = 0;
    if (ata_cmd == ATA_CMD_FLUSH_CACHE_EXT) {
        if (lba != 0 || sector_count != 0)
            return KERNEL_STORAGE_ERR_ARG;
    } else if (ata_cmd == ATA_CMD_IDENTIFY) {
        if (!buffer || lba != 0 || sector_count != 1U)
            return KERNEL_STORAGE_ERR_ARG;
        has_data = 1;
    } else if (ata_cmd == ATA_CMD_READ_DMA_EXT || ata_cmd == ATA_CMD_WRITE_DMA_EXT) {
        if (!buffer || !sector_count || sector_count > AHCI_MAX_TRANSFER_SECTORS)
            return KERNEL_STORAGE_ERR_ARG;
        if (lba >= driver->CapacitySectors || sector_count > driver->CapacitySectors - lba)
            return KERNEL_STORAGE_ERR_RANGE;
        has_data = 1;
        is_write = (ata_cmd == ATA_CMD_WRITE_DMA_EXT);
    } else {
        return KERNEL_STORAGE_ERR_ARG;
    }

    uint32_t base = PORT_BASE(driver->PortIndex);
    int status = wait_mask_clear_private(
        &driver->Transport,
        base + PX_TFD,
        AHCI_PXTFD_BSY | AHCI_PXTFD_DRQ
    );
    if (status != KERNEL_STORAGE_OK)
        return status;

    if (!mmio_write32(&driver->Transport, base + PX_IS, 0xFFFFFFFFU) ||
        !mmio_write32(&driver->Transport, HBA_IS, 1U << driver->PortIndex) ||
        !mmio_write32(&driver->Transport, base + PX_SERR, 0xFFFFFFFFU))
        return KERNEL_STORAGE_ERR_IO;

    AHCI_DMA_CONTROL *ctrl = driver->Control;
    size_t transfer_bytes = (size_t)sector_count * 512U;
    if (has_data) {
        if (is_write)
            copy_bytes(driver->DataBuffer, buffer, transfer_bytes);
        else
            zero_bytes(driver->DataBuffer, transfer_bytes);
    }

    zero_bytes(&ctrl->CommandTable, sizeof(ctrl->CommandTable));
    if (!kahci_build_h2d_fis(
            ata_cmd,
            lba,
            (uint16_t)sector_count,
            ctrl->CommandTable.CommandFis
        ))
        return KERNEL_STORAGE_ERR_ARG;

    uint64_t cmd_table_phys = driver->ControlPhys + offsetof(AHCI_DMA_CONTROL, CommandTable);
    // CFL = 5 DWORDS (20 bytes); W = bit 6; C = bit 10.
    uint16_t flags = (uint16_t)(5U | (is_write ? (1U << 6) : 0U) | (1U << 10));
    ctrl->CommandList[0].Flags = flags;
    ctrl->CommandList[0].PrdtLength = has_data ? 1U : 0U;
    ctrl->CommandList[0].PrdByteCount = 0U;
    ctrl->CommandList[0].CmdTableBaseLow = (uint32_t)cmd_table_phys;
    ctrl->CommandList[0].CmdTableBaseHigh = (uint32_t)(cmd_table_phys >> 32);

    if (has_data) {
        ctrl->CommandTable.Prdt[0].DataBaseLow = (uint32_t)driver->DataPhys;
        ctrl->CommandTable.Prdt[0].DataBaseHigh = (uint32_t)(driver->DataPhys >> 32);
        ctrl->CommandTable.Prdt[0].Reserved = 0U;
        ctrl->CommandTable.Prdt[0].ByteCountAndInterrupt = (uint32_t)(transfer_bytes - 1U);
    }

    uint64_t irq_seq = (driver->Transport.IrqEnabled && driver->Transport.IrqSeq)
        ? driver->Transport.IrqSeq(driver->Transport.Context)
        : 0ULL;

    driver->Transport.Barrier(driver->Transport.Context);
    if (!mmio_write32(&driver->Transport, base + PX_CI, 1U))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t start = 0;
    if (!driver->Transport.Clock(driver->Transport.Context, &start))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t elapsed = 0;
    uint32_t last = start;

    for (uint32_t poll = 0; poll < driver->Transport.MaxPolls; ++poll) {
        uint32_t px_is = 0, px_ci = 0, px_tfd = 0;
        if (!mmio_read32(&driver->Transport, base + PX_IS, &px_is) ||
            !mmio_read32(&driver->Transport, base + PX_CI, &px_ci) ||
            !mmio_read32(&driver->Transport, base + PX_TFD, &px_tfd))
            return KERNEL_STORAGE_ERR_IO;

        if ((px_is & AHCI_PXIS_TFES) || (px_tfd & AHCI_PXTFD_ERR)) {
            (void)mmio_write32(&driver->Transport, base + PX_IS, px_is);
            (void)mmio_write32(&driver->Transport, HBA_IS, 1U << driver->PortIndex);
            return KERNEL_STORAGE_ERR_IO;
        }

        if ((px_ci & 1U) == 0U && (px_tfd & (AHCI_PXTFD_BSY | AHCI_PXTFD_DRQ)) == 0U) {
            if (px_is) {
                (void)mmio_write32(&driver->Transport, base + PX_IS, px_is);
                (void)mmio_write32(&driver->Transport, HBA_IS, 1U << driver->PortIndex);
            }
            driver->Transport.Barrier(driver->Transport.Context);
            if (has_data && !is_write)
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
