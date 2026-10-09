import hashlib
import json
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[7]
MODULE = ROOT / 'arch/x86/kernel/drivers/video/framebuffer'
SOURCE = {'.c', '.h', '.rs', '.asm', '.cpp'}

# ============ source constraints and manifest ============

def main():
    errors, files, directories = [], [], {}
    candidates = list(MODULE.rglob('*')) + [ROOT / 'kernel/main.c', ROOT / 'kernel/scripts/build.ps1',
        ROOT / 'kernel/drivers/video/framebuffer/scripts/firmware.py']
    for path in candidates:
        if not path.is_file():
            continue
        data = path.read_bytes()
        text = data.decode('utf-8-sig')
        lines = text.splitlines()
        name = path.relative_to(ROOT).as_posix()
        if len(lines) > 400 and path.suffix in SOURCE:
            errors.append(name + ': over 400 lines')
        if re.search(r'[ \t]+$', text, re.M) or '\t' in text or re.search(r'\r(?!\n)', text):
            errors.append(name + ': whitespace')
        if re.search(r'\b(TODO|FIXME|HACK|XXX)\b', text) and path.suffix in SOURCE:
            errors.append(name + ': unfinished marker')
        if path.suffix in SOURCE and path.is_relative_to(MODULE):
            directories.setdefault(path.parent.relative_to(ROOT).as_posix(), []).append(path.name)
            if path.suffix in {'.c', '.rs'} and '// ======' not in text:
                errors.append(name + ': missing separator')
            for index, line in enumerate(lines[:-1]):
                if line.startswith('// ======') and lines[index + 1].strip():
                    errors.append(name + ': missing blank after separator')
        if path.suffix == '.md':
            for target in re.findall(r'\]\(([^)]+)\)', text):
                if '://' not in target and not target.startswith('#') and not (path.parent / target.split('#')[0]).exists():
                    errors.append(name + ': broken local link ' + target)
        files.append({'Path': name, 'Lines': len(lines), 'Sha256': hashlib.sha256(data).hexdigest().upper()})
    for name, children in directories.items():
        if len(children) > 5:
            errors.append(name + ': over five direct source files')
    out = ROOT / 'kernel/build/framebuffer-live-review.json'
    out.write_text(json.dumps({'Files': files, 'Directories': directories, 'Errors': errors,
        'MaxSourceLines': max(item['Lines'] for item in files if Path(item['Path']).suffix in SOURCE)},
        indent=2), encoding='utf-8')
    if errors:
        raise RuntimeError('\n'.join(errors))
    print('PASS: final source constraints and manifest ' + str(out))


if __name__ == '__main__':
    main()
