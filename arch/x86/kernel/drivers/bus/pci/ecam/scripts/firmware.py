"""Owned QEMU native ECAM diagnostic; independent MCFG, ECAM and legacy read checks."""
import argparse
import base64
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import uuid
# K1: layout values from arch/x86/configs/layout.h
import importlib.util as _layout_util
_layout_spec = _layout_util.spec_from_file_location("os_layout", __import__("pathlib").Path(__file__).resolve().parents[8] / "scripts/lib/layout.py")
layout = _layout_util.module_from_spec(_layout_spec)
_layout_spec.loader.exec_module(layout)

ROOT=Path(__file__).resolve().parents[8]
spec=importlib.util.spec_from_file_location('pci_test_backend',ROOT/'kernel/drivers/video/virtio/discovery/scripts/firmware.py')
backend=importlib.util.module_from_spec(spec); spec.loader.exec_module(backend)

# ====== ready ======

def ready(vm,path,negative,timeout=80):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        log=backend.protocol.read_serial(path)
        if negative and 'ECAM_INVALID' in log: return log
        if 'kernel panic' in log.lower() or 'KERNEL_EXCEPTION_HALTED' in log: raise RuntimeError('Unexpected fault')
        if 'KERNEL_READY' in log:
            if negative: raise RuntimeError('Invalid MCFG accepted')
            return log
        if vm.poll() is not None: raise RuntimeError('VM exited before result')
        time.sleep(0.005)
    raise TimeoutError('ECAM result deadline')

# ====== verify ======

def verify(stream,directory,log,absent,negative):
    number=backend.protocol.number
    if number(log,'KERNEL_ECAM_BUDGET_BEFORE')!=number(log,'KERNEL_ECAM_BUDGET_AFTER'): raise RuntimeError('ECAM budget')
    if negative:
        if 'KERNEL_ECAM_OK' in log or 'KERNEL_ECAM_VALUE:' in log: raise RuntimeError('Duplicate reached ECAM reads')
        return dict(ExpectedRejection=True,BudgetRestored=True)
    if absent:
        if 'KERNEL_ECAM_ABSENT' not in log or 'KERNEL_ECAM_OK' in log or 'KERNEL_ECAM_FILTERED_ABSENCE_TEST' not in log:
            raise RuntimeError('Missing filtered MCFG absence')
        return dict(FilteredAbsence=True,BudgetRestored=True)
    if 'KERNEL_ECAM_OK' not in log: raise RuntimeError('Missing native ECAM result')
    physical=number(log,'KERNEL_ECAM_MCFG')
    if not 0x100000<=physical<=0x8000000-556: raise RuntimeError('MCFG table outside RAM')
    length=backend.qtest(stream,f'readl {physical+4:#x}')
    if not 60<=length<=556 or (length-44)%16: raise RuntimeError('MCFG length')
    stream.write(f'b64read {physical:#x} {length:#x}\n'.encode()); stream.flush()
    fields=stream.readline(4096).decode().split()
    if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('MCFG read')
    raw=base64.b64decode(fields[1],validate=True)
    if len(raw)!=length or raw[:4]!=b'MCFG' or sum(raw)&255 or raw[8]!=1 or any(raw[36:44]): raise RuntimeError('MCFG snapshot')
    (directory/'mcfg.bin').write_bytes(raw)
    ranges=[struct.unpack_from('<QHBBI',raw,at) for at in range(44,length,16)]
    if len(ranges)!=number(log,'KERNEL_ECAM_RANGES'): raise RuntimeError('Range count')
    for pos,key in enumerate(('BASE','SEGMENT','FIRST','LAST')):
        values=[int(v,16) for v in re.findall('KERNEL_ECAM_'+key+r': 0x([0-9a-fA-F]+)',log)]
        if values!=[entry[pos] for entry in ranges]: raise RuntimeError('MCFG fields')
    segment,bus,device,function=[number(log,'KERNEL_ECAM_TARGET_'+key) for key in ('SEGMENT','BUS','DEVICE','FUNCTION')]
    matches=[entry for entry in ranges if entry[1]==segment and entry[2]<=bus<=entry[3]]
    if len(matches)!=1: raise RuntimeError('ECAM match')
    page=matches[0][0]+(bus<<20)+(device<<15)+(function<<12)
    if page!=number(log,'KERNEL_ECAM_PAGE') or page&4095: raise RuntimeError('ECAM addressing')
    values=[int(v,16) for v in re.findall(r'KERNEL_ECAM_VALUE: 0x([0-9a-fA-F]+)',log)]
    offsets=[0,8,12,24]*2
    if len(values)!=8: raise RuntimeError('ECAM read count')
    saved=backend.qtest(stream,'inl 0xcf8')
    try:
        for offset,value in zip(offsets,values):
            observed=backend.qtest(stream,f'readl {page+offset:#x}')
            if observed!=value: raise RuntimeError('Native ECAM read mismatch')
            if segment==0:
                selector=0x80000000|(bus<<16)|(device<<11)|(function<<8)|offset
                backend.qtest(stream,f'outl 0xcf8 {selector:#x}')
                if backend.qtest(stream,'inl 0xcfc')!=value: raise RuntimeError('Independent legacy PCI mismatch')
    finally: backend.qtest(stream,f'outl 0xcf8 {saved:#x}')
    pat=number(log,'KERNEL_ECAM_PAT'); cache=number(log,'KERNEL_ECAM_CACHE')
    index=((cache>>3)&3)|((cache>>5)&4)
    if cache&~0x98 or (pat>>(index*8))&255: raise RuntimeError('ECAM UC cache policy')
    table=number(log,'KERNEL_ECAM_ROOT'); address=(layout.slot_address("X86_LAYOUT_WINDOWS_SLOT")+0x20000)
    for shift in (39,30,21):
        entry=backend.qtest(stream,f'readq {table+((address>>shift)&511)*8:#x}')
        if entry&0x87!=3: raise RuntimeError('ECAM window ancestor')
        table=entry&0x000ffffffffff000
    first=(address>>12)&511
    for slot in range(first-1,first+17):
        if backend.qtest(stream,f'readq {table+slot*8:#x}'): raise RuntimeError('ECAM window not revoked or guard altered')
    return dict(Ranges=len(ranges),Segment=segment,Bus=bus,Device=device,Function=function,Page=page,
                Reads=8,ECAMAndLegacyAgree=True,BudgetRestored=True,WindowRevoked=True,Guards=True,UC=True)

# ====== boot ======

def boot(image,directory,bridge,absent,negative,topology=False,nested=False,routing=False,routing_absent=False,activation=False,dma=False):
    esp=directory/'esp/EFI/BOOT'; esp.mkdir(parents=True)
    loader=ROOT/'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    for source,name in ((image,'kernel.elf'),(loader,'BOOTX64.EFI')):
        shutil.copy2(source,esp/name)
        if backend.protocol.image_hash(source)!=backend.protocol.image_hash(esp/name): raise RuntimeError('ESP hash mismatch')
    (esp/'boot-check.txt').write_bytes(b'OS boot check\r\n')
    shutil.copy2(backend.QEMU/'share/edk2-i386-vars.fd',directory/'vars.fd')
    serial=directory/'serial.log'; vm=None
    with backend.listener() as qm,backend.listener() as qt,(directory/'stderr.log').open('w') as error:
        args=[str(backend.QEMU/'qemu-system-x86_64.exe'),'-machine','q35','-cpu','max','-accel','tcg',
            '-m','128','-smp','1','-net','none','-vga','std' if absent else 'none','-display','none','-monitor','none','-no-reboot',
            '-serial','file:'+serial.as_posix(),'-qmp',f'tcp:127.0.0.1:{qm.getsockname()[1]}',
            '-qtest',f'tcp:127.0.0.1:{qt.getsockname()[1]}',
            '-drive','if=pflash,format=raw,readonly=on,file='+(backend.QEMU/'share/edk2-x86_64-code.fd').as_posix(),
            '-drive','if=pflash,format=raw,file='+(directory/'vars.fd').as_posix(),
            '-drive','format=raw,file=fat:rw:'+(directory/'esp').as_posix()]
        if nested: args+=['-device','pcie-root-port,id=rp1,chassis=1,slot=2','-device','x3130-upstream,id=sw1,bus=rp1',
            '-device','xio3130-downstream,id=dp1,bus=sw1,chassis=2,slot=3','-device','virtio-vga,bus=dp1']
        elif bridge: args+=['-device','pcie-root-port,id=rp1,chassis=1,slot=2','-device','virtio-vga,bus=rp1']
        elif not absent: args+=['-device','virtio-vga']
        if routing:
            (directory/'routing-events.txt').write_text(ACTIVATION_EVENTS if activation else 'vtd_reg_write\n')
            args+=['-trace','events=routing-events.txt,file=routing.trace']
            if not routing_absent: args+=['-device','intel-iommu,intremap=off,caching-mode=on,device-iotlb=off']
            # Block 3: QEMU edu (hw/misc/edu.c) as the owned DMA requester at 00:05.0.
            if dma: args+=['-device','edu,addr=05.0']
        if negative:
            raw=bytearray(60); raw[:4]=b'MCFG'; struct.pack_into('<I',raw,4,60); raw[8]=1
            struct.pack_into('<QHBBI',raw,44,0x200000,0,0,0,0); raw[9]=(-sum(raw))&255
            (directory/'extra-mcfg.bin').write_bytes(raw); args+=['-acpitable','file=extra-mcfg.bin']
        (directory/'arguments.json').write_text(json.dumps(args,indent=2))
        try:
            vm=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=error,creationflags=subprocess.CREATE_NO_WINDOW,cwd=directory)
            with qm.accept()[0] as control,qt.accept()[0] as transport:
                control.settimeout(10); transport.settimeout(10)
                with control.makefile('rwb') as cs,transport.makefile('rwb') as ts:
                    if 'QMP' not in json.loads(cs.readline(65536)): raise RuntimeError('QMP greeting')
                    backend.qmp(cs,'qmp_capabilities'); started=time.monotonic()
                    log=ready(vm,serial,negative,240 if topology else 80); elapsed=time.monotonic()-started; backend.qmp(cs,'stop')
                    result=verify(ts,directory,log,absent,negative)
                    if (bridge or nested) and result['Bus']==0: raise RuntimeError('GPU was not placed behind bridge')
                    if topology:
                        target=ROOT/'arch/x86/kernel/drivers/bus/pci/topology/scripts/firmware.py'
                        topo_spec=importlib.util.spec_from_file_location('topology_verifier',target)
                        topo=importlib.util.module_from_spec(topo_spec); topo_spec.loader.exec_module(topo)
                        result['Topology']=topo.verify(backend,ts,cs,directory,log,nested)
                        result['HostReadySeconds']=elapsed
                        if routing:
                            target=ROOT/'arch/x86/kernel/drivers/memory/iommu/intel/routing/scripts/firmware.py'
                            route_spec=importlib.util.spec_from_file_location('routing_verifier',target)
                            route=importlib.util.module_from_spec(route_spec); route_spec.loader.exec_module(route)
                            result['Routing']=route.verify(backend,ts,directory,log,topo,routing_absent)
                            if dma: result['Dma']=dma_module().verify(log,result['Routing'])
                            elif activation: result['Activation']=activation_module().verify(log,result['Routing'])
                    backend.qmp(cs,'quit'); vm.wait(timeout=10)
                    if vm.returncode: raise RuntimeError('QEMU exit failure')
                    if dma: result['DmaTrace']=dma_module().verify_trace(directory,log)
                    elif activation: result['ActivationTrace']=activation_module().verify_trace(directory,log)
                    elif routing: result['RoutingTrace']=route.verify_trace(directory)
                    return dict(KernelSHA256=backend.protocol.image_hash(image),LoaderSHA256=backend.protocol.image_hash(loader),Result=result)
        finally:
            if vm and vm.poll() is None:
                vm.terminate()
                try: vm.wait(timeout=5)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait(timeout=5)

# ====== activation_module ======

# QEMU v11.1.0 hw/i386/trace-events: register writes, root pointer, enable, QI descriptors, faults, address-space switches.
ACTIVATION_EVENTS='\n'.join(('vtd_reg_write','vtd_reg_dmar_root','vtd_dmar_enable','vtd_inv_qi_enable','vtd_inv_desc_cc_global',
    'vtd_inv_desc_iotlb_global','vtd_inv_desc_wait_sw','vtd_dmar_fault','vtd_frr_new','vtd_switch_address_space'))+'\n'

def activation_module():
    target=ROOT/'arch/x86/kernel/drivers/memory/iommu/intel/activation/scripts/firmware.py'
    spec=importlib.util.spec_from_file_location('activation_verifier',target)
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module

# ====== dma_module ======

def dma_module():
    target=ROOT/'test/arch/x86/kernel/drivers/memory/iommu/intel/dma/scripts/firmware.py'
    spec=importlib.util.spec_from_file_location('dma_verifier',target)
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module

# ====== main ======

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',type=Path,default=ROOT/'kernel/build/ecam-image/kernel.elf')
    parser.add_argument('--boots',type=int,default=10)
    parser.add_argument('--bridge',action='store_true'); parser.add_argument('--absent',action='store_true')
    parser.add_argument('--duplicate',action='store_true')
    parser.add_argument('--topology',action='store_true'); parser.add_argument('--nested',action='store_true')
    parser.add_argument('--routing',action='store_true'); parser.add_argument('--routing-absent',action='store_true')
    parser.add_argument('--activation',action='store_true'); parser.add_argument('--dma',action='store_true')
    options=parser.parse_args()
    if options.activation and options.routing_absent: raise SystemExit('Activation requires a DMAR unit')
    if options.dma: options.activation=True
    if options.activation: options.routing=True
    if options.routing_absent: options.routing=True
    if options.routing: options.topology=True
    if not 1<=options.boots<=100 or sum((options.bridge,options.absent,options.duplicate,options.nested))>1 or (options.topology and (options.absent or options.duplicate)): raise SystemExit('Invalid campaign')
    if options.nested and not options.topology: raise SystemExit('Nested profile requires topology verification')
    out=ROOT/'kernel/build'/('ecam-firmware-'+uuid.uuid4().hex); out.mkdir()
    print('Evidence: '+str(out),flush=True); records=[]
    for index in range(options.boots):
        records.append(boot(options.image.resolve(),out/f'boot-{index+1}',options.bridge,options.absent,options.duplicate,options.topology,options.nested,options.routing,options.routing_absent,options.activation,options.dma))
        (out/'results.json').write_text(json.dumps(records,indent=2))
        print(f'PASS boot {index+1}: native ECAM result and budget verified',flush=True)

if __name__=='__main__': main()
