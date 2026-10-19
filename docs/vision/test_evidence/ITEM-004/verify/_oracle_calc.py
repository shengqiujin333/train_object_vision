import math

s = 0.5
PX = 0.0
PY = 140.0
SW, SH = 1280, 720


def soft(bins):
    L = [0.0] * 16
    for b, v in bins.items():
        L[b] = v
    mx = max(L)
    es = [math.exp(l - mx) for l in L]
    su = sum(es)
    return sum(es[i] * i for i in range(16)) / su


def coords(s, i, j, d):
    st = 640.0 / (80 >> s)
    x1 = (-d[0] + j + 0.5) * st
    y1 = (-d[1] + i + 0.5) * st
    x2 = (d[2] + j + 0.5) * st
    y2 = (d[3] + i + 0.5) * st
    return x1, y1, x2, y2


def norm(mx1, my1, mx2, my2):
    def cl(v):
        return max(0.0, min(v, float(SW if v == v else v)))
    rx1 = (mx1 - PX) / s
    ry1 = (my1 - PY) / s
    rx2 = (mx2 - PX) / s
    ry2 = (my2 - PY) / s

    def c2(v, hi):
        return max(0.0, min(v, hi))
    cx1 = c2(rx1, SW)
    cy1 = c2(ry1, SH)
    cx2 = c2(rx2, SW)
    cy2 = c2(ry2, SH)
    if not (cx2 > cx1) or not (cy2 > cy1):
        return None
    nx = cx1 / SW
    ny = cy1 / SH
    nw = (cx2 - cx1) / SW
    nh = (cy2 - cy1) / SH
    if nx + nw > 1.0:
        nw = 1.0 - nx
    if ny + nh > 1.0:
        nh = 1.0 - ny
    if not (nw > 0.0) or not (nh > 0.0):
        return None
    return (nx, ny, nw, nh)


# g01: s0 cell(20,30) with non-trivial DFL logits
d = [soft({4: 5.0, 6: 2.0}), soft({2: 8.0}), soft({7: 9.0}), soft({1: 4.0, 2: 4.0})]
print("g01 d =", [round(x, 9) for x in d])
x1, y1, x2, y2 = coords(0, 20, 30, d)
print("g01 model box", round(x1, 5), round(y1, 5), round(x2, 5), round(y2, 5))
n = norm(x1, y1, x2, y2)
print("g01 src norm", [round(v, 7) for v in n] if n else None)
if n:
    print("g01 xywh=(%.5f,%.5f,%.5f,%.5f)" % n)

# g07 crop pixel grid for rail-like box s0(30,40) bins(4,4,4,4)
d4 = [4.0] * 4
x1, y1, x2, y2 = coords(0, 30, 40, d4)
print("g07 model crop box", x1, y1, x2, y2)
inrows = [oy for oy in range(SH) if 212.0 <= oy * 0.5 + 139.75 < 276.0]
incols = [ox for ox in range(SW) if 292.0 <= ox * 0.5 - 0.25 < 356.0]
print("g07 crop count", len(inrows), len(incols), "px area", len(inrows) * len(incols))

# proto bilinear 160->640: proto half pattern |f(px)| pattern.
# proto_mask model (640x640): pm(x,y) = bilinear over half plane.
# src sample mx = ox*0.5-0.25, my=oy*0.5+139.75 -> model coord.
# rail box crop: model box x in [292,356) i.e. model x = mx.
# Half pattern: proto col < 80 -> +1 else -1. proto->640 bilinear of +-1 block boundary.
# boundary in proto x=79.5 maps to model x: fx=(x+0.5)*4-0.5 -> proto index for model x m: p=(m+0.5)/4-0.5
# p=79.5 -> m = (79.5+0.5)*4-0.5 = 319.5. so model mask: cols m<319.5 ~ +1 region boundary at 319.5
# In crop region model x in [292,356): cols 292..319 have +1, cols 320..355 have -1 => src px x with mx=ox*0.5-0.25:
#   mx<319.5 -> ox*0.5-0.25<319.5 -> ox<639.5 -> ox<=639 ; ox>=640 -> -1
# but rail crop cols found above: incols 585..712. Positive: ox in [585,639] => 55 cols; negative [640,712]
pos_cols = [ox for ox in incols if ox <= 639]
print("g07/mk positive cols", pos_cols[0], pos_cols[-1], len(pos_cols))
neg_cols = [ox for ox in incols if ox >= 640]
print("g07/mk negative cols", neg_cols[0], neg_cols[-1], len(neg_cols))
print("g07 expected positive count", len(pos_cols) * len(inrows))
print("g07 black pixel px(600,240)=%d px(680,240)=%d px(400,240)=%d" % (600 <= 639, 680 > 639, 400 < 585))

# g08 offmask: use coeff pattern on proto rows instead: make proto all +1 except channel0 has +1 rows<80 / -1 rows>=80
# coeff vector = mostly 0 with channel0=1 => mask nonzero where proto row<80.
# same geometry: model y boundary: proto row 79.5 -> model y 319.5 (same formula) then crop rows inrange
# src sample my=oy*0.5+139.75 must be < 320 for +1 (proto row<80). But crop also applies to box y [212,276) -> my in [212,276)
# my<320 always true within crop -> positive = crop box area.
print("g08: offmask via row-pattern coeff => positive == crop area", len(inrows) * len(incols))

# g12 IoU mirror: I=2,W=34,H=56 ; U=(34+1)*2 -> wait g12 uses HUnit=2 box: x1 290 x2 292 y1 212 y2 268 => w=2 h=56
# IoU identical: (2*56)/(2*(2*56)+... ) with +1: i=(min+1)= (2-1+1)? recompute exactly
def iou_plus1(a, b):
    ax1, ay1, ax2, ay2 = a
    bx1, by1, bx2, by2 = b
    w = max(0.0, min(ax2, bx2) - max(ax1, bx1) + 1.0)
    h = max(0.0, min(ay2, by2) - max(ay1, by1) + 1.0)
    i = w * h
    u = (ax2 - ax1 + 1.0) * (ay2 - ay1 + 1.0) + (bx2 - bx1 + 1.0) * (by2 - by1 + 1.0) - i
    return 0.0 if u <= 0 else i / u


# g12: A s0(30,40) bins(4,4,4,4) -> model (292,212)-(356,276). B shifted +1: bins dl=5.
dA = [4.0] * 4
dB = [5.0, 4.0, 5.0, 4.0]
a = coords(0, 30, 40, dA)
b = coords(0, 30, 40, dB)
print("g12 A", a, "B", b, "IoU", iou_plus1(a, b))

# g13: A(30,40) bins(4,4,4,4), B(30,41) same bins -> shifted grid +1 stride => x offset st=16
b2 = coords(0, 30, 41, dA)
print("g13 B", b2, "IoU", iou_plus1(a, b2))

# g16: per-channel literal pan: proto chan k all +1*10^-k? keep trilinear exact mult: chan k filled (k+1)=1.0 except chan5=-3.0
# coeff vector = identity: coeff[k]=1.0 -> mask=chan k values; choose chan k values as +1 except chan5 -3
# With coeff=e_k, mask = channel k - all +1 / chan5 -3. But coeff vector could be e_5 => mask = -3 everywhere -> negative -> positive=0
# and e_0 => +1 everywhere (clamped crop) => positive = crop area. Combined assert via two boxes.
print("g16 pan: e_5 coeff -> positive=0; e_0 coeff -> positive=crop area")

# g17: uniform [+1/2,-1/2] alternating rows pattern on channel0, ident coeff => mask = -0.5 half rows, +0.5 half rows
# proto row r: value = r%2? need row band: with bilinear it blurs. choose block rows: proto rows <80 = +0.5, >=80 = -0.5
# mask model boundary same y=319.5 => positive rows: my<319.5 within crop [212,276) => all crop rows my<=275.75<319.5 => positive=crop area
print("g17 upper-half positive => positive == crop area (same as full)")

# lower-half pattern: rows>=80=+0.5 else -0.5 -> within crop my in [212,276) < 320 => all -0.5 => positive=0
print("g17b lower-half positive => positive == 0")

# LOCATE: s1 cell(15,15) bins(2,2,3,3): stride=640/40=16
d1 = [2.0, 2.0, 3.0, 3.0]
x1, y1, x2, y2 = coords(1, 15, 15, d1)
print("g18 s1 model box", x1, y1, x2, y2)
n1 = norm(x1, y1, x2, y2)
print("g18 norm", [round(v, 7) for v in n1] if n1 else None)

# s2 cell(12,10) bins(1,1,2,1): stride=640/20=32
d2 = [1.0, 1.0, 2.0, 1.0]
x1, y1, x2, y2 = coords(2, 12, 10, d2)
print("g19 s2 model box", x1, y1, x2, y2)
n2 = norm(x1, y1, x2, y2)
print("g19 norm", [round(v, 7) for v in n2] if n2 else None)
