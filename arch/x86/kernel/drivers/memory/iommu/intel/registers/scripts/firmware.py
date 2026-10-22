"""Independent read-only VT-d and retired paging-table verification."""
import base64
import re

# ====== verify ======

def verify(backend,stream,directory,log,discovery):
    number=backend.protocol.number
    if 'KERNEL_VTD_REGISTERS_OK' not in log: raise RuntimeError('VT-d probe did not complete')
    if number(log,'KERNEL_VTD_BUDGET_BEFORE')!=number(log,'KERNEL_VTD_BUDGET_AFTER'): raise RuntimeError('VT-d budget mismatch')
    root=number(log,'KERNEL_VTD_ROOT'); cache=number(log,'KERNEL_VTD_CACHE'); pat=number(log,'KERNEL_VTD_PAT')
    if not 0x100000<=root<=0x08000000-4096 or root&4095 or cache&~0x98: raise RuntimeError('Invalid root/cache')
    slot=((cache>>3)&3)|((cache>>5)&4)
    if (pat>>(slot*8))&255 or backend.qtest(stream,f'readq {root+228*8:#x}'): raise RuntimeError('Cache or revocation mismatch')
    tables=[int(value,16) for value in re.findall(r'KERNEL_VTD_TABLE: 0x([0-9a-fA-F]+)',log)]
    if len(tables)!=6 or len(set(tables[-3:]))!=3: raise RuntimeError('Wrong table count')
    tables=tables[-3:]
    for level,physical in enumerate(tables):
        if physical&4095 or not 0x100000<=physical<=0x08000000-4096: raise RuntimeError('Table outside RAM')
        stream.write(f'b64read {physical:#x} 0x1000\n'.encode()); stream.flush()
        fields=stream.readline(16384).decode().split()
        if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('Table snapshot failed')
        raw=base64.b64decode(fields[1],validate=True)
        if len(raw)!=4096: raise RuntimeError('Short table snapshot')
        (directory/f'vtd-table-{level}.bin').write_bytes(raw)
        for index in range(512):
            expected=0
            if level<2 and index==0: expected=tables[level+1]|3
            if level==2 and index%2==1 and index//2<len(discovery['Units']):
                expected=discovery['Units'][index//2]['Base']|cache|(1<<63)|1
            if int.from_bytes(raw[index*8:index*8+8],'little')&~0x60!=expected: raise RuntimeError('Retired PTE/RO/NX/guard mismatch')
    registers=[]
    for index,unit in enumerate(discovery['Units']):
        observed={}
        for label,offset,width in [('VERSION',0,4),('CAP',8,8),('ECAP',16,8),('STATUS',28,4)]:
            values=[int(value,16) for value in re.findall('KERNEL_VTD_'+label+r': 0x([0-9a-fA-F]+)',log)]
            if len(values)!=2*len(discovery['Units']) or values[index]!=values[index+len(discovery['Units'])]: raise RuntimeError('Rebind register mismatch')
            value=backend.qtest(stream,f'{"readq" if width==8 else "readl"} {unit["Base"]+offset:#x}')
            if values[index]!=value: raise RuntimeError('Guest/QTest register mismatch')
            observed[label]=value
        if observed['STATUS']!=0: raise RuntimeError('Unexpected active VT-d state')
        registers.append(observed)
    return dict(ReadOnly=True,NX=True,Guards=True,UC=True,Revoked=True,Registers=registers)
