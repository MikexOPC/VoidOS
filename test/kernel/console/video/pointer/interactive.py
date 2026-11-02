import argparse
import importlib.util
import json
import re
from pathlib import Path
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

# G2: owned VM and event-based serial log (scripts/lib); the pointer module keeps the raster model.
qemu = load('os_qemu', 'scripts/lib/qemu.py')
pointer = load('interactive_pointer', 'kernel/console/video/scripts/pointer/firmware.py')

# ============ complete screenshot deadline ============

def screenshot(vm, directory, name):
    return vm.screenshot(name)

# ============ shell input lifecycle ============

def cancel_input(vm):
    count = vm.log().count('PS2_EDIT_CANCEL')
    vm.command('sendkey esc 20')
    return vm.wait('PS2_EDIT_CANCEL', 5, occurrences=count + 1)

def exit_input(vm):
    cancel_input(vm)
    for key in ('e', 'x', 'i', 't', 'ret'):
        count = vm.log().text.count('PS2_KEY: ')
        vm.command('sendkey ' + key + ' 20')
        vm.wait_text('PS2_KEY: ', 5, occurrences=count + 1)
        time.sleep(0.04)
    return vm.wait('KERNEL_READY', 30)

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

# ====== profile_check ======

PROFILE = ('FLUSH','RENDER','GROUP','VERIFY','TARGET','PREPARE','PRESENT','POLL')

# Profile images (-ProfileVgpuFlush): every phase report coherent, one FLUSH sample per flush, and
# the timed phases inside a flush never exceed the flush total.
def profile_check(log):
    if not log.has_number('KERNEL_PROFILE_FLUSH_COUNT'): return None
    phases = {name: latency_check(log,'KERNEL_PROFILE_'+name) for name in PROFILE}
    if phases['FLUSH']['Count'] != log.number('KERNEL_CONSOLE_FLUSHES'): raise RuntimeError('Profile flush count')
    inside = phases['RENDER']['Total'] + phases['PRESENT']['Total'] + phases['POLL']['Total']
    if inside > phases['FLUSH']['Total']: raise RuntimeError('Profile phases exceed the flush')
    return phases

def translation_check(log, trace_text, fallbacks=0, presenting=True, stall=False):
    for marker in ('KERNEL_VGPU_SESSION_TRANSLATING','KERNEL_VGPU_IOMMU_TRANSLATING','KERNEL_VGPU_SESSION_RELEASED',
            'KERNEL_VGPU_IOMMU_RELEASED'):
        if not log.has(marker): raise RuntimeError('V2b marker missing: '+marker)
    if log.has('KERNEL_VGPU_SESSION_GPU_FAULT') or any('RETAINED' in name for _, _, name in log.events):
        raise RuntimeError('V2b GPU fault or retention')
    if not log.before('KERNEL_VGPU_SESSION_TRANSLATING','KERNEL_POINTER_OPEN'):
        raise RuntimeError('Translation started after the console')
    if not log.before('KERNEL_INPUT_TASKS_COLLECTED','KERNEL_VGPU_SESSION_RELEASED'):  # stop runs after the watchdog closes
        raise RuntimeError('Translation ended before the session tasks')
    if log.number('KERNEL_VGPU_SESSION_BUDGET') != log.number('KERNEL_VGPU_SESSION_BUDGET_AFTER'):
        raise RuntimeError('V2b budget')
    polls = log.number('KERNEL_VGPU_SESSION_POLLS')
    source = log.number('KERNEL_VGPU_IOMMU_SOURCE')
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
    if not presenting:
        # Profile image B: translated GPU domain, console kept on GOP.
        if log.has('KERNEL_VGPU_PRESENTING'): raise RuntimeError('Image B presented')
        return dict(Source=hex(source),Polls=polls,Translated=True,Presents=0,
            Latency=dict(Poll=latency_check(log,'KERNEL_VGPU_POLL_PM',polls)))
    # V2c: the screenshots above come from the VirtIO scanout; the console presented through it.
    presents = log.number('KERNEL_VGPU_PRESENTS')
    if not log.has('KERNEL_VGPU_PRESENTING') or not presents or log.number('KERNEL_VGPU_PRESENT_FAILURES') != fallbacks:
        raise RuntimeError('Console not presented through the GPU')
    timeout = log.has('KERNEL_VGPU_PRESENT_TIMEOUT')
    if stall:
        # R2/R3-A: the 0.2 s hold is only marked slow; the 1.2 s hold times out and is recovered.
        if (log.number('KERNEL_VGPU_PRESENT_TIMEOUTS') != 1 or log.number('KERNEL_VGPU_PRESENT_RECOVERIES') != 1 or
            not log.number('KERNEL_VGPU_PRESENT_SLOWS') or log.has('KERNEL_VGPU_PRESENT_FALLBACK') or
            not log.before('KERNEL_VGPU_PRESENT_TIMEOUT','KERNEL_VGPU_PRESENT_RECOVERED')):
            raise RuntimeError('Stall: slow mark, timeout or recovery missing')
    elif fallbacks and not log.has('KERNEL_VGPU_PRESENT_FALLBACK'): raise RuntimeError('Fallback not reported')
    elif not fallbacks and (log.has('KERNEL_VGPU_PRESENT_FALLBACK') or timeout or
        log.number('KERNEL_VGPU_PRESENT_TIMEOUTS')): raise RuntimeError('Unexpected fallback')
    latency = dict(Present=latency_check(log,'KERNEL_VGPU_PRESENT_PM',presents-fallbacks),
        FlushWait=latency_check(log,'KERNEL_VGPU_FLUSH_WAIT_PM',presents-fallbacks),
        Area=latency_check(log,'KERNEL_VGPU_PRESENT_AREA',presents-fallbacks),
        Poll=latency_check(log,'KERNEL_VGPU_POLL_PM',polls),Quiet=latency_check(log,'KERNEL_VGPU_QUIET_PM',1))
    return dict(Source=hex(source),Polls=polls,Translated=True,Presents=presents,Fallbacks=fallbacks,Latency=latency)

# ====== latency_check ======

# V2d: an accumulator report is coherent (buckets sum to the count, min <= mean <= max).
def latency_check(log, name, count=None):
    value = lambda field: log.number(f'{name}_{field}')
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
    if not log.has('KERNEL_VGPU_SESSION_UNTRANSLATED') or not log.has('KERNEL_VGPU_IOMMU_DMAR_ABSENT'):
        raise RuntimeError('Untranslated fallback not reported')
    if log.has('KERNEL_VGPU_SESSION_TRANSLATING') or log.has_number('KERNEL_VGPU_SESSION_POLLS'):
        raise RuntimeError('Translation claimed without a DMAR')
    return dict(Untranslated=True)

def boot(image, out, index, iommu=False, absent=False, fallbacks=0, presenting=True, stall=False, smp=1, soak_seconds=0):
    directory = out / f'boot-{index}'
    mouse_trace = directory/'mouse-trace.log'
    video = 'iommu' if iommu else 'virtio' if absent else 'std'
    with qemu.Vm(directory, image, video, smp=smp, traces=[('ps2_mouse_send_packet', mouse_trace)]) as vm:
        log = vm.wait('KERNEL_POINTER_READY',600 if iommu or absent else 80)  # V2b scans the PCI graph first; TCG speed varies with host load
        if smp > 1 and (not log.has('KERNEL_AP_RESOURCES_PREPARED', 'OK') or log.number('KERNEL_AP_CPUS') != smp):
            raise RuntimeError('AP resources were not prepared for configured CPUs')
        packet_pattern = r'ps2_mouse_send_packet .*? x (-?\d+) y (-?\d+) z'
        def delivered():
            packets = re.findall(packet_pattern,mouse_trace.read_text(errors='replace'))
            return sum(int(dx) for dx,dy in packets),sum(int(dy) for dx,dy in packets)
        soak = (load('ap_soak','test/arch/x86/kernel/cpu/percpu/startup/runtime/soak.py').run(vm,delivered,soak_seconds)
            if soak_seconds else None)
        baseline_x,baseline_y = delivered()
        width,height,stride,base,fmt = pointer.geometry(log.text)
        gop = (width,height,stride,fmt)
        presented = re.search(r'KERNEL_VGPU_PRESENT_GEOMETRY: 0x([0-9a-f]{16})x0x([0-9a-f]{16})',log.text)
        if presented:
            # Geometry A: while presenting, the screen and the console grid follow the GPU display.
            width,height = int(presented[1],16),int(presented[2],16)
            stride,fmt = width,1
        elif iommu and presenting and not absent: raise RuntimeError('Present geometry not reported')
        gpu = (width,height,stride,fmt)
        columns,rows = width//9,height//18
        grid = re.search(r'KERNEL_CONSOLE_GRID: 0x([0-9a-f]{16})x0x([0-9a-f]{16})',log.text)
        if not grid or (int(grid[1],16),int(grid[2],16)) != (min(columns,240),min(rows,67)):
            raise RuntimeError('Console grid does not follow the drawing target')
        x,y = (columns*9-18)//2,(rows*18-18)//2
        initial_x,initial_y = x,y
        first = screenshot(vm,directory,'initial')
        if fallbacks and presented and first.size == gop[:2]:
            # The injected fault can fire before the first capture: the view is already clipped
            # to the GOP grid; the pointer, centred in the GPU grid, fits it unchanged.
            width,height,stride,fmt = gop
            columns,rows = min(columns,width//9),min(rows,height//18)
        if first.size != (width,height): raise RuntimeError('Screenshot geometry')
        scene = first.copy()
        for row in range(18):
            for col in range(18): scene.putpixel((x+col,y+row),(8,16,24))
        if expected(scene,x,y).tobytes() != first.tobytes():
            raise RuntimeError('Initial cursor raster mismatch')
        latencies = []
        captures = 0
        syncs = 0
        def fit(actual):
            # GOP fallback: the screen shrinks to the GOP; the view was clipped to its grid and the
            # pointer clamped. The expected background is the old scene inside the new grid.
            nonlocal scene,columns,rows,x,y,width,height,stride,fmt
            if actual.size == scene.size: return
            if stall and actual.size == gpu[:2]:
                # R3-A recovery: the cleared GPU surface shows the clipped grid on black.
                width,height,stride,fmt = gpu
                rebuilt = Image.new('RGB',actual.size,(0,0,0))
                rebuilt.paste(scene.crop((0,0,columns*9,rows*18)),(0,0))
                for row in range(18):
                    for col in range(18): rebuilt.putpixel((x+col,y+row),(8,16,24))
                scene = rebuilt
                return
            if not fallbacks or actual.size != gop[:2]: raise RuntimeError('Unexpected screen geometry')
            width,height,stride,fmt = gop
            columns,rows = min(columns,width//9),min(rows,height//18)
            if x > columns*9-18 or y > rows*18-18: raise RuntimeError('Pointer clamp not modelled')
            rebuilt = actual.copy()
            rebuilt.paste(scene.crop((0,0,columns*9,rows*18)),(0,0))
            for row in range(18):
                for col in range(18): rebuilt.putpixel((x+col,y+row),(8,16,24))
            scene = rebuilt
        for segment in range(12):
            dx,dy = [(1,1),(-1,1),(-1,-1),(1,-1)][segment%4]
            for step in range(50):
                vm.command(f'mouse_move {dx} {dy}')
                x = max(0,min(columns*9-18,x+dx))
                y = max(0,min(rows*18-18,y+dy))
                time.sleep(0.002)
            start = time.monotonic()
            target = expected(scene,x,y).tobytes()
            deadline = start+(90 if fallbacks else 5)  # V2c fallback: the GOP is redrawn in full once
            while time.monotonic() < deadline:
                actual = screenshot(vm,directory,f'move-{segment}-{captures}')
                captures += 1
                if actual.size != scene.size:
                    fit(actual)
                    target = expected(scene,x,y).tobytes()
                dx_sent,dy_sent = delivered()
                arrived = (dx_sent-baseline_x,dy_sent-baseline_y) == (x-initial_x,initial_y-y)
                if actual.tobytes() == target and arrived: break
                if not arrived:
                    vm.command('mouse_move 0 0')
                    syncs += 1
                vm.wait('KERNEL_POINTER_READY',1)
            else:
                vm.command('info irq')
                vm.command('info pic')
                final = exit_input(vm)
                (directory/'failure-state.json').write_text(json.dumps(dict(
                    ExpectedX=x,ExpectedY=y,FinalX=final.number('KERNEL_POINTER_X'),
                    FinalY=final.number('KERNEL_POINTER_Y'),Segment=segment,
                    Delivered=delivered(),Baseline=[baseline_x,baseline_y]),indent=2))
                raise RuntimeError('Cursor trails, deformation or missing progress')
            latencies.append(time.monotonic()-start)
        if stall:
            # R3-A: the recovery comes 5 s after the timeout; the moves may end before it.
            deadline = time.monotonic()+20
            while scene.size != gpu[:2]:
                if time.monotonic() > deadline: raise RuntimeError('Stall: no recovery to the GPU geometry')
                actual = screenshot(vm,directory,f'recovery-{captures}')
                captures += 1
                fit(actual)
            deadline = time.monotonic()+5
            while screenshot(vm,directory,f'recovered-{captures}').tobytes() != expected(scene,x,y).tobytes():
                captures += 1
                if time.monotonic() > deadline: raise RuntimeError('Stall: recovered screen mismatch')
        for row in range(18):
            # V2c: the console pixels live in the surface backing whenever the screen shows the GPU
            # scanout (stall: after the recovery); otherwise in the GOP aperture.
            origin = (log.number('KERNEL_VGPU_PRESENT_SURFACE')
                if log.has('KERNEL_VGPU_PRESENTING') and (not fallbacks or (stall and scene.size == gpu[:2])) else base)
            words = vm.physical(origin+((y+row)*stride+x)*4)
            for col in range(18):
                rgb = tuple(pointer.blend(pointer.sample(col,row),(8,16,24)))
                r,g,b = rgb
                color = r|(g<<8)|(b<<16) if fmt == 0 else b|(g<<8)|(r<<16)
                if words[col//2] >> (32*(col%2)) & 0xffffffff != color:
                    raise RuntimeError('Physical cursor mismatch')
        for count,key in enumerate(['e','x','i','t'],1):
            vm.command('sendkey '+key+' 20')
            vm.wait_text('PS2_KEY: ',5,count)
            time.sleep(0.04)
        cancelled = cancel_input(vm)
        if 'SHELL_EXIT_REQUEST' in cancelled.text or cancelled.has('KERNEL_POINTER_CLEAN'):
            raise RuntimeError('Esc stopped the interactive session')
        vm.command('mouse_move 3 2')
        x += 3; y += 2
        target = expected(scene,x,y).tobytes()
        deadline = time.monotonic()+5
        attempt = 0
        while time.monotonic() < deadline:
            actual = screenshot(vm,directory,f'after-cancel-{attempt}')
            if actual.size != scene.size:
                fit(actual)
                target = expected(scene,x,y).tobytes()
            if actual.tobytes() == target: break
            attempt += 1
            time.sleep(0.005)
        else: raise RuntimeError('Mouse/video did not recover after Esc')
        for count,key in enumerate(['h','e','l','p','ret'],6):
            vm.command('sendkey '+key+' 20')
            vm.wait_text('PS2_KEY: ',5,count)
        vm.wait_text('SHELL_HELP:')
        log = exit_input(vm)
        for marker in ['KERNEL_POINTER_CLEAN','KERNEL_CONSOLE_SHELL_CLEAN',
            'KERNEL_PS2_MOUSE_CLEANUP_OK','KERNEL_PS2_INPUT_CLEANUP_OK','KERNEL_INPUT_TASKS_COLLECTED']:
            if not log.has(marker): raise RuntimeError('Missing '+marker)
        if 'MOUSE_EVENT:' in log.text or 'KERNEL_POINTER_FRAME' in log.text:
            raise RuntimeError('Interactive image still logs every event/frame')
        if log.number('KERNEL_POINTER_X') != x or log.number('KERNEL_POINTER_Y') != y:
            raise RuntimeError('Final cursor position mismatch')
        dx_sent,dy_sent = delivered()
        if (dx_sent-baseline_x,dy_sent-baseline_y) != (x-initial_x,initial_y-y):
            raise RuntimeError('QEMU hardware packet sum differs from requested motion')
        # G5: the idle paging audit ran during the session and never failed.
        if not log.number('KERNEL_PAGING_AUDITS') or log.has('KERNEL_PAGING_AUDIT_FAILED'):
            raise RuntimeError('Paging audit missing or failed')
        if log.number('MOUSE_LOSSES'):
            raise RuntimeError('Input loss')
        session = qemu.serial.SerialLog(log.text[:log.text.index('KERNEL_INPUT_TASKS_COLLECTED')].rsplit(
            '# ============ scheduler cooperativo BSP',1)[-1])
        before = session.number('SCHEDULER_BUDGET_BEFORE')
        if before != session.number('SCHEDULER_BUDGET_AFTER'):
            raise RuntimeError('Physical allocation budget not restored')
        if 'Servicing hardware INT=0x42' not in mouse_trace.read_text(errors='replace'):
            raise RuntimeError('Missing hardware mouse IRQ evidence')
        ap_state = (load('console_ap_runtime','test/arch/x86/kernel/cpu/percpu/startup/runtime/firmware.py').attest_console(vm,log,smp)
            if log.has('KERNEL_AP_RUNTIME_READY') or log.has('KERNEL_AP_RUNTIME_BSP_ONLY') else None)
        vm.command('quit')
        vm.process.wait(timeout=5)
        if vm.process.returncode: raise RuntimeError('QEMU exit failure')
        translation = (translation_check(log,mouse_trace.read_text(errors='replace'),fallbacks,presenting,stall) if iommu else
            untranslated_check(log) if absent else None)
        return dict(ConfiguredVcpus=smp,OsCpusReady=ap_state['OsCpusInitialized'] if ap_state else 1,ApRuntime=ap_state,Soak=soak,Profile=profile_check(log),Translation=translation,Commands=600,Syncs=syncs,Scenes=12,Captures=captures,PhysicalPixels=324,
            EscCancelled=True,EscMouseProgress=True,CancelScenes=1,
            DrainSeconds=latencies,BudgetBefore=before,BudgetAfter=before,
            Flushes=log.number('KERNEL_CONSOLE_FLUSHES'),
            FlushPm=log.number('KERNEL_CONSOLE_FLUSH_TOTAL_PM'),
            SettlePm=log.number('KERNEL_CONSOLE_SETTLE_TOTAL_PM'))

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
    parser.add_argument('--smp',type=int,choices=(1,4),default=1)
    parser.add_argument('--soak-seconds',type=int,choices=(0,300),default=0)
    parser.add_argument('--image',type=Path)
    parser.add_argument('--audit',type=Path)
    parser.add_argument('--boots',type=int,choices=range(1,21),default=10)
    parser.add_argument('--iommu',action='store_true'); parser.add_argument('--iommu-absent',action='store_true')
    parser.add_argument('--present-stall',action='store_true')
    parser.add_argument('--present-fallback',action='store_true'); parser.add_argument('--iommu-nopresent',action='store_true')
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
    try:
        for index in range(1,args.boots+1):
            results.append(boot(args.image,out,index,args.iommu or args.present_fallback or args.iommu_nopresent or args.present_stall,
                args.iommu_absent,1 if args.present_fallback or args.present_stall else 0,not args.iommu_nopresent,args.present_stall,args.smp,args.soak_seconds))
            print(f'PASS: interactive cursor {index}/{args.boots}; pixels, movement, cleanup',flush=True)
    except Exception as failure:
        results.append(dict(Failure=str(failure)))
        qemu.finish(out,'interactive',args.image,results,len(results)-1,args.boots)
        raise
    if not qemu.finish(out,'interactive',args.image,results,len(results),args.boots): raise SystemExit(1)

if __name__ == '__main__':
    main()
