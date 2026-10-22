"""Read physical paging structures, never GPU registers, in an owned VM."""
import argparse
import base64
import importlib.util
import json
from pathlib import Path
import re
import struct
import time
import uuid

ROOT=Path(__file__).resolve().parents[7]
spec=importlib.util.spec_from_file_location('vgpu_firmware',ROOT/'kernel/drivers/video/virtio/discovery/scripts/firmware.py')
backend=importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)

# ====== read_page ======

def read_page(stream, physical, directory, label):
    if physical & 4095 or not 0x100000<=physical<=0x08000000-4096:
        raise RuntimeError('Table outside ordinary RAM of the 128MiB test VM')
    stream.write(f'b64read {physical:#x} 0x1000\n'.encode())
    stream.flush()
    fields=stream.readline(16384).decode().split()
    if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('QTest read failed')
    raw=base64.b64decode(fields[1],validate=True)
    if len(raw)!=4096: raise RuntimeError('Incomplete physical table')
    (directory/(label+'.bin')).write_bytes(raw)
    return struct.unpack('<512Q',raw)

# ====== pci_pages ======

def pci_pages(config):
    bars=struct.unpack_from('<6I',config,16)
    cursor=config[0x34]
    visited=set(); ranges={}
    while cursor:
        if cursor in visited or cursor<64 or cursor>252 or cursor%4: raise RuntimeError('PCI chain')
        visited.add(cursor)
        if config[cursor]==9 and config[cursor+3] in (1,2,3,4):
            kind=config[cursor+3]; bar=config[cursor+4]
            base=bars[bar]&~15
            if bars[bar]&6==4: base|=bars[bar+1]<<32
            offset,length=struct.unpack_from('<II',config,cursor+8)
            if kind not in ranges: ranges[kind]=(base+offset,length)
        cursor=config[cursor+1]
    if len(ranges)!=4: raise RuntimeError('Missing GPU regions')
    pages=set()
    for address,length in ranges.values():
        pages.update(range(address//4096,(address+length-1)//4096+1))
    return sorted(page*4096 for page in pages)

# ====== verify ======

def verify(stream,log,directory,gpu,diagnostic):
    number=backend.protocol.number
    if gpu=='std':
        root=number(log,'KERNEL_FB_ROOT')
        pml4=read_page(stream,root,directory,'pml4')
        if pml4[226] or 'KERNEL_VGPU_MAPPING_ABSENT' not in log: raise RuntimeError('GPU fallback leaked mapping')
        result=dict(Present=False,NoAllocation=True)
    else:
        if 'KERNEL_VGPU_MAPPING_LIVE' not in log: raise RuntimeError('Missing live mapping')
        if diagnostic and 'KERNEL_VGPU_ROLLBACK_OK' not in log: raise RuntimeError('Missing bind/unbind/rebind proof')
        root=number(log,'KERNEL_VGPU_MAPPING_ROOT')
        cache=number(log,'KERNEL_VGPU_MAPPING_CACHE'); pat=number(log,'KERNEL_VGPU_MAPPING_PAT')
        virtual=number(log,'KERNEL_VGPU_MAPPING_VIRTUAL')
        count=number(log,'KERNEL_VGPU_MAPPING_PAGES')
        tables=[int(v,16) for v in re.findall(r'KERNEL_VGPU_MAPPING_TABLE: 0x([0-9a-fA-F]+)',log)]
        physical=pci_pages((directory/'config.bin').read_bytes())
        logged=[int(v,16) for v in re.findall(r'KERNEL_VGPU_MAPPING_PHYSICAL: 0x([0-9a-fA-F]+)',log)]
        if virtual!=(226<<39)+4096 or count!=len(physical) or not 1<=count<=16 or logged!=physical or len(tables)!=3:
            raise RuntimeError('GPU plan mismatch')
        index=((cache>>3)&3)|((cache>>5)&4)
        if cache & ~0x98 or pat>>(index*8)&255 or pat&255!=6: raise RuntimeError('GPU cache policy')
        pml4=read_page(stream,root,directory,'pml4')
        if pml4[226]&~0x60 != tables[0]|3: raise RuntimeError('Foreign GPU branch')
        for level,table in enumerate(tables):
            entries=read_page(stream,table,directory,'gpu-table-'+str(level))
            for slot,entry in enumerate(entries):
                expected=0
                if level<2 and slot==0: expected=tables[level+1]|3
                if level==2 and 1<=slot<=count: expected=physical[slot-1]|cache|(1<<63)|3
                if entry&~0x60 != expected: raise RuntimeError(f'GPU table mismatch {level}:{slot}')
        result=dict(Present=True,Root=root,Tables=tables,Pages=physical,Guards=True,UC=True,Supervisor=True,NX=True,Rollback=diagnostic)
    (directory/'mapping.json').write_text(json.dumps(result,indent=2))

# ====== main ======

def exercise(vm,serial):
    backend.protocol.command(vm,'mouse_move 4 4')
    time.sleep(0.05)

# ====== main ======

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',type=Path,default=ROOT/'kernel/build/virtio-mapping-image/kernel.elf')
    parser.add_argument('--boots',type=int,default=10)
    parser.add_argument('--gpu',choices=('virtio','std'),default='virtio')
    parser.add_argument('--ordinary',action='store_true')
    options=parser.parse_args()
    if not 1<=options.boots<=100: raise SystemExit('Invalid boot count')
    out=ROOT/'kernel/build'/('virtio-mapping-firmware-'+uuid.uuid4().hex)
    out.mkdir(); print('Evidence: '+str(out),flush=True)
    records=[]
    for index in range(options.boots):
        directory=out/f'boot-{index+1}'
        record=backend.boot(options.image.resolve(),ROOT/'kernel/build/virtio-pci-host/host.exe',directory,options.gpu,
            lambda stream,log,path,gpu:verify(stream,log,path,gpu,not options.ordinary),exercise)
        record['Mapping']=json.loads((directory/'mapping.json').read_text())
        record['MouseMoves']=backend.protocol.number(backend.protocol.read_serial(directory/'serial.log'),'KERNEL_POINTER_MOVES')
        if not record['MouseMoves']: raise RuntimeError('No mouse input consumed with GPU mapping')
        records.append(record)
        (out/'results.json').write_text(json.dumps(records,indent=2))
        print(f'PASS boot {index+1}: mapping, guards, input and cleanup',flush=True)

if __name__=='__main__': main()
