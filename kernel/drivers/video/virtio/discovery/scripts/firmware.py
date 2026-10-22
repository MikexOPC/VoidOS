"""Read-only snapshots of an owned QEMU GPU; no guest driver activation."""
import argparse
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[6]
QEMU = Path('C:/Program Files/qemu')
spec = importlib.util.spec_from_file_location('protocol',ROOT/'arch/x86/kernel/drivers/input/ps2/scripts/protocol.py')
protocol = importlib.util.module_from_spec(spec)
spec.loader.exec_module(protocol)

# ====== listener ======

def listener():
    channel = socket.socket()
    channel.bind(('127.0.0.1',0))
    channel.listen(1)
    channel.settimeout(15)
    return channel

# ====== qmp ======

def qmp(stream, command, arguments=None):
    stream.write((json.dumps(dict(execute=command,arguments=arguments or {}))+'\n').encode())
    stream.flush()
    for _ in range(128):
        line = stream.readline(1024*1024)
        if not line: raise RuntimeError('QMP disconnected')
        response = json.loads(line)
        if 'error' in response: raise RuntimeError(str(response))
        if 'return' in response: return response['return']
    raise RuntimeError('Too many QMP events')

# ====== qtest ======

def qtest(stream, command):
    stream.write((command+'\n').encode())
    stream.flush()
    line = stream.readline(4096).decode().strip().split()
    if not line or line[0] != 'OK': raise RuntimeError('QTest: '+str(line))
    return int(line[1],0) if len(line)>1 else 0

# ====== snapshot ======

# QEMU v11.1.0 hw/pci-host/q35.c: CF8/CFC index/data.
# hw/pci/pci_host.c pci_host_data_read: enable31, bus23:16,
# devfn15:8, aligned conventional offset7:2. No config data writes.
def snapshot(stream, device):
    saved = qtest(stream,'inl 0xcf8')
    result = bytearray(256)
    occupied = set()
    selector = 0x80000000 | (device['bus']<<16) | (device['slot']<<11) | (device['function']<<8)
    def read(offset,count):
        for at in range(offset,offset+count):
            qtest(stream,f'outl 0xcf8 {selector|(at&~3):#x}')
            result[at]=qtest(stream,f'inb {0xcfc+(at&3):#x}')
    try:
        read(0,64)
        cursor=result[0x34]
        count=0
        while cursor:
            count+=1
            if count>48 or cursor<64 or cursor>252 or cursor%4 or cursor in occupied or cursor+1 in occupied:
                raise RuntimeError('Unsafe PCI chain')
            read(cursor,2)
            length,metadata=2,2
            if result[cursor]==9:
                if cursor+2 in occupied or cursor+3 in occupied: raise RuntimeError('Overlapping PCI header')
                read(cursor+2,2)
                length=result[cursor+2]
                if length<4 or length>256-cursor: raise RuntimeError('PCI capability extent')
                if result[cursor+3] in (1,2,3,4): metadata=20 if result[cursor+3]==2 else 16
                if length<metadata: raise RuntimeError('Short PCI metadata')
            extent=set(range(cursor,cursor+length))
            if occupied & extent: raise RuntimeError('Overlapping PCI capabilities')
            occupied |= extent
            if metadata>4: read(cursor+4,metadata-4)
            cursor=result[cursor+1]
    finally:
        qtest(stream,f'outl 0xcf8 {saved:#x}')
    return result

# ====== boot ======

def boot(image, host, directory, gpu, check=None, exercise=None):
    esp = directory/'esp/EFI/BOOT'
    esp.mkdir(parents=True)
    loader = ROOT/'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    for source, name in ((image,'kernel.elf'),(loader,'BOOTX64.EFI')):
        shutil.copy2(source,esp/name)
        if protocol.image_hash(source)!=protocol.image_hash(esp/name): raise RuntimeError('ESP mismatch')
    (esp/'boot-check.txt').write_bytes(b'OS boot check\r\n')
    variables = directory/'vars.fd'
    shutil.copy2(QEMU/'share/edk2-i386-vars.fd',variables)
    serial = directory/'serial.log'
    vm = None
    parser_hash = None
    parser_result = 'ABSENT: standard VGA fallback'
    with listener() as qm, listener() as qt, (directory/'monitor.log').open('w') as monitor, \
            (directory/'stderr.log').open('w') as error:
        arguments = [str(QEMU/'qemu-system-x86_64.exe'),'-machine','q35','-cpu','max',
            '-accel','tcg','-m','128','-smp','1','-net','none','-vga','none',
            '-device',('virtio-vga,id=vgpu,xres=1024,yres=768' if gpu=='virtio' else 'VGA'),'-display','none',
            '-monitor','stdio','-no-reboot','-serial','file:'+serial.as_posix(),
            '-qmp',f'tcp:127.0.0.1:{qm.getsockname()[1]}',
            '-qtest',f'tcp:127.0.0.1:{qt.getsockname()[1]}',
            '-qtest-log',str(directory/'qtest.log'),
            '-drive','if=pflash,format=raw,readonly=on,file='+(QEMU/'share/edk2-x86_64-code.fd').as_posix(),
            '-drive','if=pflash,format=raw,file='+variables.as_posix(),
            '-drive','format=raw,file=fat:rw:'+(directory/'esp').as_posix()]
        (directory/'arguments.json').write_text(json.dumps(arguments,indent=2))
        try:
            vm = subprocess.Popen(arguments,stdin=subprocess.PIPE,stdout=monitor,stderr=error,
                text=True,creationflags=subprocess.CREATE_NO_WINDOW)
            with qm.accept()[0] as control, qt.accept()[0] as transport:
                control.settimeout(10); transport.settimeout(10)
                with control.makefile('rwb') as cs, transport.makefile('rwb') as ts:
                    greeting = json.loads(cs.readline(65536))
                    if 'QMP' not in greeting: raise RuntimeError('Missing QMP greeting')
                    qmp(cs,'qmp_capabilities')
                    protocol.wait_marker(vm,serial,'KERNEL_POINTER_READY',80)
                    guest = protocol.read_serial(serial)
                    present = 'PRESENT' if gpu=='virtio' else 'ABSENT'
                    if 'VIRTIO_PCI_HANDOFF_'+present not in guest or 'KERNEL_VGPU_PCI_'+present not in guest:
                        raise RuntimeError('Missing guest acquisition/contract evidence')
                    if protocol.number(guest,'KERNEL_BOOT_VERSION') != 5 or protocol.number(guest,'KERNEL_BOOT_SIZE') != 536:
                        raise RuntimeError('Wrong guest contract version/size')
                    qmp(cs,'stop')
                    if qmp(cs,'query-status')['running']: raise RuntimeError('VM not stopped')
                    pci = qmp(cs,'query-pci')
                    (directory/'pci.json').write_text(json.dumps(pci,indent=2))
                    candidates = [dev for bus in pci for dev in bus['devices']
                        if dev['id']['vendor']==0x1af4 and dev['id']['device']==0x1050]
                    if gpu=='virtio':
                        if len(candidates)!=1: raise RuntimeError('Expected exactly one modern GPU')
                        device = candidates[0]
                        config = snapshot(ts,device)
                        if config != snapshot(ts,device): raise RuntimeError('Unstable config snapshot')
                        sizes = [0]*6
                        for region in device['regions']:
                            if 0<=region['bar']<6: sizes[region['bar']]=region['size']
                        (directory/'config.bin').write_bytes(config)
                        (directory/'sizes.bin').write_bytes(struct.pack('<6Q',*sizes))
                        environment = os.environ.copy()
                        runtimes = sorted(Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC').glob('*/bin/Hostx64/x64'))
                        if not runtimes: raise RuntimeError('MSVC sanitizer runtime unavailable')
                        environment['PATH'] = str(runtimes[-1])+os.pathsep+environment.get('PATH','')
                        parser_hash = protocol.image_hash(host)
                        parsed = subprocess.run([str(host),str(directory/'config.bin'),str(directory/'sizes.bin')],
                            capture_output=True,text=True,timeout=15,env=environment)
                        (directory/'parser.log').write_text(parsed.stdout+parsed.stderr)
                        parsed.check_returncode()
                        parser_result = parsed.stdout.strip()
                        guest_addresses = re.findall(r'KERNEL_VGPU_REGION: 0x([0-9a-fA-F]+)',guest)
                        host_addresses = re.findall(r'(?:common|notify|isr|device)=0x([0-9a-fA-F]+)',parser_result)
                        if [int(v,16) for v in guest_addresses] != [int(v,16) for v in host_addresses]:
                            raise RuntimeError('Guest/host PCI regions differ')
                        if parser_hash != protocol.image_hash(host): raise RuntimeError('Parser changed during test')
                    elif candidates: raise RuntimeError('Unexpected VirtIO device in fallback test')
                    if check: check(ts,guest,directory,gpu)
                    qmp(cs,'cont')
                    if exercise: exercise(vm,serial)
                    final = protocol.exit_input(vm,serial)
                    if protocol.number(final,'SCHEDULER_BUDGET_BEFORE') != protocol.number(final,'SCHEDULER_BUDGET_AFTER'):
                        raise RuntimeError('Physical budget mismatch')
                    qmp(cs,'quit')
                    vm.wait(timeout=10)
                    if vm.returncode: raise RuntimeError('QEMU exit failure')
                    return dict(KernelSHA256=protocol.image_hash(image),LoaderSHA256=protocol.image_hash(loader),
                        ParserSHA256=parser_hash,
                        SnapshotSHA256=protocol.image_hash(directory/'config.bin') if gpu=='virtio' else None,Parser=parser_result,Gpu=gpu,
                        KernelReady=True,PhysicalBudgetRestored=True)
        finally:
            if vm and vm.poll() is None:
                vm.terminate()
                try: vm.wait(timeout=5)
                except subprocess.TimeoutExpired: vm.kill(); vm.wait(timeout=5)

# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image',type=Path,default=ROOT/'kernel/build/virtio-handoff-image/kernel.elf')
    parser.add_argument('--host',type=Path,default=ROOT/'kernel/build/virtio-pci-host/host.exe')
    parser.add_argument('--boots',type=int,default=10)
    parser.add_argument('--gpu',choices=('virtio','std'),default='virtio')
    options = parser.parse_args()
    if not 1<=options.boots<=100: raise SystemExit('Boot count outside 1..100')
    out = ROOT/'kernel/build'/('virtio-pci-firmware-'+uuid.uuid4().hex)
    out.mkdir()
    print('Evidence: '+str(out),flush=True)
    records=[]
    for index in range(options.boots):
        records.append(boot(options.image.resolve(),options.host.resolve(),out/f'boot-{index+1}',options.gpu))
        (out/'results.json').write_text(json.dumps(records,indent=2))
        print(f'PASS boot {index+1}: '+records[-1]['Parser'],flush=True)
    print('PASS: guest PCI metadata v5 matched device presence; console and physical budget verified; display still GOP',flush=True)

if __name__=='__main__': main()
