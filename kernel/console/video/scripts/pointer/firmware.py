import importlib.util
import json
from pathlib import Path
import re
import time
ROOT = Path(__file__).resolve().parents[5]
COVERAGE = [
    [0, 0, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 192, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0],
    [0, 512, 192, 192, 192, 192, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0],
    [512, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0],
    [512, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 512, 512, 0, 0, 0, 0, 0],
    [512, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0],
    [512, 192, 192, 192, 192, 192, 512, 512, 512, 512, 512, 512, 512, 0, 0, 0, 0, 0],
    [512, 192, 192, 192, 192, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [512, 192, 192, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [512, 192, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    [512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
]

# ============ bitmap coverage reference ============

def sample(x, y):
    return COVERAGE[y][x] if 0 <= x < 18 and 0 <= y < 18 else 0

def blend(pixel, rgb):
    black, white = pixel & 31, pixel >> 5
    return bytes((c*(16-black-white)+255*white+8)//16 for c in rgb)

# Console cell in pixels: must equal WIDTH/HEIGHT of scripts/font/convert.py (checked in verify()).
CELL_W, CELL_H = 9, 18

def expected_position(width, height, packets):
    if not 18 <= width <= 1920 or not 32 <= height <= 1080:
        raise RuntimeError('Invalid pointer geometry')
    width, height = width // CELL_W * CELL_W, height // CELL_H * CELL_H
    x, y, moves = (width-18)//2, (height-18)//2, 0
    for dx, dy, buttons in packets:
        if not -256 <= dx <= 255 or not -256 <= dy <= 255 or not 0 <= buttons <= 7:
            raise RuntimeError('Invalid expected mouse packet')
        next_x, next_y = max(0,min(width-18,x+dx)), max(0,min(height-18,y-dy))
        moves += (next_x,next_y) != (x,y)
        x, y = next_x, next_y
    return x, y, moves

def load(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def number(log, field):
    match = re.search(re.escape(field) + r': 0x([0-9a-fA-F]{16})',log)
    if not match:
        raise RuntimeError('Missing pointer evidence: ' + field)
    return int(match[1],16)

# ============ validate framebuffer geometry ============

def geometry(log):
    width,height,stride,base,format_id,used,capacity = [number(log,'KERNEL_FB_'+field) for field in
        ['WIDTH','HEIGHT','STRIDE','BASE','FORMAT','USED','CAPACITY']]
    expected_position(width,height,[])
    if (format_id not in (0,1) or not width <= stride <= 0xffffffff or not 4096 <= base < (1<<52)
            or used != stride*height*4 or capacity < used or capacity > (1<<52)-base):
        raise RuntimeError('Invalid bounded framebuffer geometry')
    return width,height,stride,base,format_id

# ============ wait for a fully rendered scene ============

def settled(vm, serial, packets, read_serial, wait_marker):
    pattern = ''.join(r'KERNEL_POINTER_FRAME_' + field + r': 0x([0-9a-fA-F]{16})\n'
                      for field in ['X','Y','VISIBLE','MOVES','CELLS'])
    deadline = time.monotonic()+10
    while time.monotonic() < deadline:
        wait_marker(vm,serial,'KERNEL_PS2_TASK_POLL_READY',1)
        log = read_serial(serial)
        width, height = number(log,'KERNEL_FB_WIDTH'), number(log,'KERNEL_FB_HEIGHT')
        target = expected_position(width,height,packets)
        events = list(re.finditer(r'(?m)^(?:\[[^]]+\]\s+)?(?:PS2_LINE_READY:|SHELL_(?:HELP:|ECHO:|STATUS:|INPUT_|EMPTY|BAD_ARGUMENTS|UNKNOWN_COMMAND|LINE_REJECTED_TRUNCATED))',log))
        latest = events[-1].start() if events else 0
        frames = list(re.finditer(pattern,log))
        if frames:
            frame = frames[-1]
            x,y,visible,moves,cells = [int(value,16) for value in frame.groups()]
            if frame.start() > latest and visible == 1 and (x,y,moves) == target and cells:
                return log,(x,y,moves,cells)
        time.sleep(0.005)
    raise TimeoutError('Pointer scene did not settle')

# ============ fresh physical reads, excluding prior HMP replies ============

def physical(vm, monitor, address):
    offset = monitor.stat().st_size
    vm.stdin.write(f'xp /9gx 0x{address:x}\n'); vm.stdin.flush()
    deadline = time.monotonic()+5
    while time.monotonic() < deadline:
        values = {}
        text = monitor.read_bytes()[offset:].decode('utf-8',errors='replace')
        for match in re.finditer(r'(?m)^([0-9a-fA-F]{8,16}):\s*((?:0x[0-9a-fA-F]{16}\s*)+)',text):
            start = int(match[1],16)
            for index,value in enumerate(re.findall(r'0x([0-9a-fA-F]{16})',match[2])):
                values[start+index*8] = int(value,16)
        if all(address+index*8 in values for index in range(9)):
            return [values[address+index*8] for index in range(9)]
        if vm.poll() is not None:
            raise RuntimeError('Owned VM exited before pointer physical read')
        time.sleep(0.005)
    raise TimeoutError('Pointer physical pixels missing')

# ============ live pixel and screenshot comparison ============

def verify(vm, scenario, serial, packets, read_serial, wait_marker, command, label):
    log,frame = settled(vm,serial,packets,read_serial,wait_marker)
    x,y,moves,cells_done = frame
    width,height,stride,base,format_id = geometry(log)
    reference = load('pointer_terminal','kernel/console/video/scripts/shell/pixels.py')
    converter = load('pointer_font','arch/x86/kernel/drivers/video/framebuffer/scripts/font/convert.py')
    if (converter.WIDTH, converter.HEIGHT) != (CELL_W, CELL_H): raise RuntimeError('Font cell differs from the pointer reference')
    font = converter.raster()
    cells,caret = reference.expected(log,width//CELL_W,height//CELL_H)
    expected = bytearray(width*height*3)
    for index,scalar in enumerate(cells):
        cx,cy = index%(width//CELL_W)*CELL_W,index//(width//CELL_W)*CELL_H
        for row,bits in enumerate(font.get(scalar,font[65533])):
            if index == caret and row >= CELL_H-2:
                bits = (1<<CELL_W)-1
            for col in range(CELL_W):
                rgb = b'\xff\xff\xff' if bits & (1<<(CELL_W-1-col)) else b'\x08\x10\x18'
                offset = ((cy+row)*width+cx+col)*3
                expected[offset:offset+3] = rgb
    for row in range(18):
        for col in range(18):
            pixel = sample(col,row)
            if pixel:
                offset = ((y+row)*width+x+col)*3
                expected[offset:offset+3] = blend(pixel, expected[offset:offset+3])
    command(vm,'stop')
    image = scenario / ('pointer-'+label+'.ppm')
    command(vm,'screendump "'+image.as_posix()+'"')
    deadline = time.monotonic()+5
    while time.monotonic() < deadline:
        data = image.read_bytes() if image.exists() else b''
        header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
        if header and len(data)-header.end() == width*height*3:
            break
        time.sleep(0.01)
    else:
        raise TimeoutError('Pointer screenshot incomplete')
    if (int(header[1]),int(header[2])) != (width,height):
        raise RuntimeError('Pointer screenshot geometry mismatch')
    visible = width*(height//CELL_H*CELL_H)*3
    if data[header.end():header.end()+visible] != expected[:visible]:
        raise RuntimeError('Pointer scene or restored background mismatch: '+label)
    for row in range(18):
        words = physical(vm,scenario/'monitor.log',base+((y+row)*stride+x)*4)
        for col in range(18):
            offset = ((y+row)*width+x+col)*3
            r,g,b = expected[offset:offset+3]
            rgb = r | g<<8 | b<<16 if format_id == 0 else b | g<<8 | r<<16
            if words[col//2] >> ((col%2)*32) & 0xffffffff != rgb:
                raise RuntimeError('Pointer physical pixel mismatch')
    result = dict(X=x,Y=y,Moves=moves,Cells=cells_done,PhysicalPixels=324,VisiblePixels=visible//3)
    (scenario/('pointer-'+label+'.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
    command(vm,'cont')
    return result

# ============ boundary movement without new keyboard input ============

def exercise(vm, scenario, serial, packets, read_serial, wait_marker, command, mouse):
    results = [verify(vm,scenario,serial,packets,read_serial,wait_marker,command,'initial')]
    for label,dx,dy in [('bottom-right',127,127),('top-left',-127,-127)]:
        for _ in range(8):
            command(vm,f'mouse_move {dx} {dy}')
            packets.append((dx,-dy,0))
            deadline = time.monotonic()+5
            while time.monotonic() < deadline:
                wait_marker(vm,serial,'KERNEL_PS2_TASK_POLL_READY',1)
                if len(mouse.packets(read_serial(serial))) >= len(packets):
                    if mouse.packets(read_serial(serial)) != packets:
                        raise RuntimeError('Boundary mouse packet mismatch')
                    break
                time.sleep(0.005)
            else:
                raise TimeoutError('Boundary mouse packet missing')
        results.append(verify(vm,scenario,serial,packets,read_serial,wait_marker,command,label))
    results.append(continuous(vm, scenario, serial, packets, read_serial, wait_marker, command, mouse))
    return results

# ============ continuous input without waiting for each packet ============

def continuous(vm, scenario, serial, packets, read_serial, wait_marker, command, mouse):
    prefix = list(packets)
    start = time.monotonic()
    for _ in range(600):
        command(vm, 'mouse_move 1 1')
        time.sleep(0.005)
    submitted = time.monotonic()
    deadline = submitted + 3
    while time.monotonic() < deadline:
        wait_marker(vm, serial, 'KERNEL_PS2_TASK_POLL_READY', 1)
        actual = mouse.packets(read_serial(serial))
        if actual[:len(prefix)] != prefix:
            raise RuntimeError('Continuous mouse changed prior packet order')
        tail = actual[len(prefix):]
        if any(buttons != 0 for _, _, buttons in tail):
            raise RuntimeError('Unexpected continuous mouse button')
        totals = sum(dx for dx, _, _ in tail), sum(dy for _, dy, _ in tail)
        if totals == (600, -600):
            packets[:] = actual
            drained = time.monotonic()
            frame = verify(vm, scenario, serial, packets, read_serial, wait_marker, command, 'continuous')
            frame.update(InputCommands=600, InputSeconds=submitted-start,
                         DrainSeconds=drained-submitted)
            (scenario/'pointer-continuous.json').write_text(json.dumps(frame, indent=2), encoding='utf-8')
            return frame
        if totals[0] > 600 or totals[1] < -600:
            raise RuntimeError('Continuous mouse duplicated movement')
        time.sleep(0.005)
    raise TimeoutError('Continuous mouse lost movement or failed to drain within 3 seconds')

def finish(log, packets):
    target = expected_position(number(log,'KERNEL_FB_WIDTH'),number(log,'KERNEL_FB_HEIGHT'),packets)
    actual = tuple(number(log,'KERNEL_POINTER_'+field) for field in ['X','Y','MOVES'])
    if actual != target or 'KERNEL_POINTER_CLEAN' not in log:
        raise RuntimeError('Pointer cleanup or final geometry mismatch')
    return dict(X=actual[0],Y=actual[1],Moves=actual[2])
