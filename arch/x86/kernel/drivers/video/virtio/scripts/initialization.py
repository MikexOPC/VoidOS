"""Owned serial-only diagnostics: negotiation, optional queues and 2D resources."""
import argparse
import base64
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid
# K1: slot indices from arch/x86/configs/layout.h
import importlib.util as _layout_util
_layout_spec = _layout_util.spec_from_file_location("os_layout", __import__("pathlib").Path(__file__).resolve().parents[7] / "scripts/lib/layout.py")
layout = _layout_util.module_from_spec(_layout_spec)
_layout_spec.loader.exec_module(layout)

ROOT=Path(__file__).resolve().parents[7]
spec=importlib.util.spec_from_file_location('vgpu_firmware',ROOT/'kernel/drivers/video/virtio/discovery/scripts/firmware.py')
backend=importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)
visual_spec=importlib.util.spec_from_file_location('vgpu_presentation',Path(__file__).with_name('presentation.py'))
visual=importlib.util.module_from_spec(visual_spec)
visual_spec.loader.exec_module(visual)
iommu_spec=importlib.util.spec_from_file_location('vgpu_iommu',Path(__file__).with_name('iommu.py'))
iommu=importlib.util.module_from_spec(iommu_spec)
iommu_spec.loader.exec_module(iommu)

# ====== boot ======

def ready(vm,serial,reject,display=None,withhold=False,limit=80):
    deadline=time.monotonic()+limit
    phase=0
    while time.monotonic()<deadline:
        log=backend.protocol.read_serial(serial)
        if display and phase<2 and f'kernel_vgpu_frame_ready: 0x{phase+1:016x}' in log.lower():
            control,transport,directory=display
            phase+=1
            if not withhold: visual.checkpoint(backend,control,transport,directory,log,phase)
        if reject and 'KERNEL_VGPU_INIT_REJECTED' in log: return log
        if 'kernel panic' in log.lower() or 'KERNEL_EXCEPTION_HALTED' in log:
            raise RuntimeError('Kernel failure: '+str(serial))
        if 'KERNEL_READY' in log: return log
        if vm.poll() is not None: raise RuntimeError('Owned VM exited before ready')
        time.sleep(0.005)
    raise TimeoutError('Kernel ready deadline: '+str(serial))

# ====== boot ======

def boot(image,directory,gpu,reject=False,queues=False,resources=False,resource_reject=False,scanout=False,visual_timeout=False,
        translated=False,revoked=False):
    esp=directory/'esp/EFI/BOOT'
    esp.mkdir(parents=True)
    loader=ROOT/'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    for source,name in ((image,'kernel.elf'),(loader,'BOOTX64.EFI')):
        shutil.copy2(source,esp/name)
        if backend.protocol.image_hash(source)!=backend.protocol.image_hash(esp/name):
            raise RuntimeError('ESP image mismatch')
    (esp/'boot-check.txt').write_bytes(b'OS boot check\r\n')
    shutil.copy2(backend.QEMU/'share/edk2-i386-vars.fd',directory/'vars.fd')
    serial=directory/'serial.log'
    vm=None
    with backend.listener() as qm,backend.listener() as qt,(directory/'monitor.log').open('w') as monitor, \
            (directory/'stderr.log').open('w') as error:
        arguments=[str(backend.QEMU/'qemu-system-x86_64.exe'),'-machine','q35','-cpu','max',
            '-accel','tcg','-m','128','-smp','1','-net','none','-vga','none']
        # V1: intel-iommu is created before the GPU it translates.
        if translated: arguments+=iommu.DEVICE+[item for event in iommu.EVENTS for item in ('-trace','enable='+event)]
        arguments+=[
            '-device',('virtio-vga,id=vgpu'+(',iommu_platform=on' if reject or translated else '')) if gpu=='virtio' else 'VGA','-display','none',
            '-monitor','stdio','-no-reboot','-serial','file:'+serial.as_posix(),
            '-qmp',f'tcp:127.0.0.1:{qm.getsockname()[1]}',
            '-qtest',f'tcp:127.0.0.1:{qt.getsockname()[1]}',
            '-trace','enable=virtio_*,file='+(directory/'status.trace').as_posix(),
            '-drive','if=pflash,format=raw,readonly=on,file='+(backend.QEMU/'share/edk2-x86_64-code.fd').as_posix(),
            '-drive','if=pflash,format=raw,file='+(directory/'vars.fd').as_posix(),
            '-drive','format=raw,file=fat:rw:'+(directory/'esp').as_posix()]
        (directory/'arguments.json').write_text(json.dumps(arguments,indent=2))
        try:
            vm=subprocess.Popen(arguments,stdin=subprocess.PIPE,stdout=monitor,stderr=error,
                text=True,creationflags=subprocess.CREATE_NO_WINDOW)
            with qm.accept()[0] as control,qt.accept()[0] as transport:
                control.settimeout(10); transport.settimeout(10)
                with control.makefile('rwb') as cs,transport.makefile('rwb') as ts:
                    if 'QMP' not in json.loads(cs.readline(65536)): raise RuntimeError('QMP greeting')
                    backend.qmp(cs,'qmp_capabilities')
                    log=ready(vm,serial,reject or resource_reject or visual_timeout,
                        (cs,ts,directory) if scanout and gpu=='virtio' and not reject else None,visual_timeout,
                        240 if translated else 80)  # V1 also scans the PCI graph, as the ECAM campaign does
                    backend.qmp(cs,'stop')
                    number=backend.protocol.number
                    root=number(log,'KERNEL_VGPU_INIT_ROOT')
                    if not 0x100000<=root<=0x08000000-4096 or root&4095: raise RuntimeError('Root outside test RAM')
                    if backend.qtest(ts,f'readq {root+layout.slot("X86_LAYOUT_GPU_SLOT")*8:#x}'): raise RuntimeError('GPU branch retained')
                    if backend.qtest(ts,f'readq {root+layout.slot("X86_LAYOUT_SURFACE_SLOT")*8:#x}'): raise RuntimeError('Surface branch retained')
                    if backend.qtest(ts,f'readq {root+layout.slot("X86_LAYOUT_DMA_SLOT")*8:#x}'): raise RuntimeError('DMA arena branch retained')
                    table=root
                    for shift in (39,30,21):
                        entry=backend.qtest(ts,f'readq {table+((0x700000020000>>shift)&511)*8:#x}')
                        if not entry&1 or entry&128: raise RuntimeError('Window table absent or huge')
                        table=entry&0x000ffffffffff000
                        if not 0x100000<=table<=0x08000000-4096: raise RuntimeError('Window table outside test RAM')
                    for slot in range(31,49):
                        if backend.qtest(ts,f'readq {table+slot*8:#x}'): raise RuntimeError('Window leaf or guard retained')
                    pci=backend.qmp(cs,'query-pci')
                    (directory/'pci.json').write_text(json.dumps(pci,indent=2))
                    devices=[dev for bus in pci for dev in bus['devices']
                        if dev['id']['vendor']==0x1af4 and dev['id']['device']==0x1050]
                    if number(log,'KERNEL_VGPU_INIT_BUDGET_BEFORE')!=number(log,'KERNEL_VGPU_INIT_BUDGET_AFTER'):
                        raise RuntimeError('GPU lease budget mismatch')
                    if gpu=='virtio':
                        marker='KERNEL_VGPU_INIT_REJECTED' if reject or resource_reject or visual_timeout else 'KERNEL_VGPU_INIT_OK'
                        if len(devices)!=1 or marker not in log: raise RuntimeError('Missing GPU result')
                        expected_status,expected_fault=(3,5) if reject else (11,0)
                        if number(log,'KERNEL_VGPU_INIT_STATUS')!=expected_status or number(log,'KERNEL_VGPU_INIT_FAULT')!=expected_fault:
                            raise RuntimeError('Feature negotiation failed')
                        if number(log,'KERNEL_VGPU_INIT_ACCEPTED')!=(iommu.ACCEPTED if translated else 1<<32): raise RuntimeError('Wrong accepted features')
                        if queues and not reject and not resources:
                            if 'KERNEL_VGPU_QUEUE_OK' not in log or number(log,'KERNEL_VGPU_QUEUE_COUNT')!=256 or number(log,'KERNEL_VGPU_QUEUE_FAULT'):
                                raise RuntimeError('Missing real queue completions')
                            physical=number(log,'KERNEL_VGPU_QUEUE_RAM')
                            if physical&4095 or not 0x100000<=physical<=0x08000000-16384:
                                raise RuntimeError('DMA buffer outside test RAM')
                            ts.write(f'b64read {physical:#x} 0x4000\n'.encode()); ts.flush()
                            fields=ts.readline(65536).decode().split()
                            if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('DMA RAM read failed')
                            raw=base64.b64decode(fields[1],validate=True)
                            if len(raw)!=16384: raise RuntimeError('Incomplete DMA RAM')
                            (directory/'dma-ram.bin').write_bytes(raw)
                            integer=lambda offset,length: int.from_bytes(raw[offset:offset+length],'little')
                            if integer(0,8)!=physical+8192 or integer(16,8)!=physical+12288 or integer(130,2)!=256 or integer(162,2)!=256:
                                raise RuntimeError('DMA ring/addresses mismatch')
                            if integer(8192,4)!=0x100 or integer(8200,8)!=256 or integer(12288,4)!=0x1101 or integer(12296,8)!=256:
                                raise RuntimeError('DMA request/response fence mismatch')
                            for slot in range(8):
                                if integer(164+slot*8,4)!=0 or integer(168+slot*8,4)!=408:
                                    raise RuntimeError('DMA used ring mismatch')
                        if resources and not reject:
                            resource_marker='KERNEL_VGPU_RESOURCE_FAILED' if resource_reject or visual_timeout else 'KERNEL_VGPU_RESOURCE_OK'
                            expected_state,expected_count,expected_fault=(3,6,0) if visual_timeout else (0xffffffff,3,4) if resource_reject else (5,11 if scanout else 6,0)
                            if revoked: expected_count,expected_fault=7,4  # 6 of the first run, CREATE of the second; ATTACH refused
                            if resource_marker not in log or number(log,'KERNEL_VGPU_RESOURCE_STATE')!=expected_state or number(log,'KERNEL_VGPU_RESOURCE_COUNT')!=expected_count or number(log,'KERNEL_VGPU_RESOURCE_FAULT')!=expected_fault:
                                raise RuntimeError('Resource lifecycle failure')
                            width=number(log,'KERNEL_VGPU_RESOURCE_WIDTH'); height=number(log,'KERNEL_VGPU_RESOURCE_HEIGHT')
                            physical=number(log,'KERNEL_VGPU_RESOURCE_RAM'); length=number(log,'KERNEL_VGPU_RESOURCE_BYTES')
                            if length!=width*height*4 or not 1<=length<=16*1024*1024 or physical&4095 or not 0x100000<=physical<=0x08000000-length:
                                raise RuntimeError('Backing geometry outside test RAM')
                            for index in (0,1,width-1,width,(length//4)//2,length//4-1):
                                x,y=index%width,index//width
                                expected_pixel=(0xff000000|(((x//32)&255)<<16)|(((y//16)&255)<<8)|((x^y)&255)) if scanout else (0xff000000|((index*2654435761)&0xffffff))
                                if backend.qtest(ts,f'readl {physical+index*4:#x}') != expected_pixel:
                                    raise RuntimeError('Backing pixel mismatch')
                            if scanout and not visual_timeout and ('KERNEL_VGPU_SCANOUT_OK' not in log or not (directory/'frame-2.ppm').exists()):
                                raise RuntimeError('Missing visual verification')
                            tables=[int(value,16) for value in re.findall(r'KERNEL_VGPU_RESOURCE_TABLE: 0x([0-9a-fA-F]+)',log)]
                            pages=(length+4095)//4096
                            if len(tables)!=2+(pages+2+511)//512 or len(set(tables))!=len(tables):
                                raise RuntimeError('Surface table count mismatch')
                            for level,table in enumerate(tables):
                                if table&4095 or not 0x100000<=table<=0x08000000-4096:
                                    raise RuntimeError('Surface table outside test RAM')
                                ts.write(f'b64read {table:#x} 0x1000\n'.encode()); ts.flush()
                                fields=ts.readline(16384).decode().split()
                                if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('Surface table read failed')
                                raw=base64.b64decode(fields[1],validate=True)
                                if len(raw)!=4096: raise RuntimeError('Incomplete surface table')
                                (directory/f'surface-table-{level}.bin').write_bytes(raw)
                                for slot in range(512):
                                    expected=0
                                    if level==0 and slot==0: expected=tables[1]|3
                                    if level==1 and slot<len(tables)-2: expected=tables[slot+2]|3
                                    index=(level-2)*512+slot
                                    if level>=2 and 1<=index<=pages: expected=(physical+(index-1)*4096)|(1<<63)|3
                                    if int.from_bytes(raw[slot*8:slot*8+8],'little')&~0x60!=expected:
                                        raise RuntimeError('Retired surface PTE/guard mismatch')
                        config=backend.snapshot(ts,devices[0]); cursor=config[0x34]; common=None; visited=set()
                        while cursor:
                            if cursor in visited: raise RuntimeError('Capability cycle')
                            visited.add(cursor)
                            if config[cursor]==9 and config[cursor+3]==1:
                                bar=config[cursor+4]
                                regions=[item for item in devices[0]['regions'] if item['bar']==bar]
                                if len(regions)!=1: raise RuntimeError('Common BAR')
                                common=regions[0]['address']+int.from_bytes(config[cursor+8:cursor+12],'little')
                                break
                            cursor=config[cursor+1]
                        if common!=number(log,'KERNEL_VGPU_INIT_PHYSICAL'): raise RuntimeError('Common address mismatch')
                        status=backend.qtest(ts,f'readb {common+20:#x}')
                        if status: raise RuntimeError('Device reset not observed')
                    elif devices or 'KERNEL_VGPU_INIT_ABSENT' not in log: raise RuntimeError('Fallback mismatch')
                    backend.qmp(cs,'quit'); vm.wait(timeout=10)
                    if vm.returncode: raise RuntimeError('QEMU exit failure')
                    trace=(directory/'status.trace').read_text(errors='replace')
                    if gpu=='virtio':
                        events=[line for line in trace.splitlines() if 'virtio_set_status' in line]
                        (directory/'status-events.json').write_text(json.dumps(events,indent=2))
                        statuses=[int(re.search(r' val (\d+)$',line)[1]) for line in events]
                        transitions=[value for index,value in enumerate(statuses) if not index or value!=statuses[index-1]]
                        expected=[0,1,3,11,131,0] if reject else ([0,1,3,11,15,0] if queues else [0,1,3,11,0])
                        if transitions[-len(expected):]!=expected: raise RuntimeError('Independent status trace mismatch')
                        if translated: result_iommu=iommu.verify(backend.protocol.read_serial(serial),trace,revoked)
                        if resources and not reject and not resource_reject and not visual_timeout and not revoked:
                            names=['virtio_gpu_cmd_res_create_2d','virtio_gpu_cmd_res_back_attach',
                                'virtio_gpu_cmd_res_xfer_toh_2d','virtio_gpu_cmd_res_back_detach','virtio_gpu_cmd_res_unref']
                            expected_names=list(names)
                            if scanout:
                                names+=['virtio_gpu_cmd_set_scanout','virtio_gpu_cmd_res_flush']
                                expected_names=[names[0],names[1],names[2],names[5],names[6],names[2],names[6],names[5],names[3],names[4]]
                            observed=[name for line in trace.splitlines() for name in names if name in line]
                            if observed[-len(expected_names):]!=expected_names: raise RuntimeError('Device resource command trace mismatch')
                    return dict(Iommu=result_iommu if translated else None,KernelSHA256=backend.protocol.image_hash(image),LoaderSHA256=backend.protocol.image_hash(loader),
                        GPU=gpu,Ready=not(reject or resource_reject or visual_timeout),ExpectedRejection=reject or resource_reject or visual_timeout,Unmapped=True,ResetObserved=gpu=='virtio',
                        QueueCompletions=(262 if visual_timeout else 259 if resource_reject else 267 if scanout else 262 if resources else 256) if queues and gpu=='virtio' and not reject else 0,
                        Resources=resources and gpu=='virtio' and not reject,ResourceRejected=resource_reject,WindowRevoked=True,
                        VisualFrames=2 if scanout and gpu=='virtio' and not reject and not visual_timeout else 0,VisualTimeout=visual_timeout)
        finally:
            if vm and vm.poll() is None:
                vm.terminate()
                try: vm.wait(timeout=5)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait(timeout=5)

# ====== main ======

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',type=Path,default=ROOT/'kernel/build/virtio-initialization-image/kernel.elf')
    parser.add_argument('--gpu',choices=('virtio','std'),default='virtio')
    parser.add_argument('--boots',type=int,default=10)
    parser.add_argument('--require-access-platform',action='store_true')
    parser.add_argument('--queues',action='store_true')
    parser.add_argument('--resources',action='store_true')
    parser.add_argument('--resource-reject',action='store_true')
    parser.add_argument('--scanout',action='store_true')
    parser.add_argument('--scanout-timeout',action='store_true')
    parser.add_argument('--iommu',action='store_true'); parser.add_argument('--iommu-revoke',action='store_true')
    options=parser.parse_args()
    if options.scanout_timeout:
        if options.gpu!='virtio' or options.require_access_platform: raise SystemExit('Visual timeout requires GPU presentation')
        options.scanout=True
    if options.scanout:
        if options.resource_reject: raise SystemExit('Choose visual or resource rejection')
        options.resources=True
    if options.resource_reject:
        if options.require_access_platform or options.gpu!='virtio': raise SystemExit('Resource rejection requires a separate GPU test')
        options.resources=True
    if options.iommu_revoke:
        if options.scanout: raise SystemExit('Revocation runs on the resource path')
        options.iommu=True
    if options.iommu:
        if options.require_access_platform or options.resource_reject or options.scanout_timeout or options.gpu!='virtio':
            raise SystemExit('V1 needs the positive GPU profile')
        options.resources=True
    if options.resources: options.queues=True
    if not 1<=options.boots<=100: raise SystemExit('Invalid boot count')
    if options.require_access_platform and options.gpu!='virtio': raise SystemExit('GPU required for negative test')
    out=ROOT/'kernel/build'/('virtio-initialization-firmware-'+uuid.uuid4().hex)
    out.mkdir(); print('Evidence: '+str(out),flush=True)
    records=[]
    for index in range(options.boots):
        records.append(boot(options.image.resolve(),out/f'boot-{index+1}',options.gpu,options.require_access_platform,options.queues,options.resources,options.resource_reject,options.scanout,options.scanout_timeout,
            options.iommu,options.iommu_revoke))
        (out/'results.json').write_text(json.dumps(records,indent=2))
        print(f'PASS boot {index+1}: negotiation/reset or absent GPU, branch revoked',flush=True)

if __name__=='__main__': main()
