import re
from PIL import Image, ImageDraw, ImageFilter
D = ("M201.163 133.54L201.149 133.528L201.134 133.515L91.6855 36.4935C86.5144 31.7659 81.4269 27.9549 76.5421 25.525C71.7671 23.1497 66.0861 21.5569 60.4133 23.1213C54.3118 24.8039 50.4875 29.4674 48.3639 34.759C46.3122 39.8715 45.4999 46.2787 45.4999 53.5383L45.4999 200.431V200.493L45.5008 200.555C45.6218 208.862 50.4279 217.843 55.9963 223.894C58.8934 227.043 62.5163 229.986 66.6704 231.742C70.9172 233.537 76.217 234.254 81.4691 231.884C85.7536 229.951 89.6754 226.055 92.8565 222.651C94.6841 220.695 96.8336 218.252 99.0355 215.749C100.71 213.847 102.414 211.91 104.03 210.126C112.189 201.122 121.346 192.286 132.161 187.407C143.013 182.511 155.809 181.375 167.963 181.146C170.959 181.089 173.85 181.087 176.65 181.085H176.663H176.686C179.447 181.083 182.164 181.081 184.662 181.019C189.231 180.906 194.643 180.609 198.777 178.88C208.711 174.723 210.972 163.838 210.753 156.445C210.521 148.596 207.57 139.272 201.163 133.54Z")
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
def art(size=18, ss=16, wide=None):
    pts = points(); half = 8.5
    x0, y0 = min(p[0] for p in pts) - half, min(p[1] for p in pts) - half
    y1 = max(p[1] for p in pts) + half
    scale = size / (y1 - y0)
    W = size * ss
    img = Image.new('L', (W, W), 0)
    ImageDraw.Draw(img).polygon([((x - x0) * scale * ss, (y - y0) * scale * ss) for x, y in pts], fill=255)
    r = int(round(half * scale * ss)); k = 2 * r + 1
    outer = img.filter(ImageFilter.MaxFilter(k)); inner = img.filter(ImageFilter.MinFilter(k))
    rows = []
    for y in range(size):
        row = ''
        for x in range(size):
            def cov(m): return sum(m.getpixel((x*ss+a, y*ss+b)) > 0 for a in range(ss) for b in range(ss)) / (ss * ss)
            row += '.' if cov(outer) < 0.5 else ('X' if cov(inner) >= 0.5 else 'o')
        rows.append(row)
    return rows
if __name__ == '__main__':
    print('\n'.join(art()))
