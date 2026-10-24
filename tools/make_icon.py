import numpy as np, math, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'icone_gerado')
os.makedirs(OUT, exist_ok=True)
from PIL import Image, ImageDraw, ImageFilter

def radial(size, cx, cy, r, c_in, c_out):
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    d = np.clip(np.sqrt((x-cx)**2 + (y-cy)**2) / r, 0, 1)[..., None]
    a = np.array(c_in, np.float32); b = np.array(c_out, np.float32)
    return Image.fromarray((a + (b-a)*d).astype(np.uint8), 'RGBA')

def vgrad(w, h, top, bot):
    t = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    a = np.array(top, np.float32); b = np.array(bot, np.float32)
    arr = np.broadcast_to(a + (b-a)*t, (h, w, 4))
    return Image.fromarray(arr.astype(np.uint8), 'RGBA')

def circle_mask(size, cx, cy, r):
    m = Image.new('L', (size, size), 0); ImageDraw.Draw(m).ellipse((cx-r, cy-r, cx+r, cy+r), fill=255); return m

def draw(S, detail):
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    pad = S*0.03; R = S*0.22
    # fundo: quadrado arredondado escuro
    bg = vgrad(S, S, (54, 56, 64, 255), (20, 21, 25, 255))
    m = Image.new('L', (S, S), 0); ImageDraw.Draw(m).rounded_rectangle((pad, pad, S-pad, S-pad), R, fill=255)
    img.paste(bg, (0, 0), m)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((pad, pad, S-pad, S-pad), R, outline=(255, 255, 255, 28), width=max(1, int(S*0.008)))
    cx = cy = S/2 + S*0.01
    kr = S*0.255                       # raio do knob
    ar = S*0.36; aw = S*0.06           # arco da escala
    a0, a1, val = 135, 405, 0.70       # graus no sistema do PIL (0 = direita, sentido horário)
    aval = a0 + (a1-a0)*val
    box = (cx-ar, cy-ar, cx+ar, cy+ar)
    d.arc(box, a0, a1, fill=(70, 73, 82, 255), width=int(aw))
    d.arc(box, a0, aval, fill=(242, 162, 52, 255), width=int(aw))
    # pontas arredondadas do arco
    for ang, col in ((a0, (242, 162, 52, 255)), (aval, (242, 162, 52, 255)), (a1, (70, 73, 82, 255))):
        rr = ar - aw/2; px = cx + rr*math.cos(math.radians(ang)); py = cy + rr*math.sin(math.radians(ang))
        d.ellipse((px-aw/2, py-aw/2, px+aw/2, py+aw/2), fill=col)
    # marcas da escala
    if detail:
        for i in range(11):
            ang = math.radians(a0 + (a1-a0)*i/10)
            r0, r1 = ar - aw - S*0.018, ar - aw - S*0.045
            d.line((cx + r0*math.cos(ang), cy + r0*math.sin(ang), cx + r1*math.cos(ang), cy + r1*math.sin(ang)),
                   fill=(180, 184, 192, 200), width=max(1, int(S*0.012)))
    # sombra do knob
    sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse((cx-kr*1.08, cy-kr*1.08+S*0.03, cx+kr*1.08, cy+kr*1.08+S*0.03), fill=(0, 0, 0, 150))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(S*0.025)))
    d = ImageDraw.Draw(img)
    # saia serrilhada
    skirt = kr*1.1
    d.ellipse((cx-skirt, cy-skirt, cx+skirt, cy+skirt), fill=(36, 31, 26, 255))
    if detail:
        n = 40
        for i in range(n):
            ang = 2*math.pi*i/n
            d.line((cx + kr*1.0*math.cos(ang), cy + kr*1.0*math.sin(ang), cx + skirt*math.cos(ang), cy + skirt*math.sin(ang)),
                   fill=(80, 72, 62, 255), width=max(1, int(S*0.008)))
    # tampa creme
    cap = radial(S, cx - kr*0.3, cy - kr*0.35, kr*1.5, (255, 250, 236, 255), (176, 162, 132, 255))
    img.paste(cap, (0, 0), circle_mask(S, cx, cy, kr))
    d = ImageDraw.Draw(img)
    d.ellipse((cx-kr, cy-kr, cx+kr, cy+kr), outline=(120, 104, 80, 255), width=max(1, int(S*0.01)))
    # ponteiro
    ang = math.radians(aval)
    pw = S*(0.05 if detail else 0.075)
    p0 = (cx + kr*0.18*math.cos(ang), cy + kr*0.18*math.sin(ang)); p1 = (cx + kr*0.86*math.cos(ang), cy + kr*0.86*math.sin(ang))
    d.line((p0, p1), fill=(40, 30, 18, 255), width=int(pw))
    for p in (p0, p1): d.ellipse((p[0]-pw/2, p[1]-pw/2, p[0]+pw/2, p[1]+pw/2), fill=(40, 30, 18, 255))
    # LED de power
    if detail:
        lx, ly, lr = S*0.80, S*0.20, S*0.03
        glow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse((lx-lr*3, ly-lr*3, lx+lr*3, ly+lr*3), fill=(255, 70, 40, 110))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(S*0.02)))
        ImageDraw.Draw(img).ellipse((lx-lr, ly-lr, lx+lr, ly+lr), fill=(255, 96, 70, 255))
    return img

big = draw(1024, True)
big.save(OUT+'/icon_1024.png')
sizes = [256, 128, 64, 48, 40, 32, 24, 20, 16]
frames = []
for s in sizes:
    src = draw(1024, s >= 40)                     # versões pequenas sem detalhes finos
    frames.append(src.resize((s, s), Image.LANCZOS))
for f, s in zip(frames, sizes): f.save(f'{OUT}/icon_{s}.png')
frames[0].save(OUT+'/app.ico', format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[1:])
# folha de prévia em fundo claro e escuro
sheet = Image.new('RGBA', (900, 330), (0, 0, 0, 0))
for row, bgc in enumerate([(240, 241, 243, 255), (32, 33, 37, 255)]):
    band = Image.new('RGBA', (900, 165), bgc); sheet.paste(band, (0, row*165))
    x = 20
    for s, f in zip([256, 128, 64, 48, 32, 24, 16], [frames[0], frames[1], frames[2], frames[3], frames[5], frames[6], frames[8]]):
        im = f if s != 256 else f.resize((140, 140), Image.LANCZOS)
        w = im.size[0]; sheet.alpha_composite(im, (x, row*165 + (165-w)//2)); x += w + 34
sheet.save(OUT+'/previa_icone.png')
print("ok")
