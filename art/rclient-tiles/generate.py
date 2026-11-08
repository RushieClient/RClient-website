"""Original RClient slate atlas. No source images or third-party textures."""
from pathlib import Path
import math
import random
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
S = 3
T = 64
atlas = Image.new('RGBA', (1024 * S, 1024 * S))


def tile(index, top=False, right=False, bottom=False, left=False, variant=0):
    rng = random.Random(721 + variant)
    image = Image.new('RGBA', (T*S, T*S), (28, 43, 58, 255))
    d = ImageDraw.Draw(image)
    def poly(points, color):
        d.polygon([(int(x*S), int(y*S)) for x, y in points], fill=color)
    def line(points, color, width=1):
        d.line([(int(x*S), int(y*S)) for x, y in points], fill=color, width=width*S)
    # Keep the body quiet; shape comes from the exposed surface, not a repeated
    # diagonal pattern. Optional interior variants carry only a fine seam.
    if variant:
        y=rng.randrange(20,48)
        line([(0,y),(24,y-3),(44,y+2),(64,y)],(25,39,53,255))
    if top:
        poly([(0,0),(64,0),(64,5),(50,7),(40,4),(27,7),(13,5),(0,7)],(76,108,119,255))
        line([(0,1),(15,1),(28,2),(43,1),(64,1)],(106,141,150,255))
        line([(0,8),(15,6),(28,8),(42,5),(64,7)],(15,31,44,255))
    if bottom:
        poly([(0,59),(14,57),(30,60),(45,56),(64,58),(64,64),(0,64)],(15,27,39,255))
        line([(0,62),(20,61),(40,62),(64,61)],(51,69,80,255))
    if left:
        poly([(0,0),(4,0),(6,15),(3,31),(6,49),(4,64),(0,64)],(64,92,103,255))
        line([(1,0),(1,64)],(107,139,146,255))
    if right:
        poly([(60,0),(64,0),(64,64),(59,64),(61,43),(58,28)],(15,28,40,255))
        line([(63,0),(63,64)],(52,76,88,255))
    atlas.alpha_composite(image,((index%16)*T*S,(index//16)*T*S))


# 3 by 3 terrain block: top-left/top/top-right; left/fill/right; bottom row.
for y in range(3):
    for x in range(3):
        tile(1+x+y*16,top=y==0,right=x==2,bottom=y==2,left=x==0)
for x in range(3):
    tile(5+x,top=True,bottom=True,left=x==0,right=x==2)
for y in range(3):
    tile(9+y*16,top=y==0,bottom=y==2,left=True,right=True)
tile(11,top=True,right=True,bottom=True,left=True)
for v in range(8):
    tile(49+v,variant=v+1)

# Quiet rock interiors keep large formations from repeating the edge texture.
for v in range(4):
    core = Image.new('RGBA', (T*S, T*S), (28, 43, 58, 255))
    atlas.alpha_composite(core, (((57+v)%16)*T*S, 3*T*S))

# Transparent frost overlay; placed above the corresponding Game-layer freeze.
frost = Image.new('RGBA', (T*S, T*S), (84, 160, 181, 62))
fd = ImageDraw.Draw(frost)
fd.line([(0, 1*S), (T*S, 1*S)], fill=(180, 225, 231, 240), width=2*S)
for x, y in [(8,18), (34,39), (52,10), (18,51)]:
    fd.line([(x*S,y*S),((x+7)*S,(y+5)*S),((x+3)*S,(y+12)*S)],
            fill=(153, 213, 223, 150), width=S)
atlas.alpha_composite(frost, (0, 4*T*S))

nohook = Image.new('RGBA', (T*S, T*S), (86, 103, 113, 255))
nd = ImageDraw.Draw(nohook)
nd.line([(0,0),(T*S,0)], fill=(145,165,172,255), width=S)
for x,y in [(12,14),(43,45)]:
    nd.line([(x*S,y*S),((x+8)*S,(y+8)*S)], fill=(65,83,96,255), width=2*S)
atlas.alpha_composite(nohook, (T*S, 4*T*S))


def decoration(index, kind, seed):
    rng=random.Random(seed)
    image=Image.new('RGBA',(T*S,T*S))
    d=ImageDraw.Draw(image)
    def line(points,color,width=1):
        d.line([(round(x*S),round(y*S)) for x,y in points],fill=color,width=width*S)
    def poly(points,color):
        d.polygon([(round(x*S),round(y*S)) for x,y in points],fill=color)
    if kind in ('grass','flower'):
        for n in range(11):
            x=rng.randint(10,54); h=rng.randint(9,29)
            poly([(x,62),(x+rng.randint(-13,13),62-h),(x+4,62)],rng.choice([(62,91,86,255),(78,111,100,255),(45,74,77,255)]))
        if kind=='flower':
            for x,y in [(24,28),(40,36),(31,17)]:
                line([(x,59),(x-3,y+7),(x,y)],(72,114,104,255),2)
                for a in range(5):
                    xx=x+math.cos(a*math.tau/5)*4; yy=y+math.sin(a*math.tau/5)*4
                    d.ellipse(((xx-2)*S,(yy-3)*S,(xx+2)*S,(yy+3)*S),fill=(178,211,220,255))
                d.ellipse(((x-1)*S,(y-1)*S,(x+1)*S,(y+1)*S),fill=(93,150,167,255))
    elif kind=='roots':
        for n in range(6):
            x=rng.randint(8,56); end=rng.randint(19,58)
            path=[(x,0),(x-3,10),(x+4,end//2),(x-2,end)]
            line(path,(54,76,75,255),2)
            line([(x+4,end//2),(x+12,end//2+4)],(73,95,87,255))
    elif kind=='stone':
        for n in range(3):
            x=12+n*18; y=rng.randint(50,56)
            poly([(x-9,61),(x-7,y),(x+1,y-7),(x+9,y),(x+12,61)],(49,73,88,255))
            poly([(x-7,y),(x+1,y-7),(x+9,y),(x,55)],(82,109,121,255))
    atlas.alpha_composite(image,((index%16)*T*S,(index//16)*T*S))


for k,kind in enumerate(['grass','flower','roots','stone']):
    for v in range(4):
        decoration(81+k*16+v,kind,920+k*10+v)
atlas=atlas.resize((1024,1024),Image.Resampling.LANCZOS)
atlas.paste((0,0,0,0),(0,0,64,64))
atlas.save(ROOT/'rclient_slate.png')
assert atlas.mode=='RGBA' and atlas.size==(1024,1024)
assert atlas.getchannel('A').crop((0,0,64,64)).getbbox() is None
print('Created original 1024x1024 atlas; tile 0 transparent; 64px cells.')
