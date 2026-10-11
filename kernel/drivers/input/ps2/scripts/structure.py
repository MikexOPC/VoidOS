"""Check the PS/2 input pipeline's source layout and local references."""
from pathlib import Path
import json
import re
import hashlib

ROOT = Path(__file__).resolve().parents[5]
MODULES = (
    'kernel/drivers/input/ps2', 'kernel/input/keyboard', 'kernel/input/integration',
    'kernel/console/line', 'kernel/shell', 'arch/x86/kernel/drivers/input/ps2',
    'test/kernel/drivers/input/ps2', 'test/kernel/input/keyboard',
    'test/kernel/console/line', 'test/kernel/shell', 'test/arch/x86/kernel/drivers/input/ps2',
)
SOURCE = {'.c', '.h', '.rs', '.asm', '.ps1', '.py'}

# ============ validate structure and references ============

def main():
    errors = []
    manifest = []
    includes = {}
    for module in MODULES:
        direct = [p for p in (ROOT / module).iterdir() if p.suffix in SOURCE and p.is_file()]
        if len(direct) > 5:
            errors.append(f'{module}: {len(direct)} direct source files')
        for path in (ROOT / module).rglob('*'):
            if path.is_dir():
                direct = [p for p in path.iterdir() if p.suffix in SOURCE and p.is_file()]
                if len(direct) > 5:
                    errors.append(f'{path}: {len(direct)} direct source files')
                continue
            if path.suffix not in SOURCE | {'.md'}:
                continue
            text = path.read_text(encoding='utf-8')
            lines = text.splitlines()
            relative = path.relative_to(ROOT).as_posix()
            if path.suffix in SOURCE:
                if len(lines) > 400:
                    errors.append(f'{relative}: {len(lines)} lines')
                if any(line.rstrip() != line for line in lines):
                    errors.append(f'{relative}: trailing whitespace')
                if b'\r' in path.read_bytes():
                    errors.append(f'{relative}: expected LF')
                if path.suffix == '.rs' and 'rust' not in path.parts and 'test' not in path.parts:
                    errors.append(f'{relative}: Rust outside rust/ or test/')
                if path.suffix == '.c' and 'c' in path.relative_to(ROOT).parts:
                    errors.append(f'{relative}: C in language directory')
                manifest.append({'path': relative, 'lines': len(lines),
                                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
            for ref in re.findall(r'#include\s+"([^"]+)"|#\[path\s*=\s*"([^"]+)"', text):
                value = ref[0] or ref[1]
                candidate = path.parent / value
                if not candidate.exists():
                    candidate = ROOT / value
                if not candidate.exists():
                    errors.append(f'{relative}: missing source {value}')
                elif ref[0]:
                    includes.setdefault(path.resolve(), []).append(candidate.resolve())
            if path.suffix == '.md':
                for value in re.findall(r'\]\(([^\s)]+)\)', text):
                    if '://' in value or value.startswith('#'):
                        continue
                    if not (path.parent / value.split('#')[0]).exists():
                        errors.append(f'{relative}: missing link {value}')
    visiting, visited = set(), set()

    def visit(path):
        if path in visiting:
            errors.append(f'{path.relative_to(ROOT)}: circular local include')
            return
        if path in visited:
            return
        visiting.add(path)
        for dependency in includes.get(path, []):
            visit(dependency)
        visiting.remove(path)
        visited.add(path)

    for path in includes:
        visit(path)
    out = ROOT / 'kernel/build/ps2-structure-check.json'
    out.write_text(json.dumps({'sources': manifest, 'errors': errors}, indent=2), encoding='utf-8')
    for error in errors:
        print(error)
    if errors:
        raise SystemExit(1)
    print(f'PASS: {len(manifest)} sources; layout, UTF-8, LF, size and references')


if __name__ == '__main__':
    main()
