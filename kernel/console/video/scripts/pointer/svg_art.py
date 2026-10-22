import re
from PIL import Image, ImageDraw
from pathlib import Path
import xml.etree.ElementTree as ET
SVG = ET.parse(Path(__file__).with_name('left_ptr.svg')).getroot()
PATH = SVG.find('{http://www.w3.org/2000/svg}path')
D = PATH.attrib['d']

def points():
    tokens = re.findall(r'[MLHVCZ]|-?\d+\.?\d*', D)
    out, i, cmd, cur = [], 0, None, (0, 0)
    while i < len(tokens):
        if tokens[i].isalpha(): cmd = tokens[i]; i += 1
        if cmd == 'Z': break
        if cmd in 'ML': cur = (float(tokens[i]), float(tokens[i+1])); i += 2; out.append(cur)
        elif cmd == 'H': cur = (float(tokens[i]), cur[1]); i += 1; out.append(cur)
        elif cmd == 'V': cur = (cur[0], float(tokens[i])); i += 1; out.append(cur)
        else:
            p = [cur] + [(float(tokens[i+k]), float(tokens[i+k+1])) for k in (0, 2, 4)]; i += 6
            for s in range(1, 21):
                t = s / 20; u = 1 - t
                out.append(tuple(u**3*p[0][a] + 3*u*u*t*p[1][a] + 3*u*t*t*p[2][a] + t**3*p[3][a] for a in (0, 1)))
            cur = p[3]
    return out
# ============ supersampled fill and centered SVG stroke ============

def coverage(size=18, ss=16):
    pts = points()
    half = float(PATH.attrib['stroke-width']) / 2
    x0, y0 = min(x for x,y in pts)-half, min(y for x,y in pts)-half
    scale = size / (max(y for x,y in pts)+half-y0)
    coords = [((x-x0)*scale*ss,(y-y0)*scale*ss) for x,y in pts]
    fill = Image.new('L',(size*ss,size*ss))
    stroke = Image.new('L',fill.size)
    ImageDraw.Draw(fill).polygon(coords,fill=255)
    ImageDraw.Draw(stroke).line(coords+[coords[0]],fill=255,
        width=round(2*half*scale*ss),joint='curve')
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            black = white = 0
            for sy in range(ss):
                for sx in range(ss):
                    point = (x*ss+sx,y*ss+sy)
                    if stroke.getpixel(point): white += 1
                    elif fill.getpixel(point): black += 1
            w = (white*16+ss*ss//2)//(ss*ss)
            b = min(16-w,(black*16+ss*ss//2)//(ss*ss))
            row.append(b | (w << 5))
        rows.append(row)
    return rows

if __name__ == '__main__':
    print(coverage())
