"""Independent QMP PCI graph and raw snapshot verification for the topology diagnostic."""
import base64
import json
import struct

# ====== physical_read ======

def physical_read(stream,address,size):
    if not 0x100000<=address<=0x8000000-size or not 1<=size<=4096: raise RuntimeError('Snapshot RAM bounds')
    stream.write(f'b64read {address:#x} {size:#x}\n'.encode()); stream.flush()
    fields=stream.readline(8192).decode().split()
    if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('Snapshot read')
    data=base64.b64decode(fields[1],validate=True)
    if len(data)!=size: raise RuntimeError('Short snapshot')
    return data

# ====== virtual_read ======

def virtual_read(backend,stream,root,virtual,size):
    result=bytearray()
    while size:
        table=root
        for shift in (39,30,21,12):
            entry=backend.qtest(stream,f'readq {table+((virtual>>shift)&511)*8:#x}')
            if not entry&1 or entry&4 or (shift!=12 and entry&128): raise RuntimeError('Snapshot page mapping')
            table=entry&0x000ffffffffff000
        length=min(size,4096-(virtual&4095))
        result.extend(physical_read(stream,table+(virtual&4095),length))
        virtual+=length; size-=length
    return bytes(result)

# ====== verify ======

def verify(backend,stream,control,directory,log,nested):
    number=backend.protocol.number
    if 'KERNEL_PCI_TOPOLOGY_OK' not in log: raise RuntimeError('Missing topology result')
    raw=virtual_read(backend,stream,number(log,'KERNEL_ECAM_ROOT'),number(log,'KERNEL_PCI_TOPOLOGY_SNAPSHOT'),16936)
    (directory/'topology.bin').write_bytes(raw)
    signature,count,bridges,reads=struct.unpack_from('<QIIQ',raw)
    if signature!=0x3159504f54494350 or count>512 or bridges>64 or not 0<reads<=150000: raise RuntimeError('Topology header')
    if (count,bridges,reads)!=tuple(number(log,'KERNEL_PCI_TOPOLOGY_'+key) for key in ('COUNT','BRIDGES','READS')): raise RuntimeError('Topology logged metadata')
    mcfg=(directory/'mcfg.bin').read_bytes()
    physical,ranges,bits=struct.unpack_from('<QII',raw,24)
    if physical!=number(log,'KERNEL_ECAM_MCFG') or ranges!=(len(mcfg)-44)//16 or not 32<=bits<=52: raise RuntimeError('Topology MCFG metadata')
    if raw[40:40+ranges*16]!=mcfg[44:] or any(raw[40+ranges*16:552]): raise RuntimeError('Topology MCFG contents')
    nodes=[]
    for index in range(count):
        fields=struct.unpack_from('<4IH3B3s2I',raw,552+index*32)
        if fields[8]!=bytes(3): raise RuntimeError('Node reserved bytes')
        nodes.append(dict(Id=fields[0],Class=fields[1],Header=fields[2],Buses=fields[3],
            Key=tuple(fields[4:8]),Parent=fields[9],Depth=fields[10]))
    if any(raw[552+count*32:]): raise RuntimeError('Unused topology entries not zero')
    hardware=backend.qmp(control,'query-pci'); (directory/'qmp-pci.json').write_text(json.dumps(hardware,indent=2))
    actual={}
    def walk(devices,parent,depth):
        for device in devices:
            key=(0,device['bus'],device['slot'],device['function'])
            if key in actual: raise RuntimeError('QMP duplicate device')
            actual[key]=dict(Device=device,Parent=parent,Depth=depth)
            if 'pci_bridge' in device: walk(device['pci_bridge'].get('devices',[]),key,depth+1)
    for bus in hardware: walk(bus['devices'],None,0)
    if len(actual)!=len(nodes) or {n['Key'] for n in nodes}!=set(actual): raise RuntimeError('Incomplete or foreign device snapshot')
    saved=backend.qtest(stream,'inl 0xcf8')
    try:
        for node in nodes:
            _,bus,device,function=node['Key']; expected=actual[node['Key']]
            expected_parent=expected['Parent']; parent=node['Parent']
            observed_parent=None if parent==0xffffffff else nodes[parent]['Key'] if parent<len(nodes) else 'invalid'
            if observed_parent!=expected_parent or node['Depth']!=expected['Depth']: raise RuntimeError('Bridge ancestry mismatch')
            for offset,key in ((0,'Id'),(8,'Class'),(12,'Header')):
                selector=0x80000000|(bus<<16)|(device<<11)|(function<<8)|offset
                backend.qtest(stream,f'outl 0xcf8 {selector:#x}')
                if backend.qtest(stream,'inl 0xcfc')!=node[key]: raise RuntimeError('PCI field mismatch')
            if 'pci_bridge' in expected['Device']:
                backend.qtest(stream,f'outl 0xcf8 {0x80000018|(bus<<16)|(device<<11)|(function<<8):#x}')
                if backend.qtest(stream,'inl 0xcfc')!=node['Buses']: raise RuntimeError('Bridge bus register mismatch')
            elif node['Buses']: raise RuntimeError('Endpoint has bridge metadata')
    finally: backend.qtest(stream,f'outl 0xcf8 {saved:#x}')
    if sum('pci_bridge' in value['Device'] for value in actual.values())!=bridges: raise RuntimeError('Bridge count mismatch')
    maximum=max((node['Depth'] for node in nodes),default=0)
    if nested and maximum<3: raise RuntimeError('Nested bridge profile not reached')
    return dict(Functions=count,Bridges=bridges,Reads=reads,MaximumDepth=maximum,QMPGraphMatches=True,RawSnapshotVerified=True)
