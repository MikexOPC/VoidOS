"""Check VirtIO discovery, loader acquisition and their centralized tests."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[6]
SOURCE = {'.c','.h','.rs','.py','.ps1','.asm'}

# ====== main ======

def main():
    errors, manifest = [], []
    for name in ('kernel/drivers/video/virtio/discovery','test/kernel/drivers/video/virtio/discovery',
                 'arch/x86/boot/uefi/pci','test/arch/x86/boot/uefi/pci',
                 'kernel/drivers/video/virtio/mapping','test/kernel/drivers/video/virtio/mapping',
                 'arch/x86/kernel/drivers/video/virtio','test/arch/x86/kernel/drivers/video/virtio',
                 'arch/x86/mm/paging/gpu','kernel/drivers/video/virtio/initialization',
                 'test/kernel/drivers/video/virtio/initialization','kernel/drivers/video/virtio/queues',
                 'test/kernel/drivers/video/virtio/queues','kernel/drivers/video/virtio/resources',
                 'test/kernel/drivers/video/virtio/resources','arch/x86/mm/paging/surface'):
        folder = ROOT/name
        for directory in [folder,*[p for p in folder.rglob('*') if p.is_dir()]]:
            if sum(p.is_file() and p.suffix in SOURCE for p in directory.iterdir()) > 5:
                errors.append(f'Too many direct sources: {directory}')
        for path in folder.rglob('*'):
            if not path.is_file() or path.suffix not in SOURCE|{'.md'}: continue
            raw = path.read_bytes()
            text = raw.decode('utf-8')
            if path.suffix in SOURCE:
                if len(text.splitlines()) > 400 or b'\r' in raw or any(line.rstrip()!=line for line in text.splitlines()):
                    errors.append(f'Size/format violation: {path}')
                manifest.append(dict(Path=str(path.relative_to(ROOT)),Lines=len(text.splitlines()),SHA256=hashlib.sha256(raw).hexdigest()))
            for reference in re.findall(r'#include\s+"([^"]+)"|#\[path\s*=\s*"([^"]+)"',text):
                value=reference[0] or reference[1]
                if not (path.parent/value).exists() and not (ROOT/value).exists(): errors.append(f'Missing include: {path}: {value}')
            if path.suffix=='.md':
                for value in re.findall(r'\]\(([^\s)]+)\)',text):
                    if '://' not in value and not value.startswith('#') and not (path.parent/value.split('#')[0]).exists():
                        errors.append(f'Missing link: {path}: {value}')
    (ROOT/'kernel/build/virtio-pci-structure.json').write_text(json.dumps(dict(Sources=manifest,Errors=errors),indent=2),encoding='utf-8')
    if errors: raise SystemExit('\n'.join(errors))
    print(f'PASS: {len(manifest)} discovery sources, size/folders/includes/LF')

if __name__=='__main__': main()
