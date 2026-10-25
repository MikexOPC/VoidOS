"""Independent QTest verification of QI completion, retired buffers and CPU mapping."""
import base64
import re
# K1: slot indices from arch/x86/configs/layout.h
import importlib.util as _layout_util
_layout_spec = _layout_util.spec_from_file_location("os_layout", __import__("pathlib").Path(__file__).resolve().parents[9] / "scripts/lib/layout.py")
layout = _layout_util.module_from_spec(_layout_spec)
_layout_spec.loader.exec_module(layout)

# ====== page ======

def page(stream,physical,directory,name):
    # B8: low memory (page 0 excluded) is in the pool, so the queue may sit below 1 MiB.
    if physical&4095 or not 0x1000<=physical<=0x8000000-4096: raise RuntimeError('QI page outside test RAM')
    stream.write(f'b64read {physical:#x} 0x1000\n'.encode()); stream.flush()
    fields=stream.readline(16384).decode().split()
    if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('QI page read failed')
    raw=base64.b64decode(fields[1],validate=True)
    if len(raw)!=4096: raise RuntimeError('Short QI snapshot')
    (directory/name).write_bytes(raw)
    return raw

# ====== verify ======

def verify(backend,stream,directory,log,discovery):
    number=backend.protocol.number
    if 'KERNEL_QI_OK' not in log: raise RuntimeError('Missing QI completion')
    if number(log,'KERNEL_QI_BUDGET_BEFORE')!=number(log,'KERNEL_QI_BUDGET_AFTER'): raise RuntimeError('QI budget mismatch')
    queue=number(log,'KERNEL_QI_QUEUE'); status=number(log,'KERNEL_QI_STATUS_PAGE')
    cap=number(log,'KERNEL_QI_CAP'); token=number(log,'KERNEL_QI_TOKEN'); tail=number(log,'KERNEL_QI_TAIL')
    if token!=128 or tail!=2048 or queue==status or number(log,'KERNEL_QI_CLEAN_TABLES')!=14:
        raise RuntimeError('QI geometry/token mismatch')
    line=number(log,'KERNEL_QI_CPU_LINE')
    if line<8 or line>4096 or line&(line-1): raise RuntimeError('CPU cache geometry')
    raw=page(stream,queue,directory,'queue.bin'); completion=page(stream,status,directory,'completion.bin')
    expected=bytearray(4096)
    for iteration in range(1,129):
        low=[0x11,0x12|(((cap>>54)&1)<<6)|(((cap>>55)&1)<<7),0x65|(iteration<<32)]
        for index in range(3):
            slot=((iteration-1)*3+index)%256
            expected[slot*16:slot*16+8]=low[index].to_bytes(8,'little')
            expected[slot*16+8:slot*16+16]=(status if index==2 else 0).to_bytes(8,'little')
    if raw!=expected or completion!=token.to_bytes(4,'little')+bytes(4092): raise RuntimeError('QI descriptor/coherent completion mismatch')
    physical=number(log,'KERNEL_QI_PHYSICAL')
    if len(discovery['Units'])!=1 or physical!=discovery['Units'][0]['Base']: raise RuntimeError('QI remapper identity')
    for offset,width in [(0x1c,4),(0x20,8),(0x34,4),(0x80,8),(0x88,8)]:
        if backend.qtest(stream,f'{"readl" if width==4 else "readq"} {physical+offset:#x}'):
            raise RuntimeError('QI stop/register restoration mismatch')
    observed_iqa=backend.qtest(stream,f'readq {physical+0x90:#x}')
    if observed_iqa not in (0,queue): raise RuntimeError('Foreign latched IQA after stop')
    root=number(log,'KERNEL_QI_ROOT')
    if backend.qtest(stream,f'readq {root+layout.slot("X86_LAYOUT_IOMMU_SLOT")*8:#x}'): raise RuntimeError('QI CPU mapping still active')
    tables=[int(value,16) for value in re.findall(r'KERNEL_QI_TABLE: 0x([0-9a-fA-F]+)',log)]
    if len(tables)!=3 or len(set([queue,status,*tables]))!=5: raise RuntimeError('QI page alias')
    cache=number(log,'KERNEL_QI_CACHE'); pat=number(log,'KERNEL_QI_PAT')
    slot=((cache>>3)&3)|((cache>>5)&4)
    if cache&~0x98 or (pat>>(slot*8))&255: raise RuntimeError('QI UC policy')
    for level,table in enumerate(tables):
        data=page(stream,table,directory,f'cpu-table-{level}.bin')
        for index in range(512):
            value=0
            if level<2 and index==0: value=tables[level+1]|3
            if level==2 and index==1: value=physical|cache|(1<<63)|3
            if int.from_bytes(data[index*8:index*8+8],'little')&~0x60!=value: raise RuntimeError('QI RW/NX/guard PTE mismatch')
    return dict(Completions=token,QueueWrap=True,DomainRootUnpublished=True,TranslationActive=False,
                QIStopped=True,BudgetRestored=True,UC=True,NX=True,RetiredWritablePTE=True,CPUCacheLine=line,LatchedIQARemains=observed_iqa==queue)

# ====== verify_trace ======

def verify_trace(directory,active):
    lines=(directory/'qi.trace').read_text(errors='replace').splitlines()
    writes=[line for line in lines if line.startswith('vtd_reg_write ')]
    if not active:
        if writes or any(line.startswith('vtd_inv_desc_') for line in lines): raise RuntimeError('Unexpected QI device operation')
        return dict(Commands=0)
    observed=[]
    names=['vtd_inv_desc_cc_global','vtd_inv_desc_iotlb_global','vtd_inv_desc_wait_sw']
    for line in lines:
        observed.extend(name for name in names if line.startswith(name+' '))
    if observed!=names*128: raise RuntimeError('Hardware invalidation sequence mismatch')
    enables=[line for line in lines if line.startswith('vtd_inv_qi_enable ')]
    if enables!=['vtd_inv_qi_enable enabled 1','vtd_inv_qi_enable enabled 0']: raise RuntimeError('QI enable/stop sequence')
    parsed=[tuple(int(v,16) for v in re.fullmatch(r'vtd_reg_write addr (0x[0-9a-f]+) size (0x[0-9a-f]+) value (0x[0-9a-f]+)',line).groups()) for line in writes]
    if any(offset not in (0x18,0x88,0x90) or (offset==0x18 and (width!=4 or value not in (0,1<<26))) or
           (offset!=0x18 and width!=8) for offset,width,value in parsed): raise RuntimeError('QI write whitelist violated')
    if parsed[-3:]!=[(0x18,4,0),(0x88,8,0),(0x90,8,0)]: raise RuntimeError('QI final command sequence')
    return dict(Commands=len(observed),Completions=128,Whitelist=True,DisableConfirmed=True)
