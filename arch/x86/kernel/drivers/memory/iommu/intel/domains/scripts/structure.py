"""Check the VT-d private domain module and its authorized paging/build adapters."""
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[9]
SUFFIXES={'.c','.h','.rs','.py','.ps1'}

# ====== main ======

def main():
    paths=[]; errors=[]; manifest=[]
    for name in ('arch/x86/kernel/drivers/memory/iommu/intel/domains',
                 'test/arch/x86/kernel/drivers/memory/iommu/intel/domains',
                 'arch/x86/mm/paging/iommu'):
        folder=ROOT/name
        for directory in [folder,*[p for p in folder.rglob('*') if p.is_dir()]]:
            if sum(p.is_file() and p.suffix in SUFFIXES for p in directory.iterdir())>5:
                errors.append(f'Folder limit: {directory}')
        paths.extend(p for p in folder.rglob('*') if p.is_file() and p.suffix in SUFFIXES|{'.md'})
    adapters=('kernel/main.c','kernel/scripts/build.ps1','kernel/scripts/build/profiles.ps1',
        'kernel/scripts/build/iommu-registers.ps1','arch/x86/mm/paging/inventory.h',
        'arch/x86/mm/paging/inventory.c','arch/x86/mm/paging/authorization/validation.c',
        'arch/x86/mm/paging/gpu/validation.c','arch/x86/mm/paging/scripts/test.ps1',
        'kernel/drivers/video/virtio/resources/scripts/test.ps1','test/kernel/host.ps1',
        'arch/x86/kernel/drivers/video/virtio/scripts/test.ps1',
        'arch/x86/kernel/drivers/video/framebuffer/scripts/test.ps1',
        'arch/x86/kernel/drivers/memory/iommu/intel/discovery/scripts/firmware.py',
        'test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/probe.h',
        'test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/probe.c')
    paths.extend(ROOT/name for name in adapters)
    for path in paths:
        raw=path.read_bytes(); content=raw.decode('utf-8')
        if path.suffix in SUFFIXES:
            if len(content.splitlines())>400 or b'\r' in raw or any(line.rstrip()!=line for line in content.splitlines()):
                errors.append(f'Format/size: {path}')
            manifest.append(dict(Path=str(path.relative_to(ROOT)),Lines=len(content.splitlines()),SHA256=hashlib.sha256(raw).hexdigest()))
        for a,b in re.findall(r'#include\s+"([^"]+)"|#\[path\s*=\s*"([^"]+)"',content):
            value=a or b
            if not (path.parent/value).exists() and not (ROOT/value).exists(): errors.append(f'Include: {path}: {value}')
        if path.suffix=='.md':
            for value in re.findall(r'\]\(([^\s)]+)\)',content):
                if '://' not in value and not value.startswith('#') and not (path.parent/value.split('#')[0]).exists():
                    errors.append(f'Link: {path}: {value}')
    (ROOT/'kernel/build/iommu-domains-structure.json').write_text(json.dumps(dict(Sources=manifest,Errors=errors),indent=2))
    if errors: raise SystemExit('\n'.join(errors))
    print(f'PASS {len(manifest)} VT-d sources/adapters: size, folders, includes, links, LF')

if __name__=='__main__': main()
