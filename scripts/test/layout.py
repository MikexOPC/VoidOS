import ast
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = {'.c', '.h', '.cpp', '.rs', '.asm', '.ps1', '.py'}
MAX_SOURCE_LINES = 1000
MAX_DIRECTORY_SOURCES = 8

# ============ centralized test layout ============

def main():
    errors, files, imports = [], [], 0
    for base in ('kernel', 'arch', 'lib'):
        for path in (ROOT / base).rglob('test'):
            if 'build' in path.parts:
                continue
            if path.is_dir() and any(p.is_file() for p in path.rglob('*')):
                errors.append('Local test files remain: ' + str(path.relative_to(ROOT)))
    for path in (ROOT / 'test').rglob('*'):
        if not path.is_file() or path.suffix not in SOURCE:
            continue
        text = path.read_text(encoding='utf-8-sig')
        lines = text.splitlines()
        files.append({'Path': path.relative_to(ROOT).as_posix(), 'Lines': len(lines)})
        if len(lines) > MAX_SOURCE_LINES:
            errors.append(str(path.relative_to(ROOT)) + f': over {MAX_SOURCE_LINES} lines')
        if len([p for p in path.parent.iterdir() if p.is_file() and p.suffix in SOURCE]) > MAX_DIRECTORY_SOURCES:
            errors.append(str(path.parent.relative_to(ROOT)) + f': over {MAX_DIRECTORY_SOURCES} source files')
        if path.suffix == '.py':
            ast.parse(text, filename=str(path))
        for target in re.findall(r'(?:#include\s+"|#\[path\s*=\s*")([^"\n]+)', text):
            if target.startswith('.'):
                imports += 1
                if not (path.parent / target).is_file():
                    errors.append(str(path.relative_to(ROOT)) + ': missing ' + target)
    output = ROOT / 'kernel/build/test-layout/layout.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    report = {'Files': files, 'FileCount': len(files), 'RelativeImports': imports,
              'MaxLines': max((item['Lines'] for item in files), default=0),
              'Limits': {'SourceLines': MAX_SOURCE_LINES, 'DirectorySources': MAX_DIRECTORY_SOURCES},
              'Errors': sorted(set(errors))}
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    if errors:
        raise RuntimeError('\n'.join(report['Errors']))
    print('PASS: centralized test layout; ' + str(output))


if __name__ == '__main__':
    main()
