"""Validate source limits and references for the PCI topology block."""
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[8]
CODE={'.c','.h','.rs','.ps1','.py'}

# ====== main ======

def main():
    files=[]; errors=[]; records=[]
    for name in ('arch/x86/kernel/drivers/bus/pci/topology','test/arch/x86/kernel/drivers/bus/pci/topology'):
        folder=ROOT/name
        for directory in [folder,*[p for p in folder.rglob('*') if p.is_dir()]]:
            if sum(p.is_file() and p.suffix in CODE for p in directory.iterdir())>5: errors.append(f'Folder limit {directory}')
        files.extend(p for p in folder.rglob('*') if p.is_file() and p.suffix in CODE|{'.md'})
    files.extend(ROOT/name for name in ('kernel/scripts/build.ps1','kernel/scripts/build/profiles.ps1',
        'kernel/scripts/build/iommu-discovery.ps1','test/arch/x86/kernel/drivers/bus/pci/ecam/integration/probe.c',
        'arch/x86/kernel/drivers/bus/pci/ecam/scripts/firmware.py',
        'docs/design/consolidamento.md','docs/status.md','docs/future-work.md'))
    for path in files:
        raw=path.read_bytes(); text=raw.decode('utf-8')
        if b'\r' in raw or any(line.rstrip()!=line for line in text.splitlines()): errors.append(f'Format {path}')
        if path.suffix in CODE:
            if len(text.splitlines())>400 or b'\t' in raw: errors.append(f'Size/indentation {path}')
            records.append(dict(Path=str(path.relative_to(ROOT)),Lines=len(text.splitlines()),SHA256=hashlib.sha256(raw).hexdigest()))
        for a,b in re.findall(r'#include\s+"([^"]+)"|#\[path\s*=\s*"([^"]+)"',text):
            value=a or b
            if not (path.parent/value).exists() and not (ROOT/value).exists(): errors.append(f'Include {path}: {value}')
        if path.suffix=='.md':
            for value in re.findall(r'\]\(([^\s)]+)\)',text):
                if '://' not in value and not (path.parent/value.split('#')[0]).exists(): errors.append(f'Link {path}: {value}')
    (ROOT/'kernel/build/pci-topology-structure.json').write_text(json.dumps(dict(Sources=records,Errors=errors),indent=2))
    if errors: raise SystemExit('\n'.join(errors))
    print(f'PASS {len(records)} sources/adapters; size, folders, LF, whitespace, references')

if __name__=='__main__': main()
