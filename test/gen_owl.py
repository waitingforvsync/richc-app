"""
gen_owl.py - generate test/owl.png (512x512 RGBA, pure Python / stdlib only).
Run once: python test/gen_owl.py
"""
import struct, zlib, math

W = H = 512
img = [(20, 30, 60, 255)] * (W * H)   # night-sky background

# ---- drawing primitives ----

def idx(x, y):  return y * W + x

def fill_ellipse(cx, cy, rx, ry, col):
    for y in range(max(0, cy - ry - 1), min(H, cy + ry + 2)):
        for x in range(max(0, cx - rx - 1), min(W, cx + rx + 2)):
            if (x-cx)**2 * ry*ry + (y-cy)**2 * rx*rx <= rx*rx * ry*ry:
                img[idx(x, y)] = col

def fill_circle(cx, cy, r, col):
    fill_ellipse(cx, cy, r, r, col)

def fill_rect(x0, y0, x1, y1, col):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            img[idx(x, y)] = col

def fill_tri(p1, p2, p3, col):
    pts = sorted([p1, p2, p3], key=lambda p: p[1])
    def lx(a, b, y):
        return a[0] if b[1] == a[1] else a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
    for y in range(max(0, pts[0][1]), min(H, pts[2][1] + 1)):
        xa = lx(pts[0], pts[2], y)
        xb = lx(pts[0], pts[1], y) if y < pts[1][1] else lx(pts[1], pts[2], y)
        for x in range(max(0, int(min(xa, xb))), min(W, int(max(xa, xb)) + 1)):
            img[idx(x, y)] = col

def draw_feathers(cx, cy, rx, ry, base_col, stripe_col, n=6):
    """Horizontal scallop stripes across an ellipse to suggest feathers."""
    fill_ellipse(cx, cy, rx, ry, base_col)
    br, bg, bb, ba = base_col
    sr, sg, sb, sa = stripe_col
    step = (ry * 2) // n
    for i in range(n):
        stripe_y = (cy - ry) + i * step + step // 2
        half = max(1, int(rx * math.sqrt(max(0, 1 - ((stripe_y - cy) / ry)**2))))
        for y in range(stripe_y, min(H, stripe_y + max(2, step // 4))):
            t = (y - stripe_y) / max(1, step // 4)
            r2 = int(br + (sr - br) * (1 - t))
            g2 = int(bg + (sg - bg) * (1 - t))
            b2 = int(bb + (sb - bb) * (1 - t))
            for x in range(max(0, cx - half), min(W, cx + half + 1)):
                dx = x - cx
                dy = y - cy
                if dx*dx * ry*ry + dy*dy * rx*rx <= rx*rx * ry*ry:
                    img[idx(x, y)] = (r2, g2, b2, ba)

# ---- colours ----
C_SKY    = ( 20,  30,  60, 255)
C_MOON   = (255, 245, 200, 255)
C_BODY   = ( 90,  55,  20, 255)
C_DARK   = ( 55,  33,  10, 255)
C_BREAST = (200, 170, 110, 255)
C_STRIPE = (160, 130,  80, 255)
C_FACE   = (185, 150,  90, 255)
C_EYE    = (220, 185,  25, 255)
C_PUPIL  = (  8,   5,   3, 255)
C_WHITE  = (255, 255, 255, 255)
C_BEAK   = (210, 130,  30, 255)
C_PERCH  = (100,  65,  30, 255)
C_PERCH2 = ( 70,  43,  18, 255)
C_CLAW   = ( 75,  50,  20, 255)

# ---- moon ----
fill_circle(390, 80, 55, C_MOON)
fill_circle(420, 60, 48, C_SKY)   # crescent cutout

# ---- perch ----
fill_rect(60, 400, 452, 422, C_PERCH)
fill_rect(60, 410, 452, 422, C_PERCH2)

# ---- talons ----
for tx in [185, 215, 245, 275, 305]:
    fill_rect(tx - 4, 418, tx, 440, C_CLAW)

# ---- wings (behind body) ----
fill_ellipse(175, 295, 58, 135, C_DARK)
fill_ellipse(337, 295, 58, 135, C_DARK)
fill_ellipse(168, 298, 46, 125, C_BODY)
fill_ellipse(344, 298, 46, 125, C_BODY)

# ---- body ----
draw_feathers(256, 310, 105, 135, C_BODY, C_DARK, n=7)

# ---- breast ----
draw_feathers(256, 340, 68,  95, C_BREAST, C_STRIPE, n=8)

# ---- head ----
fill_circle(256, 175, 105, C_BODY)

# ---- ear tufts ----
fill_tri((188, 105), (205,  62), (228, 100), C_DARK)
fill_tri((188, 105), (205,  62), (228, 100), C_BODY)
fill_tri((192, 108), (208,  70), (225, 105), C_BODY)
fill_tri((324, 105), (307,  62), (284, 100), C_DARK)
fill_tri((324, 105), (307,  62), (284, 100), C_BODY)
fill_tri((320, 108), (304,  70), (287, 105), C_BODY)

# ---- facial disc ----
fill_ellipse(256, 188, 86, 78, C_FACE)

# ---- eye surrounds ----
fill_circle(216, 176, 44, C_DARK)
fill_circle(296, 176, 44, C_DARK)

# ---- eyes ----
fill_circle(216, 176, 38, C_EYE)
fill_circle(296, 176, 38, C_EYE)

# ---- pupils ----
fill_circle(216, 178, 22, C_PUPIL)
fill_circle(296, 178, 22, C_PUPIL)

# ---- eye highlights ----
fill_circle(224, 168,  9, C_WHITE)
fill_circle(304, 168,  9, C_WHITE)
fill_circle(228, 173,  4, C_WHITE)
fill_circle(308, 173,  4, C_WHITE)

# ---- beak ----
fill_tri((242, 214), (270, 214), (256, 238), C_BEAK)
fill_tri((242, 214), (270, 214), (256, 224), C_MOON)  # subtle ridge highlight

# ---- write PNG ----

def png_chunk(tag, data):
    payload = tag + data
    return struct.pack('>I', len(data)) + payload + struct.pack('>I', zlib.crc32(payload) & 0xffffffff)

raw = b''.join(
    b'\x00' + bytes(c for px in img[y*W:(y+1)*W] for c in px)
    for y in range(H)
)

out  = b'\x89PNG\r\n\x1a\n'
out += png_chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0))
out += png_chunk(b'IDAT', zlib.compress(raw, 6))
out += png_chunk(b'IEND', b'')

import os
path = os.path.join(os.path.dirname(__file__), 'owl.png')
with open(path, 'wb') as f:
    f.write(out)
print(f'Wrote {len(out)} bytes to {path}')
