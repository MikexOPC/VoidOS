"""Independent DMAR path resolver and serialized witnesses; never enables translation."""
import struct

# ====== info ======

def info(raw):
    if len(raw)!=992: raise RuntimeError('DMAR witness size')
    physical,bits,flags,count,scopes,other,reserved=struct.unpack_from('<Q6I',raw)
    if not 1<=count<=8 or scopes>32 or other or reserved: raise RuntimeError('DMAR witness metadata')
    units=[]; entries=[]
    for index in range(count):
        base,size,segment,flags,first,length=struct.unpack_from('<QQHBBI',raw,32+index*24)
        units.append(dict(Base=base,Bytes=size,Segment=segment,Flags=flags,First=first,Count=length))
    for index in range(scopes):
        unit,kind,enumeration,bus,depth,reserved,path=struct.unpack_from('<5B3s16s',raw,224+index*24)
        if reserved!=bytes(3) or not 1<=depth<=8: raise RuntimeError('DMAR scope witness')
        entries.append(dict(Unit=unit,Type=kind,Enumeration=enumeration,Bus=bus,Path=list(zip(path[::2],path[1::2]))[:depth]))
    if any(raw[32+count*24:224]) or any(raw[224+scopes*24:]): raise RuntimeError('Unused DMAR witness not zero')
    return dict(Physical=physical,HostBits=bits,Flags=struct.unpack_from('<I',raw,12)[0],Units=units,Scopes=entries)

# ====== decode ======

def decode(raw):
    if raw[:4]!=b'DMAR' or len(raw)!=struct.unpack_from('<I',raw,4)[0] or sum(raw)&255: raise RuntimeError('Raw DMAR checksum/header')
    units=[]; scopes=[]; offset=48
    while offset<len(raw):
        kind,length=struct.unpack_from('<HH',raw,offset)
        if kind or length<16 or offset+length>len(raw): raise RuntimeError('Unsupported raw DRHD')
        flags,size,segment,base=struct.unpack_from('<BBHQ',raw,offset+4)
        index=len(units); first=len(scopes); at=offset+16
        while at<offset+length:
            typ,span,flags_scope,reserved,enumeration,bus=struct.unpack_from('<6B',raw,at)
            if span<8 or span&1 or at+span>offset+length or flags_scope or reserved: raise RuntimeError('Raw scope')
            path=raw[at+6:at+span]
            scopes.append(dict(Unit=index,Type=typ,Enumeration=enumeration,Bus=bus,Path=list(zip(path[::2],path[1::2]))))
            at+=span
        units.append(dict(Base=base,Bytes=4096<<size,Segment=segment,Flags=flags,First=first,Count=len(scopes)-first))
        offset+=length
    return dict(HostBits=raw[36]+1,Flags=raw[37],Units=units,Scopes=scopes)

# ====== expected ======

def expected(data,nodes,request):
    segment,bus,device,function,identity=request; lookup={n['Key']:i for i,n in enumerate(nodes)}
    target=lookup[(segment,bus,device,function)]
    if nodes[target]['Id']!=identity: raise RuntimeError('GPU identity changed')
    matches=[]; claims=[]
    for index,scope in enumerate(data['Scopes']):
        unit=data['Units'][scope['Unit']]
        if unit['Segment']!=segment: continue
        if scope['Type'] in (3,4): continue
        if scope['Type'] not in (1,2): raise ValueError('Unsupported scope')
        current_bus=scope['Bus']; parent=0xffffffff; terminal=None
        for step,(dev,fn) in enumerate(scope['Path']):
            terminal=lookup[(segment,current_bus,dev,fn)]; node=nodes[terminal]
            if node['Parent']!=parent: raise ValueError('Invalid path parent')
            if step+1<len(scope['Path']):
                if (node['Header']>>16)&127!=1: raise ValueError('Path through endpoint')
                current_bus=(node['Buses']>>8)&255; parent=terminal
        if terminal is None or ((nodes[terminal]['Header']>>16)&127)!=(scope['Type']==2): raise ValueError('Scope terminal')
        members={terminal}
        if scope['Type']==2:
            for candidate in range(len(nodes)):
                current=candidate
                for _ in range(9):
                    if current==terminal: members.add(candidate); break
                    if current==0xffffffff: break
                    current=nodes[current]['Parent']
        if any(members&old for old in claims): raise ValueError('Overlapping explicit scopes')
        claims.append(members)
        if target in members: matches.append((scope['Unit'],scope['Type'],index))
    if not matches:
        matches=[(i,3,0xffffffff) for i,u in enumerate(data['Units']) if u['Segment']==segment and u['Flags']==1]
    if len(matches)>1: raise ValueError('Ambiguous remapper')
    if not matches: return None
    unit,kind,scope=matches[0]; owner=data['Units'][unit]
    return struct.pack('<QQIIIIHHI',owner['Base'],owner['Bytes'],unit,target,scope,kind,segment,(bus<<8)|(device<<3)|function,0)

# ====== verify ======

def verify(backend,stream,directory,log,topo,absent):
    if absent:
        if 'KERNEL_ROUTING_DMAR_ABSENT' not in log or 'KERNEL_ROUTING_NATIVE_OK' in log: raise RuntimeError('DMAR absence result')
        physical=backend.protocol.number(log,'KERNEL_ACPI_ROOT'); length=backend.qtest(stream,f'readl {physical+4:#x}')
        if not 36<=length<=4096: raise RuntimeError('ACPI root bounds')
        root=topo.physical_read(stream,physical,length)
        width=8 if root[:4]==b'XSDT' else 4 if root[:4]==b'RSDT' else 0
        if not width or (length-36)%width or sum(root)&255: raise RuntimeError('ACPI root checksum')
        for offset in range(36,length,width):
            entry=int.from_bytes(root[offset:offset+width],'little')
            if topo.physical_read(stream,entry,36)[:4]==b'DMAR': raise RuntimeError('Native DMAR was missed')
        (directory/'routing-absent-acpi-root.bin').write_bytes(root)
        return dict(DmarAbsent=True,IndependentAcpiAbsence=True,TranslationActivated=False)
    if 'KERNEL_ROUTING_OK' not in log or 'KERNEL_ROUTING_SYNTHETIC_4_OK' not in log: raise RuntimeError('Missing routing completion')
    number=backend.protocol.number; root=number(log,'KERNEL_ECAM_ROOT')
    def read(name,size): return topo.virtual_read(backend,stream,root,number(log,name),size)
    snapshot=(directory/'topology.bin').read_bytes(); count=struct.unpack_from('<I',snapshot,8)[0]; nodes=[]
    for index in range(count):
        fields=struct.unpack_from('<4IH3B3s2I',snapshot,552+index*32)
        nodes.append(dict(Id=fields[0],Header=fields[2],Buses=fields[3],Key=tuple(fields[4:8]),Parent=fields[9]))
    raw=read('KERNEL_ROUTING_INFO',992); (directory/'routing-info.bin').write_bytes(raw); native=info(raw)
    physical=native['Physical']; length=backend.qtest(stream,f'readl {physical+4:#x}')
    if not 48<=length<=65536: raise RuntimeError('Native DMAR bounds')
    table=b''.join(topo.physical_read(stream,physical+offset,min(4096,length-offset)) for offset in range(0,length,4096))
    (directory/'routing-dmar.bin').write_bytes(table); decoded=decode(table)
    if any(native[key]!=decoded[key] for key in ('HostBits','Flags','Units','Scopes')): raise RuntimeError('Native DMAR snapshot mismatch')
    request=struct.unpack('<5I',read('KERNEL_ROUTING_REQUEST',20)); result=read('KERNEL_ROUTING_RESULT',40)
    if result!=expected(native,nodes,request): raise RuntimeError('Native routing mismatch')
    for unit in native['Units']:
        # Intel VT-d5.0 11.4.4.2 GSTS.TES31; 11.4.5 RTADDR0x20: observe only, no writes.
        if backend.qtest(stream,f'readl {unit["Base"]+0x1c:#x}')&(1<<31) or backend.qtest(stream,f'readq {unit["Base"]+0x20:#x}'):
            raise RuntimeError('Translation/root unexpectedly active')
    matrix=read('KERNEL_ROUTING_SYNTHETIC_CASES',4160); (directory/'routing-synthetic.bin').write_bytes(matrix)
    for index in range(4):
        raw=matrix[index*1040:(index+1)*1040]; data=info(raw[:992]); status,reserved=struct.unpack_from('<II',raw,1032)
        try: wanted=expected(data,nodes,request)
        except ValueError:
            if index!=3 or status!=0 or raw[992:1032]!=bytes([0xa5])*40: raise RuntimeError('Ambiguity output changed')
        else:
            if index==3 or status!=2 or reserved or raw[992:1032]!=wanted: raise RuntimeError('Synthetic routing mismatch')
    unit,node,scope,kind=struct.unpack_from('<4I',result,16)
    return dict(Unit=unit,Node=node,Scope=scope,Kind=kind,RawDmarMatches=True,IndependentResolution=True,
        SyntheticCases=4,SyntheticHardwareUnits=False,TranslationActivated=False,RootUnpublished=True)

# ====== verify_trace ======

def verify_trace(directory):
    if (directory/'routing.trace').read_text().strip():
        raise RuntimeError('Routing wrote IOMMU registers')
    return dict(RegisterWrites=0)
