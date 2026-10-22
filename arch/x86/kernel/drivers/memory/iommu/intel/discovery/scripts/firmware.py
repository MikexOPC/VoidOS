"""Owned QEMU DMAR discovery: table snapshots and guest/host comparison, no DMA."""
import argparse
import base64
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import uuid

ROOT=Path(__file__).resolve().parents[9]
spec=importlib.util.spec_from_file_location('firmware_backend',ROOT/'kernel/drivers/video/virtio/discovery/scripts/firmware.py')
backend=importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)
register_spec=importlib.util.spec_from_file_location('vtd_register_verify',ROOT/'arch/x86/kernel/drivers/memory/iommu/intel/registers/scripts/firmware.py')
register_verify=importlib.util.module_from_spec(register_spec)
register_spec.loader.exec_module(register_verify)
domain_spec=importlib.util.spec_from_file_location('vtd_domain_verify',ROOT/'arch/x86/kernel/drivers/memory/iommu/intel/domains/scripts/firmware.py')
domain_verify=importlib.util.module_from_spec(domain_spec)
domain_spec.loader.exec_module(domain_verify)
qi_spec=importlib.util.spec_from_file_location('vtd_qi_verify',ROOT/'arch/x86/kernel/drivers/memory/iommu/intel/invalidation/scripts/firmware.py')
qi_verify=importlib.util.module_from_spec(qi_spec)
qi_spec.loader.exec_module(qi_verify)

# ====== ready ======

def ready(vm,serial,negative,ram=False,qi_negative=False):
    deadline=time.monotonic()+80
    while time.monotonic()<deadline:
        log=backend.protocol.read_serial(serial)
        if negative and ('QI_INVALID' if qi_negative else 'VTD_REGISTERS_INVALID' if ram else 'DMAR_DISCOVERY_INVALID') in log: return log
        if 'kernel panic' in log.lower() or 'KERNEL_EXCEPTION_HALTED' in log: raise RuntimeError('Unexpected kernel fault')
        if 'KERNEL_READY' in log:
            if negative: raise RuntimeError('Invalid DMAR accepted')
            return log
        if vm.poll() is not None: raise RuntimeError('VM exited before result')
        time.sleep(0.005)
    raise TimeoutError('Guest result deadline')

# ====== duplicate ======

def duplicate(path,ram=False):
    data=bytearray(64)
    data[:4]=b'DMAR'; struct.pack_into('<I',data,4,len(data)); data[8]=1; data[36]=39
    struct.pack_into('<HHBBHQ',data,48,0,16,1,0,0,0x200000 if ram else 0xfed91000)
    data[9]=(-sum(data))&255
    path.write_bytes(data)

# ====== compare ======

def compare(stream,directory,log):
    number=backend.protocol.number
    physical=number(log,'KERNEL_DMAR_PHYSICAL')
    if not 0x100000<=physical<=0x08000000-48: raise RuntimeError('DMAR outside test RAM')
    length=backend.qtest(stream,f'readl {physical+4:#x}')
    if not 48<=length<=65536 or physical+length>0x08000000: raise RuntimeError('DMAR length outside RAM')
    stream.write(f'b64read {physical:#x} {length:#x}\n'.encode()); stream.flush()
    fields=stream.readline(100000).decode().split()
    if len(fields)!=2 or fields[0]!='OK': raise RuntimeError('DMAR read failure')
    raw=base64.b64decode(fields[1],validate=True)
    if len(raw)!=length or raw[:4]!=b'DMAR' or sum(raw)&255: raise RuntimeError('Invalid table snapshot')
    path=directory/'dmar.bin'; path.write_bytes(raw)
    host=ROOT/'kernel/build/iommu-discovery-host/host.exe'
    environment=os.environ.copy()
    toolchains=sorted(Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC').glob('*/bin/Hostx64/x64'))
    if not toolchains: raise RuntimeError('ASan runtime toolchain unavailable')
    environment['PATH']=str(toolchains[-1])+os.pathsep+environment.get('PATH','')
    result=subprocess.run([str(host),str(path)],capture_output=True,text=True,timeout=20,env=environment)
    (directory/'host.txt').write_text(result.stdout+result.stderr)
    result.check_returncode()
    matched=re.search(r'READY host=(\d+) flags=(\d+) units=(\d+) scopes=(\d+) other=(\d+)',result.stdout)
    if not matched: raise RuntimeError('Missing host decode')
    values=[number(log,'KERNEL_DMAR_'+name) for name in ('HOST_BITS','FLAGS','UNITS','SCOPES','OTHER')]
    if values!=[int(value) for value in matched.groups()]: raise RuntimeError('Guest/host inventory mismatch')
    units=[]; at=48
    while at<len(raw):
        kind,size=struct.unpack_from('<HH',raw,at)
        if size<4 or at+size>len(raw): raise RuntimeError('Invalid record envelope')
        if kind==0:
            flags,scale,segment,base=struct.unpack_from('<BBHQ',raw,at+4)
            units.append(dict(Base=base,Bytes=4096<<scale,Segment=segment,Flags=flags))
        at+=size
    for key in ('BASE','BYTES','SEGMENT','FLAGS'):
        observed=[int(value,16) for value in re.findall(r'KERNEL_DMAR_UNIT_'+key+r': 0x([0-9a-fA-F]+)',log)]
        if observed!=[unit[key.title()] for unit in units]: raise RuntimeError('DRHD fields mismatch')
    return dict(Physical=physical,Length=length,HostBits=values[0],Units=units,Scopes=values[3],OtherRecords=values[4])

# ====== boot ======

def boot(image,directory,present,negative,registers=False,ram=False,domains=False,gpu_absent=False,qi=False,qi_negative=False):
    esp=directory/'esp/EFI/BOOT'; esp.mkdir(parents=True)
    loader=ROOT/'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    for source,name in ((image,'kernel.elf'),(loader,'BOOTX64.EFI')):
        shutil.copy2(source,esp/name)
        if backend.protocol.image_hash(source)!=backend.protocol.image_hash(esp/name): raise RuntimeError('ESP hash mismatch')
    (esp/'boot-check.txt').write_bytes(b'OS boot check\r\n')
    shutil.copy2(backend.QEMU/'share/edk2-i386-vars.fd',directory/'vars.fd')
    serial=directory/'serial.log'; vm=None
    with backend.listener() as qm,backend.listener() as qt,(directory/'stderr.log').open('w') as error:
        args=[str(backend.QEMU/'qemu-system-x86_64.exe'),'-machine','q35','-cpu','max,-clflush' if qi_negative else 'max','-accel','tcg',
            '-m','128','-smp','1','-net','none','-vga','none' if domains and not gpu_absent else 'std','-display','none','-monitor','none','-no-reboot',
            '-serial','file:'+serial.as_posix(),'-qmp',f'tcp:127.0.0.1:{qm.getsockname()[1]}',
            '-qtest',f'tcp:127.0.0.1:{qt.getsockname()[1]}',
            '-drive','if=pflash,format=raw,readonly=on,file='+(backend.QEMU/'share/edk2-x86_64-code.fd').as_posix(),
            '-drive','if=pflash,format=raw,file='+(directory/'vars.fd').as_posix(),
            '-drive','format=raw,file=fat:rw:'+(directory/'esp').as_posix()]
        if qi:
            (directory/'qi-events.txt').write_text('vtd_reg_write\nvtd_reg_write_gcmd\nvtd_inv_qi_enable\nvtd_inv_desc_cc_global\nvtd_inv_desc_iotlb_global\nvtd_inv_desc_wait_sw\n')
            args+=['-trace','events=qi-events.txt,file=qi.trace']
        if domains and not gpu_absent: args+=['-device','virtio-vga']
        if present: args+=['-device','intel-iommu,intremap=off,caching-mode=on,device-iotlb=off']
        if negative and not qi_negative:
            extra=directory/'duplicate-dmar.bin'; duplicate(extra,ram); args+=['-acpitable','file=duplicate-dmar.bin']
        (directory/'arguments.json').write_text(json.dumps(args,indent=2))
        try:
            vm=subprocess.Popen(args,stdout=subprocess.DEVNULL,stderr=error,creationflags=subprocess.CREATE_NO_WINDOW,
                cwd=directory if negative or qi else None)
            with qm.accept()[0] as control,qt.accept()[0] as transport:
                control.settimeout(10); transport.settimeout(10)
                with control.makefile('rwb') as cs,transport.makefile('rwb') as ts:
                    if 'QMP' not in json.loads(cs.readline(65536)): raise RuntimeError('QMP greeting')
                    backend.qmp(cs,'qmp_capabilities'); log=ready(vm,serial,negative,ram,qi_negative); backend.qmp(cs,'stop')
                    number=backend.protocol.number
                    if number(log,'KERNEL_DMAR_BUDGET_BEFORE')!=number(log,'KERNEL_DMAR_BUDGET_AFTER'): raise RuntimeError('DMAR budget mismatch')
                    if negative:
                        if 'KERNEL_READY' in log or (not ram and not qi_negative and 'KERNEL_DMAR_DISCOVERY_OK' in log): raise RuntimeError('Invalid source accepted')
                        if qi_negative:
                            if number(log,'KERNEL_QI_BUDGET_BEFORE')!=number(log,'KERNEL_QI_BUDGET_AFTER') or 'KERNEL_QI_OK' in log: raise RuntimeError('QI unsupported feature cleanup')
                            base=number(log,'KERNEL_DMAR_UNIT_BASE')
                            for offset in (0x1c,0x80,0x88,0x90):
                                if backend.qtest(ts,f'{"readl" if offset==0x1c else "readq"} {base+offset:#x}'): raise RuntimeError('QI feature rejection changed device')
                        if ram:
                            if number(log,'KERNEL_VTD_BUDGET_BEFORE')!=number(log,'KERNEL_VTD_BUDGET_AFTER'): raise RuntimeError('RAM source rollback failed')
                        result=dict(ExpectedRejection=True,RamSource=ram,QIFeatureRejected=qi_negative)
                    elif present:
                        if 'KERNEL_DMAR_DISCOVERY_OK' not in log: raise RuntimeError('Missing DMAR result')
                        result=compare(ts,directory,log)
                        if registers and not domains: result['Mapping']=register_verify.verify(backend,ts,directory,log,result)
                        if domains:
                            if gpu_absent:
                                if ('KERNEL_QI_ABSENT' if qi else 'KERNEL_VTD_DOMAIN_ABSENT') not in log: raise RuntimeError('Missing domain absence')
                            elif qi: result['QI']=qi_verify.verify(backend,ts,directory,log,result)
                            else: result['Domain']=domain_verify.verify(backend,ts,directory,log,result)
                    else:
                        if 'KERNEL_DMAR_ABSENT' not in log: raise RuntimeError('Missing absence result')
                        if registers and 'KERNEL_VTD_ABSENT' not in log: raise RuntimeError('Missing register absence result')
                        if domains and ('KERNEL_QI_ABSENT' if qi else 'KERNEL_VTD_DOMAIN_ABSENT') not in log: raise RuntimeError('Missing domain absence result')
                        result=dict(Absent=True)
                    backend.qmp(cs,'quit'); vm.wait(timeout=10)
                    if vm.returncode: raise RuntimeError('QEMU exit failure')
                    if qi:
                        result['QITrace']=qi_verify.verify_trace(directory,present and not negative and not gpu_absent)
                    return dict(KernelSHA256=backend.protocol.image_hash(image),LoaderSHA256=backend.protocol.image_hash(loader),
                        IommuPresent=present,Ready=not negative,BudgetRestored=True,Discovery=result)
        finally:
            if vm and vm.poll() is None:
                vm.terminate()
                try: vm.wait(timeout=5)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait(timeout=5)

# ====== main ======

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',type=Path)
    parser.add_argument('--boots',type=int,default=10)
    parser.add_argument('--absent',action='store_true')
    parser.add_argument('--duplicate',action='store_true')
    parser.add_argument('--registers',action='store_true')
    parser.add_argument('--ram-source',action='store_true')
    parser.add_argument('--domains',action='store_true')
    parser.add_argument('--qi',action='store_true')
    parser.add_argument('--qi-no-clflush',action='store_true')
    parser.add_argument('--gpu-absent',action='store_true')
    options=parser.parse_args()
    if not 1<=options.boots<=100 or sum((options.absent,options.duplicate,options.ram_source))>1: raise SystemExit('Invalid campaign')
    if options.qi_no_clflush and (options.absent or options.duplicate or options.ram_source or options.gpu_absent): raise SystemExit('Choose a dedicated QI CPU rejection campaign')
    if options.qi_no_clflush: options.qi=True
    if options.qi: options.domains=True
    if options.domains: options.registers=True
    if options.gpu_absent and not options.domains: raise SystemExit('GPU absence requires domains')
    if options.ram_source and not options.registers: raise SystemExit('RAM-source rejection requires registers')
    if not options.image: options.image=ROOT/'kernel/build'/('iommu-qi-image' if options.qi else 'iommu-domains-image' if options.domains else 'iommu-registers-image' if options.registers else 'iommu-discovery-image')/'kernel.elf'
    out=ROOT/'kernel/build'/('iommu-discovery-firmware-'+uuid.uuid4().hex); out.mkdir()
    print('Evidence: '+str(out),flush=True); records=[]
    for index in range(options.boots):
        records.append(boot(options.image.resolve(),out/f'boot-{index+1}',not(options.absent or options.ram_source),
            options.duplicate or options.ram_source or options.qi_no_clflush,options.registers,options.ram_source,options.domains,options.gpu_absent,options.qi,options.qi_no_clflush))
        (out/'results.json').write_text(json.dumps(records,indent=2))
        print(f'PASS boot {index+1}: DMAR result and budget verified',flush=True)

if __name__=='__main__': main()
