import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location('pointer_provider',ROOT/'arch/x86/kernel/drivers/input/ps2/scripts/firmware.py')
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)

# ============ boot the prepared ESP without opening a window ============

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--session',type=Path,required=True)
    args = parser.parse_args()
    session = args.session.resolve()
    if not session.parent.is_relative_to((ROOT/'kernel/build').resolve()):
        raise RuntimeError('Session outside workspace build directory')
    description = json.loads(session.read_text(encoding='utf-8-sig'))
    interactive = b'KERNEL_POINTER_READY' in (session.parent/'esp/EFI/BOOT/kernel.elf').read_bytes()
    arguments = description['Arguments'][:]
    if arguments[arguments.index('-display')+1] != 'sdl,show-cursor=off':
        raise RuntimeError('Unexpected visible launcher profile')
    arguments[arguments.index('-display')+1] = 'none'
    arguments[arguments.index('-monitor')+1] = 'stdio'
    serial = Path(description['Serial'])
    vm = None
    with (session.parent/'smoke-monitor.log').open('w') as monitor, (session.parent/'smoke-stderr.log').open('w') as error:
        try:
            vm = subprocess.Popen([description['Qemu'],*arguments],stdin=subprocess.PIPE,
                stdout=monitor,stderr=error,text=True,creationflags=subprocess.CREATE_NO_WINDOW)
            initial = provider.wait_marker(vm,serial,'KERNEL_POINTER_READY' if interactive else 'KERNEL_POINTER_FRAME',80)
            provider.command(vm,'mouse_move 17 9')
            if not interactive:
                provider.wait_marker(vm,serial,'MOUSE_EVENT: 0x0000000000000011 -0x0000000000000009')
            for index,key in enumerate(['h','e','l','p','ret'],1):
                provider.command(vm,'sendkey '+key+' 20')
                provider.wait_marker(vm,serial,'PS2_KEY: ',5,index)
            provider.wait_marker(vm,serial,'SHELL_HELP:')
            cancelled = provider.cancel_input(vm,serial)
            if 'KERNEL_POINTER_CLEAN' in cancelled:
                raise RuntimeError('Esc stopped the session')
            log = provider.exit_input(vm,serial)
            if interactive:
                x = (provider.number(initial,'KERNEL_FB_WIDTH')//9*9-18)//2+17
                y = (provider.number(initial,'KERNEL_FB_HEIGHT')//18*18-18)//2+9
                if provider.number(log,'KERNEL_POINTER_X') != x or provider.number(log,'KERNEL_POINTER_Y') != y:
                    raise RuntimeError('Interactive mouse movement missing')
            for marker in ['KERNEL_POINTER_CLEAN','KERNEL_CONSOLE_SHELL_CLEAN','KERNEL_PS2_MOUSE_CLEANUP_OK',
                           'KERNEL_PS2_INPUT_CLEANUP_OK','KERNEL_SCHEDULER_CLEANUP_OK']:
                if marker not in log: raise RuntimeError('Missing '+marker)
            (session.parent/'smoke-results.json').write_text(json.dumps(dict(
                Image=description['ImageHash'],Loader=description['LoaderHash'],
                VisibleBackend='sdl,show-cursor=off',TestBackend='none',Mouse=True,Keyboard=True,
                Cleanup=True,Serial=str(serial)),indent=2),encoding='utf-8')
            provider.command(vm,'quit'); vm.wait(timeout=5)
            if vm.returncode: raise RuntimeError('QEMU exited with error')
            print('PASS: prepared interactive ESP boots; mouse, help, Esc cancellation, exit cleanup; '+str(session.parent))
        finally:
            if vm is not None:
                if vm.poll() is None: vm.kill()
                vm.wait(timeout=5); vm.stdin.close()

if __name__ == '__main__':
    main()
