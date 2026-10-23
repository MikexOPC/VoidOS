"""Independent VT-d DMA witness (block 3): guest report against the QEMU v11.1.0 intel_iommu trace."""
import re

QIE, SRTP, TE = 1 << 26, 1 << 30, 1 << 31
SOURCE = 0x28                  # edu at 00:05.0, placed by the campaign
BASE = 0x01000000              # X86_VTD_DMA_BASE: guard page without a leaf
PHASES = ['P1_ALLOWED', 'P2_READ_ONLY', 'P3_UNMAPPED', 'P4_REVOKED', 'P5_STILL_MAPPED']

# ====== numbers ======

def numbers(log, name):
    return [int(value, 16) for value in re.findall(re.escape(name) + r': 0x([0-9a-fA-F]{16})', log)]

def one(log, name):
    values = numbers(log, name)
    if len(values) != 1: raise RuntimeError('DMA evidence count: ' + name)
    return values[0]

# ====== expected_faults ======

# VT-d5.0 7.1.3 / QEMU VTDFaultReason: 5 write without W, 6 read without R (records: SID, FR, T, page).
def expected_faults(log):
    d, b = one(log, 'KERNEL_DMA_IOVA_D'), one(log, 'KERNEL_DMA_IOVA_B')
    return [(SOURCE, 5, 0, d), (SOURCE, 5, 0, BASE), (SOURCE, 6, 1, BASE), (SOURCE, 5, 0, b)]

# ====== verify ======

def verify(log, routing):
    if 'KERNEL_DMA_OK' not in log or 'KERNEL_DMA_RETAINED' in log or 'KERNEL_DMA_FAILURE' in log:
        raise RuntimeError('DMA probe did not complete')
    if one(log, 'KERNEL_DMA_BUDGET_BEFORE') != one(log, 'KERNEL_DMA_BUDGET_AFTER'): raise RuntimeError('DMA budget')
    if re.findall(r'KERNEL_DMA_PHASE: (\w+)', log) != PHASES: raise RuntimeError('DMA phase order')
    iova = [one(log, 'KERNEL_DMA_IOVA_' + name) for name in 'ABCD']
    if any(value & 4095 or not BASE < value < BASE + 0x02000000 for value in iova) or len(set(iova)) != 4:
        raise RuntimeError('IOVA layout')
    bar = one(log, 'KERNEL_DMA_BAR')
    if not bar or bar & 0xfffff or bar >= 1 << 32: raise RuntimeError('edu BAR0')
    if one(log, 'KERNEL_DMA_SOURCE') != SOURCE or routing.get('Unit') != 0: raise RuntimeError('DMA source')
    blocked = one(log, 'KERNEL_DMA_BLOCKED')
    if one(log, 'KERNEL_DMA_OWNED') or one(log, 'KERNEL_DMA_ACKNOWLEDGED') != 4: raise RuntimeError('Owned faults')
    records = list(zip(numbers(log, 'KERNEL_DMA_FAULT_SOURCE'), numbers(log, 'KERNEL_DMA_FAULT_REASON'),
        numbers(log, 'KERNEL_DMA_FAULT_READ'), numbers(log, 'KERNEL_DMA_FAULT_ADDRESS')))
    owned = [record for record in records if record[0] == SOURCE]
    if owned != expected_faults(log) or one(log, 'KERNEL_DMA_LOGGED') != len(records) or len(records) != min(4 + blocked, 16):
        raise RuntimeError('Guest fault log differs from P2-P4')
    return dict(Phases=len(PHASES), Iova=[hex(value) for value in iova], Bar=hex(bar), ProvokedFaults=4,
        Blocked=blocked, BudgetRestored=True)

# ====== verify_trace ======

def verify_trace(directory, log):
    lines = (directory / 'routing.trace').read_text(errors='replace').splitlines()
    root, fro = one(log, 'KERNEL_DMA_ROOT'), None
    events = []
    for line in lines:
        match = re.fullmatch(r'vtd_reg_write addr (0x[0-9a-f]+) size (0x[0-9a-f]+) value (0x[0-9a-f]+)', line)
        if match: events.append(('write',) + tuple(int(value, 16) for value in match.groups())); continue
        events.append((line.split(' ', 1)[0], line))
    writes = [event[1:] for event in events if event[0] == 'write']
    for offset, width, value in writes:
        record = offset >= 0x100 and offset % 16 == 8 and width == 8 and value == 1 << 63
        if not ((offset == 0x18 and width == 4 and value in (0, QIE, QIE | SRTP, QIE | TE)) or
                (offset == 0x20 and width == 8 and value in (0, root)) or (offset in (0x88, 0x90) and width == 8) or
                (offset == 0x34 and width == 4 and value == 1) or record):
            raise RuntimeError(f'Register write outside the whitelist: {offset:#x}/{width}={value:#x}')
    queue = next((value for offset, _, value in writes if offset == 0x90 and value), 0)
    control = [(offset, value) for offset, _, value in writes if offset in (0x18, 0x20) or (offset in (0x88, 0x90) and value in (0, queue))]
    expected = [(0x88, 0), (0x90, queue), (0x18, QIE), (0x20, root), (0x18, QIE | SRTP), (0x18, QIE | TE), (0x18, QIE), (0x20, 0),
        (0x18, 0), (0x88, 0), (0x90, 0)]
    if not queue or queue & 4095 or control != expected: raise RuntimeError('DMA command sequence')
    order = [event[0] for event in events]
    enables = [index for index, name in enumerate(order) if name == 'vtd_dmar_enable']
    published = [index for index, name in enumerate(order) if name == 'vtd_reg_dmar_root']
    triplet = ['vtd_inv_desc_cc_global', 'vtd_inv_desc_iotlb_global', 'vtd_inv_desc_wait_sw']
    flushes = [(index, name) for index, name in enumerate(order) if name in triplet]
    if len(enables) != 2 or len(published) != 1 or [name for _, name in flushes] != triplet * 3:
        raise RuntimeError('Publication, enable or invalidation trace')
    on, off = enables
    # SRTP flush before TE, the revocation flush while translating, the teardown flush after TE=0.
    if not published[0] < flushes[0][0] < flushes[2][0] < on < flushes[3][0] < flushes[5][0] < off < flushes[6][0]:
        raise RuntimeError('Invalidation order')
    revoke = flushes[3][0]
    recorded = []
    for index, (name, *rest) in enumerate(events):
        if name != 'vtd_frr_new': continue
        match = re.fullmatch(r'vtd_frr_new index \d+ high (0x[0-9a-f]+) low (0x[0-9a-f]+)', rest[0])
        if not match: raise RuntimeError('Fault record trace format')
        high, low = (int(value, 16) for value in match.groups())
        recorded.append(((high & 0xffff), (high >> 32) & 255, (high >> 62) & 1, low & ~4095, index))
    owned = [record for record in recorded if record[0] == SOURCE]
    if [record[:4] for record in owned] != expected_faults(log): raise RuntimeError('QEMU fault records differ from P2-P4')
    if not all(on < record[4] < off for record in owned) or not owned[3][4] > revoke:
        raise RuntimeError('Owned faults outside translation or before revocation')
    if len(recorded) - len(owned) != one(log, 'KERNEL_DMA_BLOCKED'): raise RuntimeError('Blocked requester records')
    faults = [line for name, *rest in events if name == 'vtd_dmar_fault' for line in rest]
    if any(not line.startswith(f'vtd_dmar_fault sid {SOURCE:#x} ') for line in faults if f'sid {SOURCE:#x}' in line) or len(faults) < 4:
        raise RuntimeError('DMA fault trace')
    switched = [event[1] for event in events[on:off] if event[0] == 'vtd_switch_address_space']
    if 'vtd_switch_address_space Device 00:05.0 switching address space (iommu enabled=1)' not in switched:
        raise RuntimeError('edu not translated')
    return dict(Writes=len(writes), Whitelist=True, SequenceExact=True, Invalidations=3, OwnedRecords=len(owned),
        QemuFaultAccesses=len(faults), RevocationFlushBeforeP4=True)
