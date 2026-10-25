"""Verify retired private VT-d structures independently through QTest."""
import base64
import re
# K1: slot indices from arch/x86/configs/layout.h
import importlib.util as _layout_util
_layout_spec = _layout_util.spec_from_file_location("os_layout", __import__("pathlib").Path(__file__).resolve().parents[9] / "scripts/lib/layout.py")
layout = _layout_util.module_from_spec(_layout_spec)
_layout_spec.loader.exec_module(layout)

# ====== verify ======

def verify(backend,stream,directory,log,discovery):
    number=backend.protocol.number
    if 'KERNEL_VTD_DOMAIN_PRIVATE_OK' not in log: raise RuntimeError('Private domain probe incomplete')
    if number(log,'KERNEL_VTD_DOMAIN_BUDGET_BEFORE')!=number(log,'KERNEL_VTD_DOMAIN_BUDGET_AFTER'):
        raise RuntimeError('Domain budget mismatch')
    def values(label):
        return [int(v,16) for v in re.findall('KERNEL_VTD_DOMAIN_'+label+r': 0x([0-9a-fA-F]+)',log)]
    counts=values('TABLES'); buses=values('BUS'); devfns=values('DEVFN'); widths=values('BITS')
    if len(counts)!=2 or counts!=[14,14] or len(buses)!=2 or buses[0]!=buses[1] or not 0<=buses[0]<=255:
        raise RuntimeError('Domain geometry mismatch')
    if len(devfns)!=2 or devfns[0]!=devfns[1] or not 0<=devfns[0]<=255 or len(widths)!=2 or widths[0]!=widths[1] or not 32<=widths[0]<=min(52,discovery['HostBits']):
        raise RuntimeError('Domain identity/width mismatch')
    tables=values('TABLE')
    if len(tables)!=28 or len(set(tables[-14:]))!=14: raise RuntimeError('Domain table count mismatch')
    tables=tables[-14:]; ranges=[]
    fields={key:values(key) for key in ('IOVA','PHYSICAL','PAGES','ACCESS')}
    if any(len(v)!=4 or v[:2]!=v[2:] for v in fields.values()): raise RuntimeError('Domain rebind ranges mismatch')
    for i in range(2):
        ranges.append({key:v[i] for key,v in fields.items()})
    if [r['PAGES'] for r in ranges]!=[4,4096] or [r['ACCESS'] for r in ranges]!=[3,1]:
        raise RuntimeError('Domain buffer geometry/permissions mismatch')
    if [r['IOVA'] for r in ranges]!=[0x1001000,0x1006000]: raise RuntimeError('Domain IOVA/guards mismatch')
    for r in ranges:
        if r['PHYSICAL']&4095 or not 0x100000<=r['PHYSICAL']<=0x8000000-r['PAGES']*4096:
            raise RuntimeError('DMA buffer outside test RAM')
    if max(r['PHYSICAL'] for r in ranges)<min(r['PHYSICAL']+r['PAGES']*4096 for r in ranges):
        raise RuntimeError('DMA buffer alias')
    for index,physical in enumerate(tables):
        if physical&4095 or not 0x100000<=physical<=0x8000000-4096 or any(
            r['PHYSICAL']<=physical<r['PHYSICAL']+r['PAGES']*4096 for r in ranges):
            raise RuntimeError('Domain table outside owned test RAM')
        stream.write(f'b64read {physical:#x} 0x1000\n'.encode()); stream.flush()
        response=stream.readline(16384).decode().split()
        if len(response)!=2 or response[0]!='OK': raise RuntimeError('Domain table read failed')
        raw=base64.b64decode(response[1],validate=True)
        if len(raw)!=4096: raise RuntimeError('Short domain table')
        (directory/f'domain-table-{index}.bin').write_bytes(raw)
        for slot in range(512):
            expected=0
            if index==0 and slot==buses[0]*2: expected=tables[1]|1
            if index==1:
                if slot==devfns[0]*2: expected=tables[2]|1
                if slot==devfns[0]*2+1: expected=0x102
            if index in (2,3) and slot==0: expected=tables[index+1]|3
            if index==4 and 8<=slot<17: expected=tables[5+slot-8]|3
            if index>=5:
                address=0x1000000+(index-5)*0x200000+slot*4096
                for r in ranges:
                    if r['IOVA']<=address<r['IOVA']+r['PAGES']*4096:
                        expected=(r['PHYSICAL']+address-r['IOVA'])|r['ACCESS']
            if int.from_bytes(raw[slot*8:slot*8+8],'little')!=expected:
                raise RuntimeError(f'Domain entry mismatch {index}:{slot}')
    root=number(log,'KERNEL_VTD_ROOT')
    if backend.qtest(stream,f'readq {root+layout.slot("X86_LAYOUT_IOMMU_SLOT")*8:#x}'): raise RuntimeError('CPU register mapping not revoked')
    for unit in discovery['Units']:
        if backend.qtest(stream,f'readl {unit["Base"]+0x1c:#x}'): raise RuntimeError('VT-d state unexpectedly changed')
    return dict(Private=True,Active=False,TablePages=len(tables),DmaPages=4100,Guards=True,
                UnusedContextsAbsent=True,ReadOnlyPixels=True,Tables=tables,Ranges=ranges)
