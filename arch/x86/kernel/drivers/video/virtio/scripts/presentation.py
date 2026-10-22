"""Diagnostic-only screenshot comparison and bounded guest checkpoints."""
import re

# ====== pixels ======

def pixels(path,width,height,phase):
    raw=path.read_bytes()
    header=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255[\r]?\n',raw)
    if not header or (int(header[1]),int(header[2]))!=(width,height):
        raise RuntimeError('Unexpected screenshot geometry/header')
    image=raw[header.end():]
    if len(image)!=width*height*3: raise RuntimeError('Incomplete screenshot')
    expected=bytearray(len(image))
    for y in range(height):
        for x in range(width):
            color=(224,64,160) if phase==2 and 16<=x<80 and 24<=y<56 else ((x//32)&255,(y//16)&255,(x^y)&255)
            at=(y*width+x)*3
            expected[at:at+3]=bytes(color)
    if image!=expected:
        first=next(index for index,(a,b) in enumerate(zip(image,expected)) if a!=b)
        raise RuntimeError(f'Screenshot mismatch phase{phase} pixel{first//3}: {image[first]} != {expected[first]}')
    return width*height

# ====== checkpoint ======

def checkpoint(backend,control,transport,directory,log,phase):
    number=backend.protocol.number
    address=number(log,'KERNEL_VGPU_SCANOUT_MAILBOX')
    width=number(log,'KERNEL_VGPU_RESOURCE_WIDTH'); height=number(log,'KERNEL_VGPU_RESOURCE_HEIGHT')
    if not 0x100000<=address<=0x08000000-4 or address&4095!=4092:
        raise RuntimeError('Invalid diagnostic mailbox')
    backend.qmp(control,'stop')
    path=directory/f'frame-{phase}.ppm'
    backend.qmp(control,'screendump',dict(filename=path.as_posix()))
    checked=pixels(path,width,height,phase)
    backend.qtest(transport,f'writel {address:#x} {phase:#x}')
    backend.qmp(control,'cont')
    return checked
