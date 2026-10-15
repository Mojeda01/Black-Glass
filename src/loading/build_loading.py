#!/usr/bin/env python3
import math
import subprocess
import sys
import tempfile
from pathlib import Path

WIDTH = 640
HEIGHT = 360
FPS = 25
FRAMES = 62
OUT = Path(__file__).with_name("loading.gif")

def chain(statements):
    return ";".join(statements)

def fade_statements():
    return [
        "st(2,clip(T/0.45,0,1))",
        "st(2,ld(2)*ld(2)*(3-2*ld(2)))",
        "st(3,clip((2.44-T)/0.4,0,1))",
        "st(2,ld(2)*ld(3)*ld(3)*(3-2*ld(3)))",
    ]

def slab_expression():
    return chain([
        "st(0,(X-W/2)/H)",
        "st(1,(Y-H/2)/H+0.06)",
    ] + fade_statements() + [
        "st(4,abs(ld(0))-0.335)",
        "st(5,abs(ld(1))-0.145)",
        "st(3,hypot(max(ld(4),0),max(ld(5),0))+min(max(ld(4),ld(5)),0)-0.025)",
        "st(4,clip((T-0.35)/1.35,0,1))",
        "st(4,ld(4)*ld(4)*(3-2*ld(4)))",
        "st(4,-0.75+1.5*ld(4))",
        "st(5,ld(0)+0.55*ld(1))",
        "st(4,exp(-pow((ld(5)-ld(4))/0.07,2)))",
        "st(6,clip(-ld(3)/0.006,0,1))",
        "st(7,exp(-pow(ld(3)/0.0035,2)))",
        "st(8,clip(0.5-ld(1)/0.34,0,1))",
        "st(9,3+5*clip(1-1.5*hypot(ld(0),ld(1)-0.06),0,1))",
        "clip(ld(2)*(ld(9)"
        "+ld(6)*(10+14*ld(8)+6*exp(-pow((ld(5)+0.25)/0.05,2)))"
        "+ld(7)*(60+95*ld(8))"
        "+ld(4)*(ld(6)*80+ld(7)*175)"
        "+(1-ld(6))*(ld(4)*exp(-max(ld(3),0)/0.03)*30+18*exp(-max(ld(3),0)/0.05)*(0.6+0.8*ld(4)))"
        "+14*exp(-pow(ld(0)/0.42,2)-pow((ld(1)-0.185)/0.02,2))),0,255)",
    ])

def line_expression(fill, track, glow):
    return chain([
        "st(0,(X-W/2)/H)",
        "st(1,(Y-H/2)/H)",
    ] + fade_statements() + [
        "st(3,clip((T-0.25)/1.9,0,1))",
        "st(3,ld(3)*ld(3)*(3-2*ld(3)))",
        "st(3,-0.2+0.4*ld(3))",
        "st(4,clip(1-abs(ld(1)-0.17)/0.004,0,1)*between(ld(0),-0.2,0.2))",
        "st(5,ld(4)*lt(ld(0),ld(3)))",
        "st(6,ld(4)*(1-lt(ld(0),ld(3)))*0.45)",
        "st(7,exp(-pow((ld(0)-ld(3))/0.02,2))*exp(-pow((ld(1)-0.17)/0.006,2))*gt(ld(3),-0.199))",
        "255*ld(2)*clip(%s*ld(5)+%s*ld(6)+%s*ld(7),0,1)" % (fill, track, glow),
    ])


LETTER_SPACING = 0.34
WORD = "BLACK GLASS"
TEXT_HEIGHT = 0.055
TEXT_CENTER = 0.44

def arc(cx, cy, rx, ry, start, end, segments=24):
    points = []
    for i in range(segments + 1):
        a = math.radians(start + (end - start) * i / segments)
        points.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return points

def glyph(letter):
    if letter == "B":
        top = [(0, 1), (0.3, 1)] + arc(0.3, 0.75, 0.22, 0.25, 90, -90) + [(0, 0.5)]
        bottom = [(0, 0.5), (0.32, 0.5)] + arc(0.32, 0.25, 0.24, 0.25, 90, -90) + [(0, 0)]
        return 0.56, [[(0, 0), (0, 1)], top, bottom]
    if letter == "L":
        return 0.46, [[(0, 1), (0, 0), (0.46, 0)]]
    if letter == "A":
        return 0.62, [[(0, 0), (0.31, 1), (0.62, 0)], [(0.12, 0.36), (0.5, 0.36)]]
    if letter == "C":
        return 0.68, [arc(0.34, 0.5, 0.34, 0.5, 40, 320, 36)]
    if letter == "K":
        return 0.55, [[(0, 1), (0, 0)], [(0.5, 1), (0.02, 0.42)], [(0.17, 0.56), (0.55, 0)]]
    if letter == "G":
        return 0.7, [arc(0.35, 0.5, 0.35, 0.5, 40, 360, 40) + [(0.38, 0.5)]]
    if letter == "S":
        curve = arc(0.28, 0.75, 0.27, 0.25, 20, 270, 24) + arc(0.28, 0.25, 0.28, 0.25, 90, -160, 24)[1:]
        return 0.56, [curve]
    return 0.5, []

def wordmark_bounds():
    width = sum(glyph(c)[0] for c in WORD) + LETTER_SPACING * (len(WORD) - 1)
    return width * TEXT_HEIGHT * HEIGHT

def render_wordmark(directory):
    scale = 2
    w, h = WIDTH * scale, HEIGHT * scale
    unit = TEXT_HEIGHT * h
    total = sum(glyph(c)[0] for c in WORD) + LETTER_SPACING * (len(WORD) - 1)
    x = w / 2 - total * unit / 2
    baseline = h * TEXT_CENTER + unit / 2
    paths = []
    for letter in WORD:
        width, strokes = glyph(letter)
        for stroke in strokes:
            points = " ".join("%.2f,%.2f" % (x + px * unit, baseline - py * unit) for px, py in stroke)
            paths.append('<polyline points="%s"/>' % points)
        x += (width + LETTER_SPACING) * unit
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">'
           '<rect width="100%%" height="100%%" fill="#000"/>'
           '<g fill="none" stroke="#fff" stroke-width="%.2f" stroke-linecap="round" stroke-linejoin="round">%s</g></svg>'
           ) % (w, h, w, h, 1.5 * scale, "".join(paths))
    svg_path = Path(directory) / "wordmark.svg"
    png_path = Path(directory) / "wordmark.png"
    svg_path.write_text(svg)
    subprocess.run(["rsvg-convert", "-o", str(png_path), str(svg_path)], check=True)
    return png_path

def reveal_expression():
    left = WIDTH / 2 - wordmark_bounds() / 2 - 14
    span = wordmark_bounds() + 40
    return chain([
        "st(0,clip((T-0.95)/1.1,0,1))",
        "st(0,ld(0)*ld(0)*(3-2*ld(0)))",
        "st(1,%.2f+%.2f*ld(0))" % (left, span),
        "255*clip((ld(1)-X)/16,0,1)",
    ])

def build(wordmark):
    size = "%dx%d" % (WIDTH, HEIGHT)
    duration = "%.3f" % (FRAMES / FPS)
    graph = (
        "color=c=black:s=%s:r=%d:d=%s,format=gray,geq=lum='%s',format=gbrp[a];"
        "color=c=black:s=%s:r=%d:d=%s,format=gbrp,geq="
        "r='%s':g='%s':b='%s'[b];"
        "[2:v]format=gray,scale=%d:%d:flags=lanczos,format=gray[m];"
        "color=c=black:s=%s:r=%d:d=%s,format=gray,geq=lum='%s'[r];"
        "[m][r]blend=all_mode=multiply,split[t0][t1];"
        "[t1]gblur=sigma=4[glow];"
        "[t0][glow]blend=all_mode=addition:all_opacity=0.7,"
        "fade=t=out:st=2.04:d=0.4,format=gbrp[c];"
        "[a][b]blend=all_mode=lighten[ab];"
        "[ab][c]blend=all_mode=lighten,split[s0][s1];"
        "[s0]palettegen=max_colors=128:stats_mode=full[p];"
        "[s1][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle"
    ) % (size, FPS, duration, slab_expression(),
         size, FPS, duration,
         line_expression(0.9, 0.3, 0.9),
         line_expression(0.12, 0.3, 0.55),
         line_expression(0.10, 0.32, 0.5),
         WIDTH, HEIGHT,
         size, FPS, duration, reveal_expression())
    command = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "warning",
               "-f", "lavfi", "-i", "nullsrc=s=16x16:r=25:d=0.1",
               "-f", "lavfi", "-i", "nullsrc=s=16x16:r=25:d=0.1",
               "-loop", "1", "-framerate", str(FPS), "-t", duration, "-i", str(wordmark),
               "-filter_complex", graph, "-frames:v", str(FRAMES), "-loop", "0", str(OUT)]
    return command

if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        result = subprocess.run(build(render_wordmark(directory)))
    sys.exit(result.returncode)
