from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).absolute().parents[5]
MODULES = ('arch/x86/mm/paging', 'test/kernel/mm/paging')
SOURCE = {'.c', '.h', '.rs', '.ps1', '.py'}

# ============ main ============

def main():
    errors, files = [], []
    for module in MODULES:
        root = ROOT / module
        for folder in [root] + [p for p in root.rglob('*') if p.is_dir()]:
            direct = [p for p in folder.iterdir() if p.is_file() and p.suffix in SOURCE]
            if len(direct) > 5:
                errors.append(f'{folder}: more than five direct sources')
            for path in direct:
                data = path.read_bytes()
                content = data.decode('utf-8')
                lines = content.splitlines()
                if len(lines) > 400 or b'\r' in data or any(line != line.rstrip() for line in lines):
                    errors.append(f'{path}: size/LF/whitespace')
                if path.suffix in {'.c', '.h', '.rs'} and data.endswith(b'\n\n'):
                    errors.append(f'{path}: extra blank line at EOF')
                if any(line.startswith('\t') for line in lines):
                    errors.append(f'{path}: indentation')
                for ref in re.findall(r'#include\s+"([^"]+)"|#\[path\s*=\s*"([^"]+)"', content):
                    value = ref[0] or ref[1]
                    if not (path.parent / value).exists() and not (ROOT / value).exists():
                        errors.append(f'{path}: missing include {value}')
                files.append({'path': path.relative_to(ROOT).as_posix(), 'lines': len(lines),
                              'sha256': hashlib.sha256(data).hexdigest()})
    for module in MODULES:
        for path in (ROOT / module).rglob('*.md'):
            for ref in re.findall(r'\]\(([^\s)]+)\)', path.read_text(encoding='utf-8')):
                if '://' not in ref and not ref.startswith('#') and not (path.parent / ref).exists():
                    errors.append(f'{path}: missing link {ref}')
    out = ROOT / 'kernel/build/paging-owned-review.json'
    out.write_text(json.dumps({'sources': files, 'errors': errors}, indent=2), encoding='utf-8')
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'PASS: {len(files)} sources/scripts, maximum {max(p["lines"] for p in files)} lines')


if __name__ == '__main__':
    main()
