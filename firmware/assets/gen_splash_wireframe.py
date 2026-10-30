#!/usr/bin/env python3
"""
gen_splash_wireframe.py - splash-screen truck turntable as a 3D wireframe (2026-10-29)

Renders the 22 splash frames as line art in the Incline screen's "D2" style
(bright outline, dimmer details) instead of photo-real turntable renders.
Writes PNGs (white/grey lines on pure black, 240x240) for the existing
convert_splash_frames.py to turn into the SD card's .bin frames - the
firmware side (ui_splash_truck.h) is unchanged.

How: a simple 3D model of the truck built from hand-traced, measured views
- the side profile (from incline_src/scout_side.png) extruded to the body /
hardtop widths from the top view (scout_top.png), with front, rear and top
details placed on their faces from scout_front.png / scout_rear.png /
scout_top.png. Each view is normalised separately (body width, ground line)
because the renders have slightly different perspective/scale. Rendering is
orthographic, camera 10 degrees up, with hidden-line removal: the model's
faces are rasterised into a depth buffer and each line is drawn only where
it isn't behind a face. Tyres are real cylinders, with their silhouette
lines recomputed per view.

Vehicle frame: X forward, Y left, Z up, origin mid-wheelbase on the ground,
units ~ side-render pixels (truck ~1700 long). The side render shows the
truck's RIGHT side (nose right).

Usage:
    python gen_splash_wireframe.py            # 22 PNGs -> splash_wireframe_png/, + contact sheet & GIF
    python gen_splash_wireframe.py --tests    # 4 test angles only
    python convert_splash_frames.py splash_wireframe_png -o splash_frames_wireframe_out --size 240 --bg-threshold 0
(--bg-threshold 0: the background is already pure black, and the default
threshold would eat the lines' antialiased edges.)
Needs Pillow + numpy.
"""
import math
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).parent
PNG_DIR = OUT / "splash_wireframe_png"

# ---- view -> vehicle conversions ---------------------------------------------
HALF_BODY = 357.0
HALF_TOP = 312.0          # hardtop half-width
X0 = 986.0                # side render x of the origin


def sx(xs):  return xs - X0          # side x -> X
def sz(ys):  return 970.0 - ys        # side y -> Z
def fy(xf):  return (xf - 950.0) / 362.0 * HALF_BODY     # front x -> Y (left on viewer's right)
def fz(yf):  return (965.0 - yf) * 1.03
def ry(xr):  return (995.0 - xr) / 395.0 * HALF_BODY     # rear x -> Y (left on viewer's left)
def rz(yr):  return (965.0 - yr) * 1.018


def arc2(cx, cy, r, a0, a1, n=24):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def rrect2(x0, y0, x1, y1, r, n=5):
    p = arc2(x1 - r, y0 + r, r, 270, 360, n) + arc2(x1 - r, y1 - r, r, 0, 90, n) + \
        arc2(x0 + r, y1 - r, r, 90, 180, n) + arc2(x0 + r, y0 + r, r, 180, 270, n)
    return p + [p[0]]


def circ2(cx, cy, r, n=32):
    return arc2(cx, cy, r, 0, 360, n)


class Model:
    def __init__(self):
        self.faces = []     # list of (N,3) arrays, planar polygons (occluders)
        self.lines = []     # list of ((N,3) array, kind) - kind 'o' outline / 'd' detail

    def face(self, pts): self.faces.append(np.array(pts, float))
    def line(self, pts, kind='o'): self.lines.append((np.array(pts, float), kind))


# Map 2D points of a given view onto a 3D plane ---------------------------------
def on_side(pts2, y):            # side render coords -> plane Y=y
    return [(sx(a), y, sz(b)) for a, b in pts2]


def on_front(pts2, x):           # front render coords -> plane X=x
    return [(x, fy(a), fz(b)) for a, b in pts2]


def on_rear(pts2, x):
    return [(x, ry(a), rz(b)) for a, b in pts2]


def build():
    m = Model()
    rw, fw = (530, 790), (1590, 782)
    # --- lower body side profile (side coords), arches cut out
    body = [(182, 700), (182, 445), (200, 410), (235, 400), (1300, 400), (1320, 392),
            (1700, 398), (1765, 425), (1790, 470), (1790, 640)]
    body += arc2(*fw, 245, 325, 200)
    body += [(1355, 740), (760, 740)]
    body += arc2(*rw, 245, 340, 200)
    body += [(245, 700), (182, 700)]
    for y in (HALF_BODY, -HALF_BODY):
        m.face(on_side(body, y))
        m.line(on_side(body, y))
    # hood: profile from cowl to the front edge, extruded across
    hood = [(1300, 400), (1320, 392), (1700, 398), (1765, 425), (1790, 470)]
    for (a, b), (c, d) in zip(hood, hood[1:]):
        m.face([(sx(a), HALF_BODY, sz(b)), (sx(c), HALF_BODY, sz(d)), (sx(c), -HALF_BODY, sz(d)), (sx(a), -HALF_BODY, sz(b))])
    m.line([(sx(1790), HALF_BODY, sz(470)), (sx(1790), -HALF_BODY, sz(470))])   # hood front edge
    m.line([(sx(1765), HALF_BODY, sz(425)), (sx(1765), -HALF_BODY, sz(425))], 'd')  # hood crease
    # front face of body
    xf = sx(1790)
    m.face([(xf, HALF_BODY, sz(470)), (xf, -HALF_BODY, sz(470)), (xf, -HALF_BODY, sz(640)), (xf, HALF_BODY, sz(640))])
    m.line([(xf, HALF_BODY, sz(640)), (xf, -HALF_BODY, sz(640))])
    # rear face of body
    xr = sx(182)
    m.face([(xr, HALF_BODY, sz(445)), (xr, -HALF_BODY, sz(445)), (xr, -HALF_BODY, sz(700)), (xr, HALF_BODY, sz(700))])
    m.line([(xr, HALF_BODY, sz(700)), (xr, -HALF_BODY, sz(700))])
    m.line([(xr, HALF_BODY, sz(445)), (xr, -HALF_BODY, sz(445))])
    # belt ledge: body top between body side and hardtop, rear to cowl
    zb = sz(400)
    for s in (1, -1):
        m.face([(sx(235), s * HALF_BODY, zb), (sx(1300), s * HALF_BODY, zb), (sx(1300), s * HALF_TOP, zb), (sx(235), s * HALF_TOP, zb)])
    m.face([(sx(200), HALF_BODY, sz(410)), (sx(235), HALF_BODY, zb), (sx(235), -HALF_BODY, zb), (sx(200), -HALF_BODY, sz(410))])
    m.line([(sx(200), HALF_BODY, sz(410)), (sx(200), -HALF_BODY, sz(410))], 'd')

    # --- hardtop
    top = [(235, 400), (216, 220), (238, 160), (1240, 163), (1263, 205), (1300, 400)]
    for y in (HALF_TOP, -HALF_TOP):
        m.face(on_side(top, y))
        m.line(on_side(top, y))
    for (a, b), (c, d) in zip(top, top[1:]):   # rear, roof, windshield surfaces
        m.face([(sx(a), HALF_TOP, sz(b)), (sx(c), HALF_TOP, sz(d)), (sx(c), -HALF_TOP, sz(d)), (sx(a), -HALF_TOP, sz(b))])
    for a, b in top[1:5]:
        m.line([(sx(a), HALF_TOP, sz(b)), (sx(a), -HALF_TOP, sz(b))])
    # side windows on the hardtop side planes
    for y in (HALF_TOP + 1, -HALF_TOP - 1):
        m.line(on_side(rrect2(340, 225, 800, 375, 25), y), 'd')
        m.line(on_side([(890, 232), (1178, 222), (1284, 398), (890, 398), (890, 232)], y), 'd')
    # door + handle + marker lights on the body sides
    for y in (HALF_BODY + 1, -HALF_BODY - 1):
        m.line(on_side([(862, 405), (862, 655), (895, 690), (1300, 690), (1306, 405)], y), 'd')
        m.line(on_side([(885, 455), (965, 455)], y), 'd')
        m.line(on_side(rrect2(250, 522, 305, 540, 3), y), 'd')
        m.line(on_side(rrect2(1715, 508, 1765, 525, 3), y), 'd')
    m.line(on_side(circ2(655, 505, 20), -HALF_BODY - 1), 'd')      # fuel cap (right side)

    # windshield glass, mapped from the front view onto the slanted windshield
    def ws_pt(xf_, yf_):
        z = fz(yf_)
        za, zb_ = sz(205), sz(400)
        t = (z - zb_) / (za - zb_)
        x = sx(1300) + (sx(1263) - sx(1300)) * t
        return (x + 1, fy(xf_), z)
    m.line([ws_pt(a, b) for a, b in rrect2(672, 210, 1228, 345, 8)], 'd')

    # --- front details (front plane, slightly proud of the face)
    xd = xf + 1
    m.line(on_front(rrect2(760, 462, 1162, 562, 10), xd), 'd')                 # grille
    for bez in (rrect2(640, 468, 746, 565, 12), rrect2(1178, 468, 1282, 565, 12)):
        m.line(on_front(bez, xd), 'd')
    for c in (circ2(693, 516, 38), circ2(1230, 516, 38)):
        m.line(on_front(c, xd), 'd')
    for c in (rrect2(665, 618, 728, 640, 3), rrect2(1195, 618, 1258, 640, 3)):
        m.line(on_front(c, xd), 'd')
    # front bumper box + push-bar hoop + driving lights (ahead of the face)
    xb0, xb1 = xf, xf + 50
    bz0, bz1 = fz(690), fz(645)
    by = fy(1300)
    m.face([(xb1, by, bz0), (xb1, -by, bz0), (xb1, -by, bz1), (xb1, by, bz1)])
    m.face([(xb0, by, bz1), (xb1, by, bz1), (xb1, -by, bz1), (xb0, -by, bz1)])
    m.line([(xb1, by, bz0), (xb1, -by, bz0), (xb1, -by, bz1), (xb1, by, bz1), (xb1, by, bz0)])
    m.line([(xb0, by, bz1), (xb1, by, bz1)]); m.line([(xb0, -by, bz1), (xb1, -by, bz1)])
    hoop = [(775, 690), (790, 600), (1130, 600), (1148, 690)]
    m.line(on_front(hoop, xb1 + 20), 'd')
    for c in (circ2(902, 496, 32), circ2(1030, 496, 32)):
        m.line(on_front(c, xb1 + 25), 'd')
    # mirrors
    for (x0_, x1_) in ((568, 600), (1292, 1324)):
        m.line(on_front(rrect2(x0_, 338, x1_, 388, 5), sx(1230)), 'd')

    # --- rear details
    xrd = xr - 1
    m.line(on_rear(rrect2(748, 405, 1243, 710, 6), xrd), 'd')                 # tailgate
    m.line(on_rear(rrect2(758, 445, 1233, 665, 6), xrd), 'd')
    for c in (circ2(673, 478, 38), circ2(1317, 478, 38), circ2(684, 665, 22), circ2(1307, 663, 22)):
        m.line(on_rear(c, xrd), 'd')
    # hardtop rear: liftgate seams + window, on the slanted rear surface
    def hr_pt(xr_, yr_):
        z = rz(yr_)
        za, zb_ = sz(220), sz(400)
        t = (z - zb_) / (za - zb_)
        x = sx(235) + (sx(216) - sx(235)) * t
        return (x - 1, ry(xr_), z)
    m.line([hr_pt(a, b) for a, b in rrect2(778, 207, 1212, 348, 20)], 'd')
    m.line([hr_pt(745, 190), hr_pt(745, 400)], 'd'); m.line([hr_pt(1245, 190), hr_pt(1245, 400)], 'd')
    # rear bumper (side render's height)
    rb0, rb1 = sx(120), sx(192)
    rz0, rz1 = sz(720), sz(645)
    rby = ry(660)
    m.face([(rb0, rby, rz0), (rb0, -rby, rz0), (rb0, -rby, rz1), (rb0, rby, rz1)])
    m.face([(rb0, rby, rz1), (rb1, rby, rz1), (rb1, -rby, rz1), (rb0, -rby, rz1)])
    m.line([(rb0, rby, rz0), (rb0, -rby, rz0), (rb0, -rby, rz1), (rb0, rby, rz1), (rb0, rby, rz0)])
    m.line([(rb0, rby, rz1), (rb1, rby, rz1)]); m.line([(rb0, -rby, rz1), (rb1, -rby, rz1)])

    # --- roof rack (top render), just above the roof
    def tx(xt): return (905 - xt) * 1.907 - 803
    def ty(yt): return (yt - 211) * 1.907
    zr = sz(160) + 18
    rack = [(470, 90), (790, 90), (790, 330), (470, 330), (470, 90)]
    m.line([(tx(a), ty(b), zr) for a, b in rack], 'd')

    # --- wheels: tyres (cylinders), rims, hubs; axles
    m.wheels = []
    for (wx, wz, r) in ((sx(rw[0]), sz(rw[1]), 180.0), (sx(fw[0]), sz(fw[1]), 178.0)):
        for s in (1, -1):
            yo, yi = s * 418.0, s * 248.0
            m.wheels.append((wx, wz, r, yo, yi))
            circ = lambda y, rr: [(wx + rr * math.cos(t), y, wz + rr * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 49)]
            m.face(circ(yo, r)); m.face(circ(yi, r))
            n = 36
            for i in range(n):
                t0, t1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
                m.face([(wx + r * math.cos(t0), yo, wz + r * math.sin(t0)), (wx + r * math.cos(t1), yo, wz + r * math.sin(t1)),
                        (wx + r * math.cos(t1), yi, wz + r * math.sin(t1)), (wx + r * math.cos(t0), yi, wz + r * math.sin(t0))])
            m.line(circ(yo, r)); m.line(circ(yi, r))
            m.line(circ(yo + s * 1, 100), 'd'); m.line(circ(yo + s * 1, 24), 'd')
        m.line([(wx, 248, wz), (wx, -248, wz)], 'd')
    return m


# ---- renderer ---------------------------------------------------------------------

def camera(az_deg, el_deg):
    az, el = math.radians(az_deg), math.radians(el_deg)
    c = np.array([math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)])
    f = -c
    right = np.cross(f, [0, 0, 1.0]); right /= np.linalg.norm(right)
    up = np.cross(right, f)
    return right, up, f


def render(m, az, el=10.0, size=240, ss=4, margin=10, widths=(1.6, 1.0),
           colors=((255, 255, 255), (150, 150, 150)), scale=None):
    right, up, f = camera(az, el)
    W = size * ss
    # Fit: fixed scale so the truck doesn't breathe between frames.
    if scale is None:
        scale = (size - 2 * margin) / 1850.0
    s = scale * ss
    cx, cy = W / 2, W / 2 + 10 * ss
    zc = 420.0  # vertical centre of the truck

    def proj(P):
        P = np.atleast_2d(P) - [0, 0, zc]
        return np.stack([cx + (P @ right) * s, cy - (P @ up) * s, P @ f], axis=1)

    zbuf = np.full((W, W), np.inf)
    for F in m.faces:
        q = proj(F)
        uv = q[:, :2]
        area = 0.5 * abs(np.dot(uv[:, 0], np.roll(uv[:, 1], 1)) - np.dot(uv[:, 1], np.roll(uv[:, 0], 1)))
        if area < 4:
            continue
        A = np.c_[uv, np.ones(len(uv))]
        coef, *_ = np.linalg.lstsq(A, q[:, 2], rcond=None)
        x0, y0 = np.floor(uv.min(0)).astype(int); x1, y1 = np.ceil(uv.max(0)).astype(int)
        x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, W - 1), min(y1, W - 1)
        if x1 <= x0 or y1 <= y0:
            continue
        mk = Image.new("L", (x1 - x0 + 1, y1 - y0 + 1), 0)
        ImageDraw.Draw(mk).polygon([(a - x0, b - y0) for a, b in uv], fill=255)
        mask = np.asarray(mk) > 0
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        d = coef[0] * xx + coef[1] * yy + coef[2]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        np.minimum(sub, np.where(mask, d, np.inf), out=sub)
    # tolerance: nearest neighbour depth in a 3x3 (so lines on a face's own edge survive)
    zmax = -np.asarray(Image.fromarray(-zbuf.astype(np.float32), "F").filter(ImageFilter.MinFilter(3)))
    zmax = np.where(np.isfinite(zbuf) | np.isfinite(zmax), zmax, np.inf)
    eps = 6.0

    # silhouette lines of the tyres for this view
    lines = list(m.lines)
    for (wx, wz, r, yo, yi) in m.wheels:
        # tyre axis is Y; silhouette where the radial normal is perpendicular to the view
        a = math.atan2(f[2], f[0])
        for t in (a + math.pi / 2, a - math.pi / 2):
            px, pz = wx + r * math.cos(t), wz + r * math.sin(t)
            lines.append((np.array([(px, yo, pz), (px, yi, pz)]), 'o'))

    layers = {'o': Image.new("L", (W, W), 0), 'd': Image.new("L", (W, W), 0)}
    draws = {k: ImageDraw.Draw(v) for k, v in layers.items()}
    wpx = {'o': max(1, round(widths[0] * ss)), 'd': max(1, round(widths[1] * ss))}
    for pts, kind in lines:
        # densify
        dense = []
        for a, b in zip(pts[:-1], pts[1:]):
            qa, qb = proj(a)[0], proj(b)[0]
            n = max(2, int(np.hypot(*(qb[:2] - qa[:2])) / 1.5) + 1)
            for i in range(n):
                dense.append(a + (b - a) * i / n)
        dense.append(pts[-1])
        q = proj(np.array(dense))
        xi = np.clip(q[:, 0].round().astype(int), 0, W - 1)
        yi = np.clip(q[:, 1].round().astype(int), 0, W - 1)
        vis = q[:, 2] <= zmax[yi, xi] + eps
        seg = []
        for i in range(len(q)):
            if vis[i]:
                seg.append((q[i, 0], q[i, 1]))
            else:
                if len(seg) > 1:
                    draws[kind].line(seg, fill=255, width=wpx[kind], joint="curve")
                seg = []
        if len(seg) > 1:
            draws[kind].line(seg, fill=255, width=wpx[kind], joint="curve")

    co = np.asarray(layers['o'].resize((size, size), Image.BOX)).astype(float) / 255
    cd = np.asarray(layers['d'].resize((size, size), Image.BOX)).astype(float) / 255
    cd = cd * (1 - co)
    img = co[..., None] * np.array(colors[0]) + cd[..., None] * np.array(colors[1])
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")


def turntable(m, n=22, outdir=None):
    """Frame k at azimuth -k*360/n: front first, then the truck's right side (nose right)."""
    frames = [render(m, -k * 360.0 / n) for k in range(n)]
    if outdir:
        outdir.mkdir(exist_ok=True)
        for k, fr in enumerate(frames):
            fr.save(outdir / f"frame_{k:02d}.png")
    return frames


def sheets(frames):
    cols = 6
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (240 * cols, 240 * rows), (0, 0, 0))
    for k, fr in enumerate(frames):
        sheet.paste(fr, ((k % cols) * 240, (k // cols) * 240))
    sheet.save(OUT / "splash_wireframe_contact.png")
    big = [fr.resize((480, 480), Image.NEAREST) for fr in frames]
    big[0].save(OUT / "splash_wireframe.gif", save_all=True, append_images=big[1:], duration=100, loop=0)


def main_tests():
    m = build()
    tests = [("front 3/4", -35), ("side", -90), ("rear 3/4", -145), ("front", 0)]
    tiles = []
    for name, az in tests:
        tiles.append(render(m, az))
    amber = render(m, -35, colors=((0xB3, 0x59, 0x00), (0x66, 0x33, 0x00)))
    z = 2
    sheet = Image.new("RGB", (240 * z * 5 + 40, 240 * z + 40), (0, 0, 0))
    d = ImageDraw.Draw(sheet)
    for i, (t, (name, _)) in enumerate(zip(tiles + [amber], tests + [("front 3/4 night", 0)])):
        sheet.paste(t.resize((240 * z, 240 * z), Image.NEAREST), (i * (240 * z + 8), 30))
        d.text((i * (240 * z + 8) + 6, 8), name, fill=(200, 200, 200))
    sheet.save(OUT / "splash_wireframe_tests.png")
    print("ok")


if __name__ == "__main__":
    import sys
    if "--tests" in sys.argv:
        main_tests()
    else:
        sheets(turntable(build(), outdir=PNG_DIR))
        print("frames ok")
