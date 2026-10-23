import argparse
import importlib.util
import json
import re
from pathlib import Path
import shutil
import subprocess
import time
import uuid
from PIL import Image
from PIL import ImageChops

ROOT = Path(__file__).resolve().parents[5]
QEMU = Path('C:/Program Files/qemu')

# ============ shared protocol and fixed raster ============

def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

provider = load('interactive_provider', 'arch/x86/kernel/drivers/input/ps2/scripts/firmware.py')
pointer = load('interactive_pointer', 'kernel/console/video/scripts/pointer/firmware.py')

# ============ complete screenshot deadline ============

def screenshot(vm, directory, name):
    target = directory / (name + '.ppm')
    provider.command(vm, 'screendump "' + target.as_posix() + '"')
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if vm.poll() is not None:
            raise RuntimeError('VM exited during screenshot')
        try:
            with Image.open(target) as image:
                image.load()
                return image.convert('RGB')
        except (OSError, ValueError):
            time.sleep(0.005)
    raise TimeoutError('Incomplete screenshot')

# ============ independent placement over the clean scene ============

def expected(scene, x, y):
    image = scene.copy()
    for row in range(18):
        for col in range(18):
            rgb = scene.getpixel((x+col,y+row))
            image.putpixel((x+col,y+row),tuple(pointer.blend(pointer.sample(col,row),rgb)))
    return image

# ============ owned interactive image and cleanup ============

# ====== translation_check ======

# V2b: the GPU domain translates from before the session to after exit (QEMU v11.1.0 trace).
VTD_EVENTS = ('vtd_dmar_enable','vtd_switch_address_space','vtd_frr_new','vtd_reg_dmar_root')

def translation_check(log, trace_text, fallbacks=0):
    for marker in ('KERNEL_VGPU_SESSION_TRANSLATING','KERNEL_VGPU_IOMMU_TRANSLATING','KERNEL_VGPU_SESSION_RELEASED',
            'KERNEL_VGPU_IOMMU_RELEASED'):
        if marker not in log: raise RuntimeError('V2b marker missing: '+marker)
    if 'KERNEL_VGPU_SESSION_GPU_FAULT' in log or 'RETAINED' in log: raise RuntimeError('V2b GPU fault or retention')
    if log.index('KERNEL_VGPU_SESSION_TRANSLATING') > log.index('KERNEL_POINTER_OPEN'):
        raise RuntimeError('Translation started after the console')
    if log.index('KERNEL_VGPU_SESSION_RELEASED') < log.index('KERNEL_INPUT_TASKS_COLLECTED'):  # stop runs after the watchdog closes
        raise RuntimeError('Translation ended before the session tasks')
    if provider.number(log,'KERNEL_VGPU_SESSION_BUDGET') != provider.number(log,'KERNEL_VGPU_SESSION_BUDGET_AFTER'):
        raise RuntimeError('V2b budget')
    polls = provider.number(log,'KERNEL_VGPU_SESSION_POLLS')
    source = provider.number(log,'KERNEL_VGPU_IOMMU_SOURCE')
    lines = trace_text.splitlines()
    enables = [line for line in lines if line.startswith('vtd_dmar_enable')]
    if enables != ['vtd_dmar_enable enable 1','vtd_dmar_enable enable 0']: raise RuntimeError('V2b enable trace')
    bdf = f'{source>>8:02x}:{(source>>3)&31:02x}.{source&7}'
    if f'vtd_switch_address_space Device {bdf} switching address space (iommu enabled=1)' not in lines:
        raise RuntimeError('GPU never translated')
    for line in lines:
        match = re.fullmatch(r'vtd_frr_new index \d+ high (0x[0-9a-f]+) low (0x[0-9a-f]+)', line)
        if match and int(match[1],16) & 0xffff == source: raise RuntimeError('GPU fault record during the session')
    if not polls: raise RuntimeError('No fault scan during the session')
    # V2c: the screenshots above come from the VirtIO scanout; the console presented through it.
    presents = provider.number(log,'KERNEL_VGPU_PRESENTS')
    if 'KERNEL_VGPU_PRESENTING' not in log or not presents or provider.number(log,'KERNEL_VGPU_PRESENT_FAILURES') != fallbacks:
        raise RuntimeError('Console not presented through the GPU')
    if fallbacks and 'KERNEL_VGPU_PRESENT_FALLBACK' not in log: raise RuntimeError('Fallback not reported')
    if not fallbacks and 'KERNEL_VGPU_PRESENT_FALLBACK' in log: raise RuntimeError('Unexpected fallback')
    latency = dict(Present=latency_check(log,'KERNEL_VGPU_PRESENT_PM',presents-fallbacks),
        FlushWait=latency_check(log,'KERNEL_VGPU_FLUSH_WAIT_PM',presents-fallbacks),
        Area=latency_check(log,'KERNEL_VGPU_PRESENT_AREA',presents-fallbacks),
        Poll=latency_check(log,'KERNEL_VGPU_POLL_PM',polls),Quiet=latency_check(log,'KERNEL_VGPU_QUIET_PM',1))
    return dict(Source=hex(source),Polls=polls,Translated=True,Presents=presents,Fallbacks=fallbacks,Latency=latency)

# ====== latency_check ======

# V2d: an accumulator report is coherent (buckets sum to the count, min <= mean <= max).
def latency_check(log, name, count=None):
    value = lambda field: provider.number(log, f'{name}_{field}')
    total, samples = value('TOTAL'), value('COUNT')
    buckets = [value(f'B{index}') for index in range(8)]
    if sum(buckets) != samples or (count is not None and samples != count) or value('SATURATED'):
        raise RuntimeError('Latency report counts: '+name)
    if samples and not value('MIN') <= total // samples <= value('MAX'):
        raise RuntimeError('Latency report bounds: '+name)
    return dict(Count=samples, Min=value('MIN'), Max=value('MAX'), Total=total, Buckets=buckets)

# ====== untranslated_check ======

# V2b without a DMAR: the session runs untranslated and allocates nothing for the GPU domain.
def untranslated_check(log):
    if 'KERNEL_VGPU_SESSION_UNTRANSLATED' not in log or 'KERNEL_VGPU_IOMMU_DMAR_ABSENT' not in log:
        raise RuntimeError('Untranslated fallback not reported')
    if 'KERNEL_VGPU_SESSION_TRANSLATING' in log or 'KERNEL_VGPU_SESSION_POLLS' in log:
        raise RuntimeError('Translation claimed without a DMAR')
    return dict(Untranslated=True)

def boot(image, out, index, iommu=False, absent=False, fallbacks=0):
    directory = out / f'boot-{index}'
    esp = directory / 'esp/EFI/BOOT'
    esp.mkdir(parents=True)
    loader = ROOT / 'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'
    shutil.copy2(image,esp/'kernel.elf')
    shutil.copy2(loader,esp/'BOOTX64.EFI')
    if provider.image_hash(image) != provider.image_hash(esp/'kernel.elf'):
        raise RuntimeError('ESP image mismatch')
    (esp/'boot-check.txt').write_bytes(b'OS boot check\r\n')
    variables = directory/'vars.fd'
    shutil.copy2(QEMU/'share/edk2-i386-vars.fd',variables)
    serial = directory/'serial.log'
    trace = directory/'qemu.log'
    mouse_trace = directory/'mouse-trace.log'
    # V2b: intel-iommu before the VirtIO GPU it translates; one trace file for both event sets.
    video = (['-vga','none','-device','intel-iommu,intremap=off,caching-mode=on,device-iotlb=off',
        '-device','virtio-vga,iommu_platform=on,xres=1024,yres=768']+[item for event in VTD_EVENTS for item in ('-trace','enable='+event)]
        if iommu else ['-vga','none','-device','virtio-vga'] if absent else ['-vga','std'])
    arguments = [str(QEMU/'qemu-system-x86_64.exe'),'-machine','q35','-cpu','max',
        '-accel','tcg','-m','128','-smp','1','-net','none']+video+[
        '-display','none','-monitor','stdio','-no-reboot','-serial','file:'+serial.as_posix(),
        '-d','int,cpu_reset','-D',trace.as_posix(),
        '-trace','enable=ps2_mouse_send_packet,file='+mouse_trace.as_posix(),
        '-drive','if=pflash,format=raw,readonly=on,file='+(QEMU/'share/edk2-x86_64-code.fd').as_posix(),
        '-drive','if=pflash,format=raw,file='+variables.as_posix(),
        '-drive','format=raw,file=fat:rw:'+(directory/'esp').as_posix()]
    vm = None
    with (directory/'monitor.log').open('w') as monitor, (directory/'stderr.log').open('w') as error:
        try:
            vm = subprocess.Popen(arguments,stdin=subprocess.PIPE,stdout=monitor,stderr=error,
                text=True,creationflags=subprocess.CREATE_NO_WINDOW)
            log = provider.wait_marker(vm,serial,'KERNEL_POINTER_READY',240 if iommu or absent else 80)  # V2b scans the PCI graph first
            packet_pattern = r'ps2_mouse_send_packet .*? x (-?\d+) y (-?\d+) z'
            def delivered():
                packets = re.findall(packet_pattern,mouse_trace.read_text(errors='replace'))
                return sum(int(dx) for dx,dy in packets),sum(int(dy) for dx,dy in packets)
            baseline_x,baseline_y = delivered()
            width,height,stride,base,fmt = pointer.geometry(log)
            columns,rows = width//9,height//18
            x,y = (columns*9-18)//2,(rows*18-18)//2
            initial_x,initial_y = x,y
            first = screenshot(vm,directory,'initial')
            if first.size != (width,height): raise RuntimeError('Screenshot geometry')
            scene = first.copy()
            for row in range(18):
                for col in range(18): scene.putpixel((x+col,y+row),(8,16,24))
            if expected(scene,x,y).tobytes() != first.tobytes():
                raise RuntimeError('Initial cursor raster mismatch')
            latencies = []
            captures = 0
            syncs = 0
            for segment in range(12):
                dx,dy = [(1,1),(-1,1),(-1,-1),(1,-1)][segment%4]
                for step in range(50):
                    provider.command(vm,f'mouse_move {dx} {dy}')
                    x = max(0,min(columns*9-18,x+dx))
                    y = max(0,min(rows*18-18,y+dy))
                    time.sleep(0.002)
                start = time.monotonic()
                target = expected(scene,x,y).tobytes()
                deadline = start+(90 if fallbacks else 5)  # V2c fallback: the GOP is redrawn in full once
                while time.monotonic() < deadline:
                    actual = screenshot(vm,directory,f'move-{segment}-{captures}')
                    captures += 1
                    dx_sent,dy_sent = delivered()
                    arrived = (dx_sent-baseline_x,dy_sent-baseline_y) == (x-initial_x,initial_y-y)
                    if actual.tobytes() == target and arrived: break
                    if not arrived:
                        provider.command(vm,'mouse_move 0 0')
                        syncs += 1
                    provider.wait_marker(vm,serial,'KERNEL_POINTER_READY',1)
                else:
                    provider.command(vm,'info irq')
                    provider.command(vm,'info pic')
                    final = provider.exit_input(vm,serial)
                    (directory/'failure-state.json').write_text(json.dumps(dict(
                        ExpectedX=x,ExpectedY=y,FinalX=provider.number(final,'KERNEL_POINTER_X'),
                        FinalY=provider.number(final,'KERNEL_POINTER_Y'),Segment=segment,
                        Delivered=delivered(),Baseline=[baseline_x,baseline_y]),indent=2))
                    raise RuntimeError('Cursor trails, deformation or missing progress')
                latencies.append(time.monotonic()-start)
            for row in range(18):
                # V2c: while presenting through the GPU the console pixels live in the surface backing.
                origin = provider.number(log,'KERNEL_VGPU_PRESENT_SURFACE') if 'KERNEL_VGPU_PRESENTING' in log and not fallbacks else base
                words = pointer.physical(vm,directory/'monitor.log',origin+((y+row)*stride+x)*4)
                for col in range(18):
                    rgb = tuple(pointer.blend(pointer.sample(col,row),(8,16,24)))
                    r,g,b = rgb
                    color = r|(g<<8)|(b<<16) if fmt == 0 else b|(g<<8)|(r<<16)
                    if words[col//2] >> (32*(col%2)) & 0xffffffff != color:
                        raise RuntimeError('Physical cursor mismatch')
            for count,key in enumerate(['e','x','i','t'],1):
                provider.command(vm,'sendkey '+key+' 20')
                provider.wait_marker(vm,serial,'PS2_KEY: ',5,count)
                time.sleep(0.04)
            cancelled = provider.cancel_input(vm,serial)
            if 'SHELL_EXIT_REQUEST' in cancelled or 'KERNEL_POINTER_CLEAN' in cancelled:
                raise RuntimeError('Esc stopped the interactive session')
            provider.command(vm,'mouse_move 3 2')
            x += 3; y += 2
            target = expected(scene,x,y).tobytes()
            deadline = time.monotonic()+5
            attempt = 0
            while time.monotonic() < deadline:
                if screenshot(vm,directory,f'after-cancel-{attempt}').tobytes() == target: break
                attempt += 1
                time.sleep(0.005)
            else: raise RuntimeError('Mouse/video did not recover after Esc')
            for count,key in enumerate(['h','e','l','p','ret'],6):
                provider.command(vm,'sendkey '+key+' 20')
                provider.wait_marker(vm,serial,'PS2_KEY: ',5,count)
            provider.wait_marker(vm,serial,'SHELL_HELP:')
            log = provider.exit_input(vm,serial)
            for marker in ['KERNEL_POINTER_CLEAN','KERNEL_CONSOLE_SHELL_CLEAN',
                'KERNEL_PS2_MOUSE_CLEANUP_OK','KERNEL_PS2_INPUT_CLEANUP_OK','KERNEL_INPUT_TASKS_COLLECTED']:
                if marker not in log: raise RuntimeError('Missing '+marker)
            if 'MOUSE_EVENT:' in log or 'KERNEL_POINTER_FRAME' in log:
                raise RuntimeError('Interactive image still logs every event/frame')
            if provider.number(log,'KERNEL_POINTER_X') != x or provider.number(log,'KERNEL_POINTER_Y') != y:
                raise RuntimeError('Final cursor position mismatch')
            dx_sent,dy_sent = delivered()
            if (dx_sent-baseline_x,dy_sent-baseline_y) != (x-initial_x,initial_y-y):
                raise RuntimeError('QEMU hardware packet sum differs from requested motion')
            if provider.number(log,'MOUSE_LOSSES'):
                raise RuntimeError('Input loss')
            session = log[:log.index('KERNEL_INPUT_TASKS_COLLECTED')].rsplit('# ============ scheduler cooperativo BSP',1)[-1]
            before = provider.number(session,'SCHEDULER_BUDGET_BEFORE')
            if before != provider.number(session,'SCHEDULER_BUDGET_AFTER'):
                raise RuntimeError('Physical allocation budget not restored')
            if 'Servicing hardware INT=0x42' not in mouse_trace.read_text(errors='replace'):
                raise RuntimeError('Missing hardware mouse IRQ evidence')
            provider.command(vm,'quit')
            vm.wait(timeout=5)
            if vm.returncode: raise RuntimeError('QEMU exit failure')
            translation = (translation_check(log,mouse_trace.read_text(errors='replace'),fallbacks) if iommu else
                untranslated_check(log) if absent else None)
            return dict(Translation=translation,Commands=600,Syncs=syncs,Scenes=12,Captures=captures,PhysicalPixels=324,
                EscCancelled=True,EscMouseProgress=True,CancelScenes=1,
                DrainSeconds=latencies,BudgetBefore=before,BudgetAfter=before,
                Flushes=provider.number(log,'KERNEL_CONSOLE_FLUSHES'),
                FlushPm=provider.number(log,'KERNEL_CONSOLE_FLUSH_TOTAL_PM'),
                SettlePm=provider.number(log,'KERNEL_CONSOLE_SETTLE_TOTAL_PM'))
        finally:
            if vm is not None:
                if vm.poll() is None: vm.kill()
                vm.wait(timeout=5)
                vm.stdin.close()

# ============ final image campaign ============

def audit(directory):
    complete = incomplete = missing = 0
    examples = []
    report = json.loads((directory/'results.json').read_text())
    for boot_directory in sorted(directory.glob('boot-*')):
        with Image.open(boot_directory/'initial.ppm') as first:
            scene = first.convert('RGB')
        x,y = (scene.width//9*9-18)//2,(scene.height//18*18-18)//2
        for row in range(18):
            for col in range(18): scene.putpixel((x+col,y+row),(8,16,24))
        for capture in sorted(boot_directory.glob('move-*.ppm')):
            with Image.open(capture) as frame: image = frame.convert('RGB')
            box = ImageChops.difference(image,scene).getbbox()
            if not box:
                missing += 1
                examples.append(str(capture.relative_to(directory)))
            elif box[0]+18 <= scene.width and box[1]+18 <= scene.height and expected(scene,box[0],box[1]).tobytes() == image.tobytes():
                complete += 1
            else:
                incomplete += 1
                examples.append(str(capture.relative_to(directory)))
    result = dict(Image=report['Image'],Complete=complete,Incomplete=incomplete,Missing=missing,Examples=examples)
    (directory/'shape-audit.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image',type=Path)
    parser.add_argument('--audit',type=Path)
    parser.add_argument('--boots',type=int,choices=range(1,21),default=10)
    parser.add_argument('--iommu',action='store_true'); parser.add_argument('--iommu-absent',action='store_true')
    parser.add_argument('--present-fallback',action='store_true')
    args = parser.parse_args()
    if args.audit:
        audit(args.audit)
        return
    if args.image is None: parser.error('--image or --audit is required')
    if b'KERNEL_POINTER_READY' not in args.image.read_bytes():
        raise RuntimeError('Requires the interactive profile')
    out = ROOT/'kernel/build'/('pointer-interactive-check-'+uuid.uuid4().hex)
    out.mkdir()
    results = []
    report = dict(Image=provider.image_hash(args.image),Loader=provider.image_hash(
        ROOT/'arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI'),Results=results)
    try:
        for index in range(1,args.boots+1):
            results.append(boot(args.image,out,index,args.iommu or args.present_fallback,args.iommu_absent,
                1 if args.present_fallback else 0))
            (out/'results.json').write_text(json.dumps(report,indent=2))
            print(f'PASS: interactive cursor {index}/{args.boots}; pixels, movement, cleanup',flush=True)
    except Exception as failure:
        report['Failure'] = str(failure)
        raise
    finally:
        (out/'results.json').write_text(json.dumps(report,indent=2))
        print('Evidence: '+str(out),flush=True)

if __name__ == '__main__':
    main()
