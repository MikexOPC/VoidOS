"""V1 witness: VirtIO GPU DMA under VT-d (guest report against the QEMU v11.1.0 trace)."""
import re

# QEMU v11.1.0 hw/i386/trace-events used by the V1 campaign.
EVENTS = ('vtd_dmar_enable', 'vtd_reg_dmar_root', 'vtd_switch_address_space', 'vtd_frr_new', 'vtd_dmar_fault',
    'vtd_inv_desc_iotlb_global')
DEVICE = ['-device', 'intel-iommu,intremap=off,caching-mode=on,device-iotlb=off']
ACCEPTED = (1 << 32) | (1 << 33)   # VERSION_1 and ACCESS_PLATFORM (VirtIO 1.2 6)

# ====== numbers ======

def numbers(log, name):
    return [int(value, 16) for value in re.findall(re.escape(name) + r': 0x([0-9a-fA-F]{16})', log)]

def one(log, name):
    values = numbers(log, name)
    if len(values) != 1: raise RuntimeError('V1 evidence count: ' + name)
    return values[0]

# ====== verify ======

# fault: the revocation profile, one GPU read fault (FR 6) on the first backing page at ATTACH.
def verify(log, trace, fault):
    for marker in ('KERNEL_VGPU_IOMMU_BEGIN', 'KERNEL_VGPU_IOMMU_TRANSLATING', 'KERNEL_VGPU_IOMMU_RELEASED'):
        if marker not in log: raise RuntimeError('V1 marker missing: ' + marker)
    if 'KERNEL_VGPU_IOMMU_RETAINED' in log: raise RuntimeError('V1 retained the domain')
    source, queue, surface, backing = (one(log, 'KERNEL_VGPU_IOMMU_' + name) for name in ('SOURCE', 'QUEUE', 'SURFACE', 'BACKING'))
    if surface != backing or queue & 4095 or surface & 4095 or not 0x01000000 < queue < surface < 0x03000000:
        raise RuntimeError('V1 IOVA layout')
    if one(log, 'KERNEL_VGPU_RESOURCE_FEATURES') != ACCEPTED: raise RuntimeError('ACCESS_PLATFORM not negotiated')
    records = list(zip(numbers(log, 'KERNEL_VGPU_IOMMU_FAULT_SOURCE'), numbers(log, 'KERNEL_VGPU_IOMMU_FAULT_REASON'),
        numbers(log, 'KERNEL_VGPU_IOMMU_FAULT_READ'), numbers(log, 'KERNEL_VGPU_IOMMU_FAULT_ADDRESS')))
    owned = [record for record in records if record[0] == source]
    expected = [(source, 6, 1, backing)] if fault else []
    if owned != expected or one(log, 'KERNEL_VGPU_IOMMU_OWNED') or one(log, 'KERNEL_VGPU_IOMMU_ACKNOWLEDGED') != len(expected):
        raise RuntimeError('V1 GPU fault log')
    if fault and 'KERNEL_VGPU_IOMMU_REVOKE_BLOCKED' not in log: raise RuntimeError('Revocation not proven')
    lines = trace.splitlines()
    enables = [index for index, line in enumerate(lines) if line.startswith('vtd_dmar_enable')]
    if [lines[index] for index in enables] != ['vtd_dmar_enable enable 1', 'vtd_dmar_enable enable 0']:
        raise RuntimeError('V1 enable trace')
    on, off = enables
    bdf = f'{source >> 8:02x}:{(source >> 3) & 31:02x}.{source & 7}'
    if f'vtd_switch_address_space Device {bdf} switching address space (iommu enabled=1)' not in lines[on:off]:
        raise RuntimeError('GPU not translated')
    gpu = []
    for index, line in enumerate(lines):
        match = re.fullmatch(r'vtd_frr_new index \d+ high (0x[0-9a-f]+) low (0x[0-9a-f]+)', line)
        if match and int(match[1], 16) & 0xffff == source:
            high, low = int(match[1], 16), int(match[2], 16)
            gpu.append((source, (high >> 32) & 255, (high >> 62) & 1, low & ~4095, index))
    if [record[:4] for record in gpu] != expected or not all(on < record[4] < off for record in gpu):
        raise RuntimeError('QEMU GPU fault records differ from the guest')
    flushes = sum(1 for line in lines if line.startswith('vtd_inv_desc_iotlb_global'))
    if flushes != (3 if fault else 2): raise RuntimeError('V1 invalidation count')
    return dict(Source=hex(source), Queue=hex(queue), Surface=hex(surface), Features=hex(ACCEPTED),
        GpuFaults=len(gpu), Invalidations=flushes, Translated=True)
