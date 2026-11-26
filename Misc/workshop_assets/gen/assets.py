import math, sys
from kit import *

IRON = [3, 4, 5, 6, 7, 8]; DKIRON = [0, 1, 2, 3]
BRASS = [26, 28, 30, 31, 108]; GOLD = [29, 31, 108, 109, 110]; STARG = [106, 107, 108, 109, 110]
WOOD = [19, 21, 23, 24, 25, 26]; CANDY = [77, 78, 79]; BERRY = [73, 75, 77, 79, 79]; DKRED = [68, 70, 72, 74]
WHITE = [12, 13, 14, 15]; GREEN = [188, 186, 184, 183, 182, 180]; DKGREEN = [190, 189, 188, 187]
DOUGH = [232, 233, 234, 235, 236]; GLASS = [244, 245, 246]
GINGER = [89, 91, 92, 93, 94, 95]; BAKED = [85, 86, 87, 88]; LEATHER = [21, 22, 23, 24, 25]
PAPER = [163, 161, 160]; YEL = [241, 242, 243, 252, 253]; CLAW = [4, 6, 8, 10]
ident = lambda p, g: p

def kick(back, deg, pivot):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    def fn(p, g):
        dx, dz = p[0] - pivot[0], p[2] - pivot[2]
        return (pivot[0] + c * dx - s * dz - back, p[1], pivot[2] + s * dx + c * dz)
    return fn

def triprism(m, pts_xz, hy, rect, grp=None):
    a = [(x, -hy, z) for x, z in pts_xz]; b = [(x, hy, z) for x, z in pts_xz]
    cen = [sum(p[i] for p in a + b) / 6 for i in range(3)]
    m.loft([a, b], rect, grp=grp)
    m.fan(a, rect, (0, 2), cen, grp); m.fan(b, rect, (0, 2), cen, grp)

# ------------------------------------------------------------------ v_platgun
def platgun():
    m = M('v_platgun', 256, 128, bg=4)
    Z0 = -11.5
    bp = [(3.6, -15.4), (3.6, -9.0), (2.2, -7.6), (-2.2, -7.6), (-3.6, -9.0), (-3.6, -15.4)]
    bw = [28, 8, 16, 8, 28, 10]
    R_body = (0, 0, 106, 100)
    def body_extra(a, f, j, ix, iy):
        if a > 0.87: return None
        aa = a / 0.87
        if 0.43 <= aa < 0.54: return m.pick(BERRY, 0.75 if j == 2 else (0.2 if j == 5 else 0.5), 0.25)
        if abs(aa - 0.43) < 0.012 or abs(aa - 0.55) < 0.012: return 2
        low = (j == 0 and f < 0.22) or (j == 4 and f > 0.78)
        if low: return m.pick(IRON, 0.15, 0.2)
        if (j == 0 and 0.22 <= f < 0.27) or (j == 4 and 0.73 < f <= 0.78): return 2
        if j == 2 and (f < 0.12 or f > 0.88): return 9      # hard highlight along the top edges
        return None
    m.paint_loft(R_body, side_nz(bp), IRON, wts=bw, extra=body_extra, noise=0.22)
    # rivets (brass, with a dark pixel under each)
    for (sy0, up) in ((1, True), (1 + 28 + 8 + 16 + 8, False)):
        for ax in (5, 34, 55, 84):
            for fy in (0.32, 0.86):
                yy = sy0 + int((1 - fy if up else fy) * 27)
                m.dot(R_body, ax, yy, 108); m.dot(R_body, ax + 1, yy, 31); m.dot(R_body, ax, yy + 1, 29); m.dot(R_body, ax + 1, yy + 1, 2)
    m.loft([prof(-16.5, bp), prof(3.5, bp), prof(5.4, bp, 0.8, (0, Z0))], R_body, us=[(0, 0.87), (0.87, 1)], wts=bw)
    R_bcap = (28, 100, 30, 28); R_fcap = (58, 100, 22, 22)
    m.fill(R_bcap, IRON, 0.25); m.fill(R_fcap, IRON, 0.35)
    m.fan(prof(-16.5, bp), R_bcap, (1, 2), (0, 0, Z0)); m.fan(prof(5.4, bp, 0.8, (0, Z0)), R_fcap, (1, 2), (0, 0, Z0))
    # wood stock
    R_stock = (212, 0, 26, 42)
    sp0 = [(2.6, -14.8), (2.6, -8.6), (-2.6, -8.6), (-2.6, -14.8)]; sp1 = [(1.9, -15.6), (1.9, -10.4), (-1.9, -10.4), (-1.9, -15.6)]
    def grain(ramp):
        return lambda a, f, j, ix, iy: (ramp[1] if (iy * 7 + ix // 5) % 5 == 0 else None)
    m.paint_loft(R_stock, side_nz(sp0), WOOD, extra=grain(WOOD), noise=0.25)
    m.loft([prof(-16.5, sp0), prof(-22.0, sp1)], R_stock)
    R_scap = (80, 100, 20, 22); m.fill(R_scap, WOOD, 0.3)
    m.fan(prof(-22.0, sp1), R_scap, (1, 2), (-18, 0, -12))
    # barrel, flare, lip, inner cone, dough plug
    R_bar = (106, 0, 88, 50); R_in = (194, 0, 18, 50); R_plug = (0, 100, 28, 28)
    r8 = ngon(0, 0, Z0, 1, 1, 8); nz8 = side_nz(yz(r8))
    def bar_extra(a, f, j, ix, iy):
        if a > 0.955: return m.pick(DKIRON, 0.8, 0.2)
        if abs(a - 0.66) < 0.012: return 2
        return None
    m.paint_loft(R_bar, nz8, IRON, cols=[(0.17, 0.25, BRASS), (0.42, 0.50, BRASS), (0.0, 0.03, DKIRON)], extra=bar_extra, noise=0.22)
    m.paint_loft(R_in, nz8, DKIRON, cols=[(0.8, 1.0, [224, 226, 228, 230], True)], noise=0.3)
    rb = lambda x, r: ngon(x, 0, Z0, r, r, 8)
    m.loft([rb(5.4, 2.9), rb(18.0, 2.5), rb(24.0, 4.7)], R_bar, us=[(0, 0.66), (0.66, 1)])
    m.loft([rb(24.0, 4.7), rb(24.0, 3.8), rb(20.0, 2.3)], R_in, us=[(0, 0.25), (0.25, 1)], center=(40, 0, Z0), flip=True)
    def plug(u, v, ix, iy):
        d = math.hypot(u - 0.5, v - 0.5)
        return (m.pick(DOUGH, 0.9 - d * 1.4 + (0.25 if (ix * 3 + iy * 5) % 7 == 0 else 0), 0.3), True)
    m.paint(R_plug, plug)
    m.fan(rb(20.0, 2.3), R_plug, (1, 2), (0, 0, Z0))
    # canister
    R_can = (134, 50, 66, 38); R_ccap = (234, 73, 14, 14)
    CZ = -5.8
    rc = lambda x: ngon(x, 0, CZ, 2.0, 2.0, 6); nz6 = side_nz(yz(rc(0)))
    def can_extra(a, f, j, ix, iy):
        if a < 0.1 or a >= 0.9:
            if abs(a - 0.1) < 0.012 or abs(a - 0.9) < 0.012: return 27
            return None
        if nz6[j] > 0.9: return (246 if (ix + iy * 3) % 11 < 2 else m.pick(GLASS, 0.5, 0.06), True)
        if nz6[j] > 0.2:
            top = (f > 0.6) if j == 1 else (f < 0.4)
            if top: return (m.pick(GLASS, 0.1, 0.08), True)
            return (m.pick(DOUGH, 0.85 if (ix // 5 + iy) % 3 == 0 else 0.6, 0.3), True)
        return (m.pick(DOUGH, 0.3 if nz6[j] > -0.9 else 0.05, 0.3), True)
    m.paint_loft(R_can, nz6, BRASS, extra=can_extra, noise=0.2)
    m.loft([rc(-13.0), rc(1.0)], R_can)
    m.fill(R_ccap, BRASS, 0.5); m.dot(R_ccap, 5, 5, 108, 4, 4); m.dot(R_ccap, 6, 6, 27, 2, 2)
    m.fan(rc(-13.0), R_ccap, (1, 2), (-6, 0, CZ)); m.fan(rc(1.0), R_ccap, (1, 2), (-6, 0, CZ))
    # brass feed pipe
    R_pipe = (234, 50, 22, 11)
    m.paint_loft(R_pipe, [0.9, -0.2, -0.6], BRASS, noise=0.2)
    rp = lambda x, z: ngon(x, 0, z, 0.75, 0.75, 3, math.radians(90))
    m.loft([rp(0.8, -5.6), rp(5.2, -5.6), rp(8.6, -9.2)], R_pipe, us=[(0, 0.55), (0.55, 1)])
    # grip + candy cane fore handle
    R_grip = (200, 50, 34, 42); R_gbot = (100, 100, 14, 10)
    m.paint_loft(R_grip, [0.2, 0.6, 0.2, -0.6], WOOD, extra=grain(WOOD), cols=[(0.9, 1.0, WOOD[:3])], noise=0.25)
    g0 = m.box_ring(-15.2, -10.8, -6.8, 1.5); g1 = m.box_ring(-22.2, -12.4, -8.6, 1.4)
    m.loft([g0, g1], R_grip); m.fill(R_gbot, WOOD, 0.15); m.fan(g1, R_gbot, (0, 1), (-9, 0, -18))
    R_cane = (106, 50, 28, 34); R_cbot = (114, 100, 10, 10)
    def cane(u, v, ix, iy):
        if ix >= 24: return m.pick(WHITE, 0.1, 0.2)
        return m.pick(CANDY, 0.8, 0.3) if ((ix + iy) // 4) % 2 == 0 else m.pick(WHITE, 0.85, 0.3)
    m.paint(R_cane, cane)
    c0 = m.box_ring(-15.2, -0.6, 1.9, 1.15); c1 = m.box_ring(-21.2, -1.3, 1.2, 1.15)
    m.loft([c0, c1], R_cane); m.fill(R_cbot, WHITE, 0.2); m.fan(c1, R_cbot, (0, 1), (0.5, 0, -18))
    # trigger
    R_trig = (234, 61, 12, 12); m.fill(R_trig, DKIRON, 0.7)
    triprism(m, [(-6.2, -15.2), (-4.6, -15.2), (-5.8, -17.6)], 0.4, R_trig)
    piv = (-8.0, 0.0, -15.0)
    m.save([('idle', ident), ('fire1', kick(3.0, 6.0, piv)), ('fire2', kick(1.5, 3.0, piv))])

# ------------------------------------------------------------------ v_lavagun
# A hot-cocoa-red sprayer: candy-striped barrel, a glass tank of glowing lava on the left, wood grip.
LAVA = [224, 225, 226, 227, 228, 229, 230, 231]

def lavagun():
    m = M('v_lavagun', 256, 128, bg=68)
    Z0 = -10.6
    bp = [(3.0, -14.0), (3.0, -8.4), (1.8, -7.2), (-1.8, -7.2), (-3.0, -8.4), (-3.0, -14.0)]
    bw = [28, 8, 16, 8, 28, 10]
    R_body = (0, 0, 90, 60)
    def body_extra(a, f, j, ix, iy):
        if a > 0.87: return None
        aa = a / 0.87
        if 0.36 <= aa < 0.46 or 0.62 <= aa < 0.70: return m.pick(BRASS, 0.75 if j == 2 else (0.2 if j == 5 else 0.5), 0.25)
        if abs(aa - 0.36) < 0.012 or abs(aa - 0.46) < 0.012 or abs(aa - 0.62) < 0.012 or abs(aa - 0.70) < 0.012: return 2
        if j == 2 and (f < 0.12 or f > 0.88): return 79       # bright edge along the top
        return None
    m.paint_loft(R_body, side_nz(bp), BERRY, wts=bw, extra=body_extra, noise=0.22)
    m.loft([prof(-14.0, bp), prof(4.0, bp), prof(5.6, bp, 0.8, (0, Z0))], R_body, us=[(0, 0.87), (0.87, 1)], wts=bw)
    R_bcap = (90, 0, 28, 28); R_fcap = (118, 0, 22, 22)
    m.fill(R_bcap, DKRED, 0.5, 0.25); m.fill(R_fcap, DKRED, 0.4, 0.3)
    m.fan(prof(-14.0, bp), R_bcap, (1, 2), (0, 0, Z0)); m.fan(prof(5.6, bp, 0.8, (0, Z0)), R_fcap, (1, 2), (0, 0, Z0))
    # candy-striped barrel flaring to a nozzle that glows from inside
    R_bar = (0, 60, 88, 44); R_in = (88, 60, 20, 44); R_plug = (224, 14, 14, 14)
    rb = lambda x, r: ngon(x, 0, Z0, r, r, 8); nz8 = side_nz(yz(rb(0, 1)))
    def stripes(a, f, j, ix, iy):
        # candy stripes, then a glass conduit full of lava between brass rings, then a white-hot nozzle
        if a > 0.73: return (m.pick(LAVA, 0.55 + 0.4 * (nz8[j] * 0.5 + 0.5) + 0.1 * ((ix + iy) % 3 == 0), 0.2), True)
        if abs(a - 0.73) < 0.02 or abs(a - 0.43) < 0.02 or abs(a - 0.67) < 0.02: return m.pick(BRASS, 0.6 + 0.3 * (j == 2), 0.2)
        if 0.45 <= a < 0.67: return (m.pick(LAVA, 0.15 + 0.6 * (nz8[j] * 0.5 + 0.5) + 0.15 * ((ix * 3 + iy) % 4 == 0), 0.3), True)
        return m.pick(WHITE, 0.8, 0.3) if ((ix + iy) // 5) % 2 else m.pick(BERRY, 0.7, 0.3)
    m.paint_loft(R_bar, nz8, BERRY, extra=stripes, noise=0.2)
    m.paint_loft(R_in, nz8, LAVA, cols=[(0.0, 1.0, LAVA[2:], True)], noise=0.3, fb=True)
    m.loft([rb(5.6, 2.4), rb(21.0, 2.2), rb(25.0, 3.4)], R_bar, us=[(0, 0.7), (0.7, 1)])
    m.loft([rb(25.0, 3.4), rb(25.0, 2.7), rb(21.5, 1.5)], R_in, us=[(0, 0.25), (0.25, 1)], center=(40, 0, Z0), flip=True)
    m.paint(R_plug, lambda u, v, ix, iy: (m.pick(LAVA, 0.95 - 0.8 * math.hypot(u - 0.5, v - 0.5), 0.25), True))
    m.fan(rb(21.5, 1.5), R_plug, (1, 2), (0, 0, Z0))
    # glass tank on the left, full of lava, with brass ribs
    R_tank = (140, 0, 70, 44); R_tcap = (210, 0, 14, 14)
    TY, TZ = 6.2, Z0 + 1.2
    rt = lambda x: ngon(x, TY, TZ, 3.2, 3.2, 8); nzt = side_nz(yz(rt(0)))
    def tank_extra(a, f, j, ix, iy):
        if a < 0.07 or a >= 0.93 or abs(a - 0.5) < 0.04:
            return m.pick(BRASS, 0.8 if j in (1, 2) else 0.35, 0.2)
        if abs(a - 0.07) < 0.012 or abs(a - 0.93) < 0.012 or abs(a - 0.46) < 0.012 or abs(a - 0.54) < 0.012: return 2
        if nzt[j] > 0.2 and f < 0.3: return (m.pick(LAVA, 0.95, 0.1), True)   # glass shine on the lit side
        return (m.pick(LAVA, 0.25 + 0.5 * ((ix * 3 + iy * 5) % 7) / 7.0 + 0.2 * (nzt[j] * 0.5 + 0.5), 0.3), True)
    m.paint_loft(R_tank, nzt, BRASS, extra=tank_extra, noise=0.2)
    m.loft([rt(-13.0), rt(3.0)], R_tank)
    m.fill(R_tcap, BRASS, 0.5)
    m.fan(rt(-13.0), R_tcap, (1, 2), (-5, TY, TZ)); m.fan(rt(3.0), R_tcap, (1, 2), (-5, TY, TZ))
    # brass feed hose from the tank to the barrel
    R_hose = (230, 28, 24, 12)
    m.paint_loft(R_hose, [0.9, -0.1, -0.7, 0.3], BRASS, noise=0.2)
    hr = lambda x, y, z, r=0.9: [(x, y + r * math.cos(math.radians(a)), z + r * math.sin(math.radians(a))) for a in (45, 135, 225, 315)]
    m.loft([hr(3.0, TY, TZ), hr(6.0, 4.6, Z0 + 0.6), hr(8.4, 2.0, Z0)], R_hose, us=[(0, 0.5), (0.5, 1)])
    # wood grip and candy cane fore handle
    R_grip = (108, 60, 34, 42); R_gbot = (142, 60, 14, 10)
    grain = lambda ramp: (lambda a, f, j, ix, iy: (ramp[1] if (iy * 7 + ix // 5) % 5 == 0 else None))
    m.paint_loft(R_grip, [0.2, 0.6, 0.2, -0.6], WOOD, extra=grain(WOOD), cols=[(0.9, 1.0, WOOD[:3])], noise=0.25)
    g0 = m.box_ring(-14.0, -10.0, -6.0, 1.5); g1 = m.box_ring(-21.0, -11.4, -7.6, 1.4)
    m.loft([g0, g1], R_grip); m.fill(R_gbot, WOOD, 0.15); m.fan(g1, R_gbot, (0, 1), (-9, 0, -18))
    R_cane = (156, 60, 28, 34); R_cbot = (184, 60, 10, 10)
    def cane(u, v, ix, iy):
        if ix >= 24: return m.pick(WHITE, 0.1, 0.2)
        return m.pick(CANDY, 0.8, 0.3) if ((ix + iy) // 4) % 2 == 0 else m.pick(WHITE, 0.85, 0.3)
    m.paint(R_cane, cane)
    c0 = m.box_ring(-14.0, 0.2, 2.7, 1.15); c1 = m.box_ring(-20.0, -0.6, 1.9, 1.15)
    m.loft([c0, c1], R_cane); m.fill(R_cbot, WHITE, 0.2); m.fan(c1, R_cbot, (0, 1), (0.8, 0, -17))
    R_trig = (194, 60, 12, 12); m.fill(R_trig, DKIRON, 0.7)
    triprism(m, [(-5.4, -13.8), (-3.8, -13.8), (-5.0, -16.2)], 0.4, R_trig)
    piv = (-8.0, 0.0, -14.0)
    m.save([('idle', ident), ('fire1', kick(2.0, 4.0, piv)), ('fire2', kick(1.0, 2.0, piv))])

# ------------------------------------------------------------------ v_grapple
def grapple():
    m = M('v_grapple', 256, 128, bg=188)
    Z0 = -9.4
    bp = [(2.0, -12.4), (2.0, -7.4), (1.2, -6.4), (-1.2, -6.4), (-2.0, -7.4), (-2.0, -12.4)]
    bw = [24, 6, 12, 6, 24, 12]
    R_body = (0, 0, 80, 86)
    def body_extra(a, f, j, ix, iy):
        if a > 0.89: return m.pick(GREEN, 0.35 + 0.3 * (j == 2), 0.2)
        aa = a / 0.89
        for c in (0.24, 0.80):
            if abs(aa - c) < 0.045: return m.pick(GOLD, 0.9 if j == 2 else (0.15 if j == 5 else 0.55), 0.25)
            if abs(abs(aa - c) - 0.052) < 0.008: return 190
        low = (j == 0 and f < 0.2) or (j == 4 and f > 0.8)
        if low: return m.pick(GREEN, 0.1, 0.2)
        if j == 2 and (f < 0.12 or f > 0.88): return 178
        return None
    m.paint_loft(R_body, side_nz(bp), GREEN, wts=bw, extra=body_extra, noise=0.22)
    m.loft([prof(0.0, bp), prof(13.4, bp), prof(15.1, bp, 0.72, (0, Z0))], R_body, us=[(0, 0.89), (0.89, 1)], wts=bw)
    R_bcap = (80, 42, 22, 34); R_fcap = (102, 42, 16, 26)
    m.fill(R_bcap, GREEN, 0.2); m.fill(R_fcap, GREEN, 0.3)
    m.fan(prof(0.0, bp), R_bcap, (1, 2), (5, 0, Z0)); m.fan(prof(15.1, bp, 0.72, (0, Z0)), R_fcap, (1, 2), (5, 0, Z0))
    # berry barrel + gold muzzle
    R_bar = (80, 0, 44, 38); R_in = (124, 0, 10, 38); R_cap = (118, 42, 12, 12)
    rb = lambda x, r: ngon(x, 0, Z0, r, r, 6); nz6 = side_nz(yz(rb(0, 1)))
    def bar_extra(a, f, j, ix, iy):
        if abs(a - 0.75) < 0.015: return 68
        if a < 0.75 and nz6[j] > 0.9 and 0.3 < f < 0.6: return 104
        return None
    m.paint_loft(R_bar, nz6, BERRY, cols=[(0.75, 1.0, GOLD)], extra=bar_extra, noise=0.22)
    m.paint_loft(R_in, nz6, [27, 29, 31], noise=0.3); m.fill(R_cap, DKIRON, 0.3)
    m.loft([rb(15.1, 1.65), rb(21.4, 1.45)], R_bar, us=[(0, 0.75)])
    m.loft([rb(21.4, 1.75), rb(23.0, 2.15)], R_bar, us=[(0.75, 1)])
    m.loft([rb(23.0, 2.15), rb(22.2, 1.3)], R_in, center=(40, 0, Z0), flip=True)
    m.fan(rb(22.2, 1.3), R_cap, (1, 2), (0, 0, Z0))
    # spool drum on the right (-Y) side
    R_dcap = (134, 0, 36, 36); R_dside = (170, 0, 10, 42)
    DX, DR = 6.6, 3.05
    rd = lambda y, r: [(DX + r * math.cos(math.pi / 8 + k * math.pi / 4), y, Z0 + r * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8)]
    nzd = [math.sin(math.pi / 4 + k * math.pi / 4) for k in range(8)]
    BULBS = [251, 242, 244, 252, 250, 243, 245, 236]
    def dside(a, f, j, ix, iy):
        if 0.3 < a < 0.75 and 0.25 < f < 0.8: return (BULBS[j], True)
        return None
    m.paint_loft(R_dside, nzd, DKGREEN, extra=dside, noise=0.3)
    def dcap(u, v, ix, iy):
        x, y = (u - 0.5) * 2, (v - 0.5) * 2
        d = max(abs(x), abs(y), (abs(x) + abs(y)) / 1.4142 * 1.0824)
        if d > 0.74: return m.pick(GOLD, 0.75 - 0.5 * y, 0.25)
        if d > 0.66: return 190
        return m.pick(DKGREEN, 0.4, 0.4)
    m.paint(R_dcap, dcap)
    for (bx, by, c) in ((12, 11, 251), (18, 9, 242), (23, 13, 244), (13, 18, 252), (21, 21, 250), (16, 24, 245), (9, 22, 243), (25, 19, 236)):
        m.dot(R_dcap, bx, by, c, 3, 3, True); m.dot(R_dcap, bx, by, 254, 1, 1, True)
    m.loft([rd(-1.9, DR), rd(-3.3, DR - 0.35)], R_dside)
    m.fan(rd(-3.3, DR - 0.35), R_dcap, (0, 2), (DX, 0, Z0))
    # grip + pommel
    R_grip = (180, 0, 34, 42); R_pom = (214, 0, 8, 42); R_pbot = (130, 42, 16, 12)
    m.paint_loft(R_grip, [0.2, 0.7, 0.2, -0.6], DKRED, extra=lambda a, f, j, ix, iy: (68 if ix % 6 == 3 and a < 0.9 else None), noise=0.25)
    m.paint_loft(R_pom, [0.5, 0.8, 0.5, 0.1], GOLD, noise=0.25); m.fill(R_pbot, GOLD, 0.15)
    g0 = m.box_ring(-12.2, 1.9, 5.3, 1.2); g1 = m.box_ring(-18.4, 0.8, 3.9, 1.2)
    p0 = m.box_ring(-18.4, 0.5, 4.2, 1.45); p1 = m.box_ring(-19.4, 0.3, 4.0, 1.45)
    m.loft([g0, g1], R_grip); m.loft([p0, p1], R_pom); m.fan(p1, R_pbot, (0, 1), (2, 0, -15))
    R_trig = (146, 42, 12, 12); m.fill(R_trig, GOLD, 0.5)
    triprism(m, [(6.2, -12.2), (7.6, -12.2), (6.6, -14.3)], 0.35, R_trig)
    # star hook on the muzzle
    R_star = (222, 0, 34, 34); R_prong = (158, 42, 8, 12)
    SC = (23.6, 0.0, Z0); paint_star(m, R_star, 90.0)
    star(m, SC, 2.7, 1.15, 0.55, R_star, 'star')
    m.fill(R_prong, CLAW, 0.6)
    for phi in (90, 210, 330):
        e = (math.cos(math.radians(phi)), math.sin(math.radians(phi)))
        q = lambda x, r: (SC[0] + x, SC[1] + r * e[0], SC[2] + r * e[1])
        t = [q(0.2, 0.25), q(0.2, 1.1), q(3.0, 1.3)]
        m.tri(t, R_prong, [(0, 1), (0, 0), (1, 0.5)], grp='star'); m.tri(t, R_prong, [(0, 1), (0, 0), (1, 0.5)], flip=True, grp='starb')
    def fire(p, g):
        if g in ('star', 'starb'): p = (21.6, 0.0, Z0)
        return (p[0] - 3.0 - 2.0, p[1], p[2])
    m.save([('idle', lambda p, g: (p[0] - 3.0, p[1], p[2])), ('fire', fire)])

def paint_star(m, r, rot=180.0):
    def fn(u, v, ix, iy):
        x, y = u - 0.5, 0.5 - v
        ang = (math.degrees(math.atan2(y, x)) - rot) % 360.0
        k = int(((ang + 36) % 360) // 72); half = ((ang + 36) % 72) < 36
        lvl = (0.85 if half else 0.3) + (0.15 if k in (0, 4) else -0.1 if k == 2 else 0)
        if math.hypot(x, y) < 0.07: return 111
        return m.pick(STARG, lvl, 0.08)
    m.paint(r, fn)

def star(m, c, R, r, h, rect, grp=None):
    """five-point star in the YZ plane (faces along X), one point up"""
    o = []
    for k in range(10):
        a = math.radians(90 + 36 * k); rr = R if k % 2 == 0 else r
        o.append((c[0], c[1] + rr * math.cos(a), c[2] + rr * math.sin(a)))
    bb = (c[1] - R, c[2] - R, c[1] + R, c[2] + R)
    for xc in (h, -h):
        cen = (c[0] + xc, c[1], c[2])
        for k in range(10):
            m.fan([cen, o[k], o[(k + 1) % 10]], rect, (1, 2), c, grp, bbox=bb)

# ------------------------------------------------------------------ starhook
def starhook():
    m = M('starhook', 64, 64, bg=108)
    R_star = (0, 0, 48, 48); R_claw = (0, 48, 44, 16)
    paint_star(m, R_star, 90.0)
    star(m, (0, 0, 0), 7.0, 2.9, 1.5, R_star)
    def claw(u, v, ix, iy):
        if 0.4 < v < 0.6: return m.pick(CLAW, 0.95, 0.15)
        return m.pick(CLAW, 0.2 if u > 0.75 else 0.5, 0.2)
    m.paint(R_claw, claw)
    P = [(0.6, 0.3), (0.6, 1.7), (5.6, 3.7), (5.2, 2.5), (8.0, 2.4)]     # (x, radial): tine splays out, tip hooks in
    U = [(0, 1), (0, 0), (0.75, 0), (0.75, 1), (1, 0.5)]
    for phi in (90, 210, 330):
        e = (math.cos(math.radians(phi)), math.sin(math.radians(phi)))
        q = [(x, r * e[0], r * e[1]) for x, r in P]
        for (i, j, k) in ((0, 1, 2), (0, 2, 3), (3, 2, 4)):
            for fl in (False, True):
                m.tri([q[i], q[j], q[k]], R_claw, [U[i], U[j], U[k]], flip=fl, grp='b' if fl else None)
    m.save([('idle', ident)])

# ------------------------------------------------------------------ gingerblob
def gingerblob():
    m = M('gingerblob', 64, 64, bg=92)
    rng = m.rng
    jit = lambda p, a=0.7: tuple(c + rng.uniform(-a, a) for c in p)
    def ring(z, r, ph):
        return [jit((r * math.cos(ph + k * math.pi / 3), r * math.sin(ph + k * math.pi / 3), z)) for k in range(6)]
    top = jit((0, 0, 5.8), 0.5); bot = jit((0, 0, -5.6), 0.5)
    r1, r2, r3 = ring(3.4, 4.5, 0.2), ring(0.0, 6.0, 0.5), ring(-3.5, 4.6, 0.3)
    R_b1 = (0, 0, 62, 16); R_b2 = (0, 16, 62, 16); R_top = (0, 32, 32, 32); R_bot = (32, 32, 32, 32)
    for R, lv in ((R_b1, [0.75, 0.5, 0.35, 0.6, 0.85, 0.65]), (R_b2, [0.5, 0.3, 0.1, 0.25, 0.55, 0.4])):
        m.paint_loft(R, [x * 2 - 1 for x in lv], GINGER, swap=True, noise=0.3)
    def cap(base):
        lv = [base + d for d in (0.1, -0.1, 0.05, -0.15, 0.12, -0.05)]
        return lambda u, v, ix, iy: m.pick(GINGER, lv[int((math.degrees(math.atan2(0.5 - v, u - 0.5)) % 360) // 60)], 0.3)
    m.paint(R_top, cap(0.85)); m.paint(R_bot, cap(0.15))
    for R, pts in ((R_b1, ((9, 6), (40, 9))), (R_b2, ((22, 5), (51, 8))), (R_top, ((12, 14),))):
        for x, y in pts: m.dot(R, x, y, 86, 2, 2); m.dot(R, x, y, 84)
    cen = (0, 0, 0)
    m.loft([r1, r2], R_b1, swap=True, center=cen); m.loft([r2, r3], R_b2, swap=True, center=cen)
    bb = (-6.8, -6.8, 6.8, 6.8)
    for k in range(6):
        m.fan([top, r1[k], r1[(k + 1) % 6]], R_top, (0, 1), cen, bbox=bb)
        m.fan([bot, r3[k], r3[(k + 1) % 6]], R_bot, (0, 1), cen, bbox=bb)
    m.save([('idle', ident)])

# ------------------------------------------------------------------ gingershard
def gingershard():
    m = M('gingershard', 32, 32, bg=92)
    t = [(-3.0, -2.2, 1.0), (2.8, -2.2, 0.9), (3.1, 1.0, 1.1), (-1.2, 2.8, 1.0)]
    b = [(-2.6, -2.2, -1.0), (3.0, -2.2, -1.1), (2.6, 1.4, -0.9), (-1.6, 2.3, -1.0)]
    R_top = (0, 0, 16, 16); R_bot = (16, 0, 16, 16)
    R_s = [(0, 16, 16, 8), (16, 16, 16, 8), (0, 24, 16, 8), (16, 24, 16, 8)]
    def face(lv):
        def fn(u, v, ix, iy):
            if iy >= 12: return m.pick(WHITE, 0.9, 0.25)
            if iy == 11: return 88
            return m.pick(GINGER, lv + (0.2 if u + v < 0.7 else 0), 0.3)
        return fn
    m.paint(R_top, face(0.6)); m.paint(R_bot, face(0.2))
    m.dot(R_top, 5, 4, 86); m.dot(R_top, 10, 7, 86)
    m.fill(R_s[0], WHITE, 0.8)
    for k in (1, 2, 3): m.fill(R_s[k], BAKED + GINGER[:2], 0.3 + 0.2 * k, 0.35)
    cen = (0, 0, 0)
    m.fan(t, R_top, (0, 1), cen); m.fan(b, R_bot, (0, 1), cen)
    for k in range(4):
        k1 = (k + 1) % 4
        m.quad([b[k], b[k1], t[k1], t[k]], R_s[k], cen)
    m.save([('idle', ident)])

# ------------------------------------------------------------------ bulb
def bulb():
    cols = {'skin_red': [248, 249, 250, 251], 'skin_green': [241, 242, 242, 243], 'skin_blue': [244, 244, 245, 246], 'skin_gold': [236, 237, 238, 239]}
    m = M('bulb', 16, 16, skins=list(cols), bg=188)
    R_sock = (0, 0, 6, 8); R_scap = (6, 0, 5, 5); R_band = (0, 8, 8, 8); R_tip = (8, 8, 8, 8)
    for i, (nm, ramp) in enumerate(cols.items()):
        m.cur = i
        m.paint_loft(R_sock, [0.6, -0.2, -0.8], [190, 188, 186, 184], noise=0.2)
        m.fill(R_scap, [188, 186, 184], 0.8, 0.2)
        m.paint_loft(R_band, [0.9, 0.3, -0.4], ramp, fb=True, noise=0.15)
        m.paint_loft(R_tip, [1.0, 0.4, -0.3], ramp, fb=True, noise=0.15)
        hi = 254 if nm != 'skin_gold' else 253
        m.dot(R_band, 3, 1, hi, 2, 1, True); m.dot(R_tip, 1, 1, hi, 2, 1, True)
    m.cur = 0
    tr = lambda z, r, ph: [(r * math.cos(math.radians(a + ph)), r * math.sin(math.radians(a + ph)), z) for a in (90, 210, 330)]
    m.loft([tr(0.45, 0.5, 0), tr(-0.45, 0.5, 0)], R_sock)
    m.fan(tr(0.45, 0.5, 0), R_scap, (0, 1), (0, 0, 0))
    m.loft([tr(-0.45, 0.5, 0), tr(-1.25, 0.95, 0)], R_band, center=(0, 0, -1.0))
    m.cone(tr(-1.25, 0.95, 0), (0, 0, -2.7), R_tip, center=(0, 0, -1.5))
    m.save([('idle', ident)])

# ------------------------------------------------------------------ flare
def flare():
    m = M('flare', 64, 32, bg=77)
    R_tube = (0, 0, 44, 18); R_str = (44, 0, 10, 18); R_tip = (54, 0, 10, 18); R_end = (0, 18, 8, 8)
    sq = lambda x, hw: ngon(x, 0, 0, hw * 1.4142, hw * 1.4142, 4); nz = side_nz(yz(sq(0, 1)))
    RED = [73, 75, 77, 79]
    def tube(a, f, j, ix, iy):
        if abs(a - 0.4) < 0.015 or abs(a - 0.56) < 0.015: return 162
        return None
    m.paint_loft(R_tube, nz, RED, cols=[(0.0, 0.06, [19, 20, 21]), (0.41, 0.55, PAPER)], extra=tube, noise=0.22)
    m.paint_loft(R_str, nz, [241, 242, 243], fb=True, noise=0.25)
    m.paint_loft(R_tip, nz, [243, 252, 253, 254], fb=True, noise=0.3)
    m.fill(R_end, [19, 20, 21], 0.4)
    m.loft([sq(-3.5, 0.5), sq(2.2, 0.5)], R_tube); m.loft([sq(2.2, 0.5), sq(2.6, 0.72)], R_str)
    m.cone(sq(2.6, 0.72), (3.5, 0, 0), R_tip, center=(0, 0, 0))
    m.fan(sq(-3.5, 0.5), R_end, (1, 2), (0, 0, 0))
    m.save([('idle', ident)])

# ------------------------------------------------------------------ wristband
def wristband():
    m = M('wristband', 64, 32, bg=22)
    R_out = (0, 0, 50, 14); R_in = (0, 14, 50, 10); R_bk = (50, 0, 12, 14)
    ring = lambda x: ngon(x, 0, 0, 1.41, 1.41, 8); nz = side_nz(yz(ring(0)))
    def out(a, f, j, ix, iy):
        if iy in (2, 11): return 161 if ix % 3 != 0 else None        # stitching
        if iy in (0, 1, 12, 13): return m.pick(LEATHER, 0.15, 0.2)
        if j == 5 and 0.35 < f < 0.65 and iy in (6, 7): return 19     # strap holes on the underside
        return None
    m.paint_loft(R_out, nz, LEATHER, swap=True, extra=out, noise=0.25)
    m.paint_loft(R_in, nz, LEATHER[:3], swap=True, noise=0.3)
    def bk(u, v, ix, iy):
        if 2 <= ix <= 9 and 3 <= iy <= 10:
            if ix in (5, 6): return 2
            return m.pick(LEATHER, 0.5, 0.2)
        return m.pick(BRASS, 0.9 if (ix < 2 or iy < 3) else 0.45, 0.2)
    m.paint(R_bk, bk)
    a, b = ring(-1.25), ring(1.25)
    m.loft([a, b], R_out, swap=True); m.loft([a, b], R_in, swap=True, flip=True, grp='in')
    z = 1.41 * math.cos(math.pi / 8) + 0.12
    m.quad([(-1.45, -0.62, z), (1.45, -0.62, z), (1.45, 0.62, z), (-1.45, 0.62, z)], R_bk, (0, 0, 0), swap=True)
    m.save([('idle', ident)])

for f in (platgun, lavagun, grapple, starhook, gingerblob, gingershard, bulb, flare, wristband):
    if len(sys.argv) < 2 or f.__name__ in sys.argv[1:]: f()
