"""Source size, local references, layout and immutable image evidence."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[5]
MODULES = ('kernel/drivers/video/framebuffer', 'arch/x86/boot/uefi/graphics',
           'test/kernel/drivers/video/framebuffer', 'test/arch/x86/boot/uefi/graphics')
SOURCE = {'.c', '.h', '.rs', '.ps1', '.py'}

# ============ module review ============

def main():
    errors, manifest = [], []
    for module in MODULES:
        for folder in [ROOT / module] + [p for p in (ROOT / module).rglob('*') if p.is_dir()]:
            files = [p for p in folder.iterdir() if p.is_file() and p.suffix in SOURCE]
            if len(files) > 5:
                errors.append(f'{folder}: {len(files)} direct sources')
        for path in (ROOT / module).rglob('*'):
            if not path.is_file() or path.suffix not in SOURCE | {'.md'}:
                continue
            data = path.read_bytes()
            text = data.decode('utf-8')
            name = path.relative_to(ROOT).as_posix()
            if path.suffix in SOURCE:
                lines = text.splitlines()
                if len(lines) > 400 or b'\r' in data or any(line != line.rstrip() for line in lines):
                    errors.append(f'{name}: size/LF/whitespace violation')
                if path.suffix in {'.c', '.rs', '.py'} and any(line.startswith('\t') for line in lines):
                    errors.append(f'{name}: indentation tab')
                if path.suffix == '.rs' and 'rust' not in path.parts and 'test' not in path.parts:
                    errors.append(f'{name}: Rust outside rust/ or test/')
                manifest.append({'path': name, 'lines': len(lines),
                                 'sha256': hashlib.sha256(data).hexdigest()})
            for ref in re.findall(r'#include\s+"([^"]+)"|#\[path\s*=\s*"([^"]+)"', text):
                value = ref[0] or ref[1]
                if not (path.parent / value).exists() and not (ROOT / value).exists():
                    errors.append(f'{name}: missing include {value}')
            if path.suffix == '.md':
                for value in re.findall(r'\]\(([^\s)]+)\)', text):
                    if '://' not in value and not value.startswith('#') and not (path.parent / value).exists():
                        errors.append(f'{name}: missing link {value}')
    out = ROOT / 'kernel/build/framebuffer-gop-source-final.json'
    out.write_text(json.dumps({'sources': manifest, 'errors': errors}, indent=2), encoding='utf-8')
    for error in errors:
        print(error)
    if errors:
        raise SystemExit(1)
    print(f'PASS: {len(manifest)} sources/scripts, maximum {max(p["lines"] for p in manifest)} lines')


if __name__ == '__main__':
    main()
