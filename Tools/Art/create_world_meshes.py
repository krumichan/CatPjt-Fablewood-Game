#!/usr/bin/env python3
"""Build original Fablewood environment meshes (centimetres, Z up, X forward).

Only Python's standard library is required. Each OBJ has one material slot,
explicit UVs and normals. Importing into Unreal is a separate parent pipeline.
Meshes contain no downloaded art or external dependencies.
"""
from pathlib import Path
from math import sin, cos, pi, sqrt, atan2
import json
import random

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ArtSource" / "Meshes"
OUT.mkdir(parents=True, exist_ok=True)


def add(a, b): return tuple(a[i] + b[i] for i in range(3))
def sub(a, b): return tuple(a[i] - b[i] for i in range(3))
def mul(a, k): return tuple(v * k for v in a)
def dot(a, b): return sum(a[i] * b[i] for i in range(3))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a):
    length = sqrt(dot(a, a))
    return mul(a, 1 / length) if length > 1e-9 else (0, 0, 1)
def lerp(a, b, t): return add(mul(a, 1-t), mul(b, t))


class Mesh:
    def __init__(self, name):
        self.name, self.vertices, self.uvs, self.normals, self.faces = name, [], [], [], []

    def triangle(self, a, b, c, uv=None, normals=None):
        if dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))) < 1e-12:
            return
        normal = unit(cross(sub(b, a), sub(c, a)))
        indices = []
        for i, p in enumerate((a, b, c)):
            self.vertices.append(p)
            self.uvs.append(uv[i] if uv else (p[0]/200, p[1]/200))
            self.normals.append(normals[i] if normals else normal)
            indices.append(len(self.vertices))
        self.faces.append(tuple(indices))

    def quad(self, a, b, c, d, uv=None):
        uv = uv or [(0, 0), (1, 0), (1, 1), (0, 1)]
        self.triangle(a, b, c, (uv[0], uv[1], uv[2]))
        self.triangle(a, c, d, (uv[0], uv[2], uv[3]))

    def box(self, center, size, yaw=0):
        sx, sy, sz = [v*.5 for v in size]
        points = []
        for x, y, z in [(-sx,-sy,-sz),(sx,-sy,-sz),(sx,sy,-sz),(-sx,sy,-sz),
                        (-sx,-sy,sz),(sx,-sy,sz),(sx,sy,sz),(-sx,sy,sz)]:
            points.append(add(center, (x*cos(yaw)-y*sin(yaw), x*sin(yaw)+y*cos(yaw), z)))
        for face in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]:
            self.quad(*(points[i] for i in face))

    def beam(self, a, b, width, depth=None):
        direction = unit(sub(b, a))
        side = unit(cross(direction, (0,0,1) if abs(direction[2]) < .9 else (0,1,0)))
        up = unit(cross(direction, side))
        side, up = mul(side, width/2), mul(up, (depth or width)/2)
        rings = [[add(p, add(mul(side, sx), mul(up, sy))) for sx, sy in [(-1,-1),(1,-1),(1,1),(-1,1)]] for p in (a,b)]
        self.quad(*reversed(rings[0]))
        self.quad(*rings[1])
        for i in range(4):
            j = (i+1)%4
            self.quad(rings[0][i],rings[0][j],rings[1][j],rings[1][i])

    def tube(self, points, radii, sides=12, seed=0):
        rng = random.Random(seed)
        offsets = [rng.uniform(.90,1.10) for _ in range(sides)]
        rings, normal_rings = [], []
        for k, (p,r) in enumerate(zip(points,radii)):
            tangent = unit(sub(points[min(k+1,len(points)-1)], points[max(0,k-1)]))
            side = unit(cross((0,1,0) if abs(tangent[1]) < .9 else (1,0,0), tangent))
            up = unit(cross(tangent,side))
            normals = [add(mul(side,cos(i*2*pi/sides)),mul(up,sin(i*2*pi/sides))) for i in range(sides)]
            rings.append([add(p,mul(n,r*offsets[i])) for i,n in enumerate(normals)])
            normal_rings.append(normals)
        for k in range(len(rings)-1):
            for i in range(sides):
                j=(i+1)%sides
                a,b,c,d=rings[k][i],rings[k][j],rings[k+1][j],rings[k+1][i]
                na,nb,nc,nd=normal_rings[k][i],normal_rings[k][j],normal_rings[k+1][j],normal_rings[k+1][i]
                uv=[(i/sides,k*.25),(j/sides,k*.25),(j/sides,(k+1)*.25),(i/sides,(k+1)*.25)]
                self.triangle(a,b,c,uv[:3],(na,nb,nc))
                self.triangle(a,c,d,(uv[0],uv[2],uv[3]),(na,nc,nd))
        for i in range(sides):
            j=(i+1)%sides
            self.triangle(points[0],rings[0][j],rings[0][i])
            self.triangle(points[-1],rings[-1][i],rings[-1][j])

    def leaf(self, center, length, width, yaw, lift=0, roll=0, lobed=True):
        # A closed folded blade with notched margins, not a sphere or billboard.
        outline = [(0,0),(.16,.38),(.28,.24),(.40,.52),(.53,.32),(.66,.44),(.78,.23),(1,0)] if lobed else [(0,0),(.25,.38),(.55,.5),(.8,.30),(1,0)]
        outline += [(u,-v) for u,v in outline[-2:0:-1]]
        axis=(cos(yaw)*cos(lift),sin(yaw)*cos(lift),sin(lift))
        side=(-sin(yaw)*cos(roll),cos(yaw)*cos(roll),sin(roll))
        up=unit(cross(axis,side))
        def point(u,v,h=0):
            return add(center,add(add(mul(axis,(u-.45)*length),mul(side,v*width)),mul(up,h)))
        rim=[point(u,v,-sin(u*pi)*length*.045) for u,v in outline]
        ridge=point(.43,0,length*.065)
        underside=point(.43,0,-length*.032)
        for i in range(len(rim)):
            j=(i+1)%len(rim)
            uv=((.43,.5),(outline[i][0],outline[i][1]+.5),(outline[j][0],outline[j][1]+.5))
            # Outline travels clockwise; reverse it for the outward upper face.
            self.triangle(ridge,rim[j],rim[i],(uv[0],uv[2],uv[1]))
            self.triangle(underside,rim[i],rim[j],uv)

    def save(self):
        path=OUT/(self.name+".obj")
        with path.open("w",encoding="utf-8",newline="\n") as file:
            file.write("# Original Fablewood mesh, centimetres; Z-up, X-forward\n")
            file.write("o "+self.name+"\n")
            for p in self.vertices: file.write("v %.5f %.5f %.5f\n"%p)
            for uv in self.uvs: file.write("vt %.6f %.6f\n"%uv)
            for n in self.normals: file.write("vn %.6f %.6f %.6f\n"%n)
            file.write("usemtl FablePaint\ns off\n")
            for f in self.faces: file.write("f "+" ".join("%d/%d/%d"%(i,i,i) for i in f)+"\n")
        bounds=[[round(min(v[i] for v in self.vertices),2),round(max(v[i] for v in self.vertices),2)] for i in range(3)]
        return {"file":path.name,"vertices":len(self.vertices),"triangles":len(self.faces),"bounds_cm":bounds}


def terrain():
    mesh=Mesh("SM_ForestTerrain")
    def point(x,y):
        # The arena stays flat; subtle painted outer-bank undulations remain decorative.
        outer=max(0,min(1,(max(abs(x)/1500,abs(y)/1600)-1)/.6))
        z=-.7+outer*(6+7*sin(x*.003)*cos(y*.004))
        stream_x=-2180+95*sin(y*.0011)+25*sin(y*.004)
        channel=max(0,min(1,1-max(0,abs(x-stream_x)-225)/100))
        z=z*(1-channel)-9*channel
        # Keep dirt ribbons and the low bridge approaches above their grass substrate.
        if abs(x-120*sin(y*.0017)) < 350 or (x < -1000 and abs(y+350) < 245):
            z=min(z,-.7)
        return x,y,z
    for i in range(60):
        for j in range(60):
            x,y=-3000+i*100,-3000+j*100
            p=[point(x,y),point(x+100,y),point(x+100,y+100),point(x,y+100)]
            mesh.quad(*p,uv=[(v[0]/200,v[1]/200) for v in p])
    return mesh


def dirt_path():
    mesh=Mesh("SM_ForestPath")
    # Several long connected ribbons are less geometric than overlapping flat circles.
    def ribbon(points,width,seed):
        rng=random.Random(seed)
        left,right=[],[]
        for i,p in enumerate(points):
            tangent=unit(sub(points[min(i+1,len(points)-1)],points[max(0,i-1)]))
            across=(-tangent[1],tangent[0],0)
            w=width*(.92+.08*sin(i*.7)+rng.uniform(-.06,.06))
            left.append(add(p,mul(across,w*.5))); right.append(add(p,mul(across,-w*.5)))
        for i in range(len(points)-1):
            mesh.quad(right[i],right[i+1],left[i+1],left[i],uv=[(0,i*.4),(0,(i+1)*.4),(1,(i+1)*.4),(1,i*.4)])
    ribbon([(100*sin(y*.0016),y,2.2) for y in range(-3000,-1050,65)],410,8)
    ribbon([(140*sin(y*.0018),y,2.2) for y in range(1200,2940,65)],390,9)
    ribbon([(-2700+i*65,-350+50*sin(i*.15),2.3) for i in range(25)],330,10)
    rng=random.Random(23)
    rim=[]
    for i in range(96):
        a=i*2*pi/96
        r=1+.035*sin(a*7)+.025*sin(a*13)+rng.uniform(-.01,.01)
        rim.append((cos(a)*1275*r,sin(a)*1370*r+120,2.1))
    for i in range(len(rim)):
        mesh.triangle((0,120,2.1),rim[i],rim[(i+1)%len(rim)])
    return mesh


def brook():
    mesh=Mesh("SM_ForestBrook")
    for i in range(88):
        y=-2930+i*67
        def point(y,u):
            x=-2180+95*sin(y*.0011)+25*sin(y*.004)
            return (x+u*(185+18*sin(y*.002)),y,2.3+.32*sin(y*.035+u*3))
        for j in range(6):
            u=-1+j/3
            mesh.quad(point(y,u),point(y,u+1/3),point(y+67,u+1/3),point(y+67,u))
    return mesh


def tree(seed,suffix):
    rng=random.Random(seed)
    bark,leaves=Mesh("SM_ForestOakTrunk"+suffix),Mesh("SM_ForestOakLeaves"+suffix)
    twist=-1 if seed%2 else 1
    trunk=[(0,0,0),(12,8,110),(-16,15,260),(24*twist,20,420),(38*twist,-4,570),(15*twist,30,710),(70*twist,15,850),(58*twist,24,970)]
    bark.tube(trunk,[108,83,69,58,49,36,23,4],16,seed)
    for k in range(9):
        a=k*2*pi/9+rng.uniform(-.12,.12)
        length=rng.uniform(170,250)
        bark.tube([(0,0,100),(cos(a)*70,sin(a)*70,38),(cos(a)*length*.7,sin(a)*length*.7,9),(cos(a)*length,sin(a)*length,0)],[43,35,17,1],9,seed+k)
    crowns=[]
    for k in range(18):
        a=k*2.39996+rng.uniform(-.2,.2)
        height=500+(k%6)*72
        reach=rng.uniform(240,380)*(1-.15*((height-500)/400))
        start=(20*twist,15,height)
        tip=(cos(a)*reach,sin(a)*reach,height+rng.uniform(120,230))
        midpoint=(tip[0]*.62,tip[1]*.62,height+55)
        bark.tube([start,midpoint,tip],[29-(k%6)*2.7,14,2],10,seed+31+k)
        branchside=(cos(a+.65)*reach*.78,sin(a+.65)*reach*.78,tip[2]-30)
        bark.tube([midpoint,lerp(midpoint,branchside,.6),branchside],[12,7,1],8,seed+60+k)
        crowns.extend([tip,branchside])
    crowns.append((50*twist,20,1010))
    for c,center in enumerate(crowns):
        # Radially layered leaf sprays: broad pointed lobes make a broken forest silhouette.
        for k in range(19):
            a=k*2.39996+c*.21
            radius=sqrt((k+.5)/19)*rng.uniform(115,155)
            position=add(center,(cos(a)*radius,sin(a)*radius,rng.uniform(-32,35)))
            leaves.leaf(position,rng.uniform(74,118),rng.uniform(49,78),a+rng.uniform(-.5,.5),
                        rng.uniform(-.36,.40),rng.uniform(-.27,.27),True)
    return bark,leaves


def rock():
    mesh=Mesh("SM_ForestRock")
    rng=random.Random(93)
    rings=[]
    for z,rx,ry in [(-8,74,58),(17,98,71),(67,78,62),(99,34,30)]:
        rings.append([(cos(i*2*pi/11)*(rx*rng.uniform(.85,1.15))+z*.12,
                       sin(i*2*pi/11)*(ry*rng.uniform(.9,1.1)),z+rng.uniform(-8,8)) for i in range(11)])
    for k in range(3):
        for i in range(11):
            j=(i+1)%11
            mesh.quad(rings[k][i],rings[k][j],rings[k+1][j],rings[k+1][i])
    for i in range(11):
        mesh.triangle((9,0,105),rings[-1][i],rings[-1][(i+1)%11])
        mesh.triangle((0,0,-10),rings[0][(i+1)%11],rings[0][i])
    return mesh


def grass():
    mesh=Mesh("SM_ForestGrass")
    rng=random.Random(104)
    for i in range(23):
        a=i*2.39996
        x,y=cos(a)*rng.uniform(3,27),sin(a)*rng.uniform(3,27)
        h=rng.uniform(24,54); bend=rng.uniform(10,30); w=rng.uniform(2,4.8)
        axis=(cos(a),sin(a),0); side=(-sin(a),cos(a),0)
        p0=(x,y,0); p1=(x+axis[0]*bend*.35,y+axis[1]*bend*.35,h*.65); p2=(x+axis[0]*bend,y+axis[1]*bend,h)
        mesh.quad(add(p0,mul(side,-w/2)),add(p0,mul(side,w/2)),add(p1,mul(side,w*.32)),add(p1,mul(side,-w*.32)))
        mesh.triangle(add(p1,mul(side,-w*.32)),add(p1,mul(side,w*.32)),p2)
    return mesh


def fern():
    mesh=Mesh("SM_ForestFern")
    for f in range(8):
        yaw=f*2.39996
        length=78+(f%3)*15
        points=[]
        for i in range(12):
            t=i/11
            points.append((cos(yaw)*length*t,sin(yaw)*length*t,6+sin(t*pi*.85)*(38+(f%2)*12)))
        mesh.tube(points,[1.8*(1-i/12) for i in range(12)],5,200+f)
        for i in range(2,11):
            t=i/11
            for side in [-1,1]:
                a=yaw+side*(.9+t*.4)
                l=(1-t)*32+6
                p=add(points[i],(cos(a)*l*.40,sin(a)*l*.40,2))
                mesh.leaf(p,l,l*.38,a,.03,side*.12,False)
        mesh.leaf(add(points[-1],(cos(yaw)*8,sin(yaw)*8,0)),22,8,yaw,-.28,0,False)
    return mesh


def flower():
    stems,petals,hearts=Mesh("SM_ForestFlowerStems"),Mesh("SM_ForestFlowerPetals"),Mesh("SM_ForestFlowerHearts")
    for i in range(5):
        a=i*2.39996; x,y=cos(a)*(10+i*4),sin(a)*(10+i*4); h=36+(i%3)*12
        p=(x+4*sin(a),y+3*cos(a),h)
        stems.tube([(x,y,0),(x+2,y,h*.5),p],[1.3,1,.8],5,i)
        stems.leaf((x+5,y,h*.40),20,10,a,.28,.1,False)
        stems.leaf((x-5,y,h*.66),17,9,a+2,.15,-.1,False)
        for j in range(7):
            yaw=j*2*pi/7
            petals.leaf(add(p,(cos(yaw)*7,sin(yaw)*7,0)),16,8,yaw,.06,0,False)
        hearts.tube([add(p,(0,0,-.4)),add(p,(0,0,2.5))],[4.3,3.5],9,i)
    return stems,petals,hearts


def cottage():
    walls,roof,timber,windows,stone=[Mesh("SM_ForestCottage"+n) for n in ("Walls","Roof","Timber","Windows","Stone")]
    walls.box((0,0,150),(500,420,300))
    # Solid plaster gables, ridgeline along Y.
    for y in [-210,210]:
        a,b,c=(-250,y,300),(250,y,300),(0,y,486)
        walls.triangle(*( (a,b,c) if y<0 else (c,b,a) ))
    # Each roof tile is a gently cambered overlapping slab, preserving scalloped eaves.
    for side in [-1,1]:
        for row in range(9):
            for col in range(11):
                x0=row*33; x1=(row+1)*33+5
                y0=-267+col*49+(row%2)*-8
                y1=y0+51
                def top(x,y,raise_z):
                    sag=4*sin((y+267)/534*pi)
                    return (side*x,y,500-x*.665+sag+raise_z)
                midy=(y0+y1)/2
                a,b,c,d,e,f=top(x0,y0,4),top(x1,y0,4),top(x1,midy,7),top(x1,y1,4),top(x0,y1,4),top(x0,midy,7)
                if side>0:
                    roof.quad(a,b,c,f); roof.quad(f,c,d,e)
                else:
                    roof.quad(f,c,b,a); roof.quad(e,d,c,f)
                roof.quad(b,top(x1,y0,-2),top(x1,y1,-2),d)
                roof.quad(a,top(x0,y0,-2),top(x1,y0,-2),b)
                roof.quad(d,top(x1,y1,-2),top(x0,y1,-2),e)
    roof.tube([(0,-282,505),(0,285,505)],[17,17],8,10)
    # Structural timber follows actual wall and gable edges.
    for y in [-216,216]:
        for x in [-247,0,247]:
            timber.beam((x,y,0),(x,y,302),15,16)
        for z in [18,163,295]:
            timber.beam((-250,y,z),(250,y,z),14,16)
        timber.beam((-265,y,296),(0,y,497),17,19)
        timber.beam((0,y,497),(265,y,296),17,19)
        timber.beam((0,y,300),(0,y,482),14,16)
        for side in [-1,1]:
            timber.beam((side*18,y,310),(side*175,y,310),10,14)
            timber.beam((side*18,y,310),(side*175,y,410),10,14)
    for x in [-256,256]:
        for y in [-200,0,200]:
            timber.beam((x,y,0),(x,y,300),14,14)
        timber.beam((x,-210,150),(x,210,150),14,14)
        timber.beam((x,-210,295),(x,210,295),14,14)
        timber.beam((x,-205,25),(x,-12,150),12,12)
        timber.beam((x,12,150),(x,205,25),12,12)
    # Front door has planks, lintel and a small inset transom.
    for k in range(6):
        timber.box((-42+k*16,-225,85),(14,13,170))
    for x in [-54,54]:
        timber.box((x,-235,89),(12,14,178))
    timber.box((0,-235,177),(119,14,14))
    for z in [30,140]:
        timber.box((0,-235,z),(97,8,10))
    for x in [-150,150]:
        windows.box((x,-221,207),(83,5,83))
        for dx in [-48,48]:
            timber.box((x+dx,-231,207),(10,15,103))
        for dz in [-48,0,48]:
            timber.box((x,-234,207+dz),(102,15,9))
        timber.box((x,-235,207),(8,14,95))
        timber.box((x,-241,153),(118,37,13))
        # Side shutters add a readable hand-built silhouette.
        for side in [-1,1]:
            timber.box((x+side*65,-226,207),(23,10,83),side*.17)
    windows.box((0,-221,384),(42,5,56))
    stone.box((0,0,9),(520,440,18))
    for k in range(5):
        for j in range(3):
            stone.box((155+(j%2)*3,75,k*37+369),(78,78,35))
            break
    stone.box((155,75,558),(97,95,20))
    stone.box((0,-251,7),(150,77,14))
    return walls,roof,timber,windows,stone


def bridge():
    stone=Mesh("SM_ForestBridge")
    def deck(x): return 14+100*(1-(x/380)**2)
    # Voussoirs have an open lower arch, unlike stacked flat primitive blocks.
    for i in range(18):
        x0=-380+i*(760/18); x1=-380+(i+1)*(760/18)
        b0=-110+183*sqrt(max(0,1-(x0/380)**2))
        b1=-110+183*sqrt(max(0,1-(x1/380)**2))
        z0,z1=deck(x0),deck(x1)
        p=[(x0,-185,b0),(x1,-185,b1),(x1,185,b1),(x0,185,b0),
           (x0,-185,z0),(x1,-185,z1),(x1,185,z1),(x0,185,z0)]
        for f in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]:
            stone.quad(*(p[k] for k in f))
        # Visible narrow mortar seams and individual coping stones.
        cx=(x0+x1)/2
        for y in [-204,204]:
            stone.box((cx,y,deck(cx)+24),(39,38,47))
            stone.box((cx,y,deck(cx)+51),(41,46,8))
    for x in [-345,345]:
        for y in [-204,204]:
            stone.box((x,y,45),(63,63,112))
            stone.box((x,y,105),(71,71,12))
    return stone


def mushrooms():
    stem,cap=Mesh("SM_ForestMushroomStems"),Mesh("SM_ForestMushroomCaps")
    for i in range(3):
        x,y=(i-1)*19,(i%2)*16; h=22+i*8
        stem.tube([(x,y,0),(x+3,y,h*.65),(x+2,y,h)],[4,3,3],8,i)
        points=[(x+2+cos(j*2*pi/16)*(17+i*3),y+sin(j*2*pi/16)*(17+i*3),h-2) for j in range(16)]
        for j in range(16):
            cap.triangle((x+2,y,h+9),points[j],points[(j+1)%16])
            cap.triangle((x+2,y,h-4),points[(j+1)%16],points[j])
    return stem,cap


def main():
    meshes=[terrain(),dirt_path(),brook(),rock(),grass(),fern(),bridge()]
    meshes.extend(tree(38,""))
    meshes.extend(tree(73,"B"))
    meshes.extend(flower())
    meshes.extend(cottage())
    meshes.extend(mushrooms())
    manifest={"coordinate_system":"centimetres; Z-up; X-forward; right-handed OBJ; importer reflects Y",
              "material_slots":1,"generated_assets":[m.save() for m in meshes]}
    path=OUT/"forest-mesh-manifest.json"
    path.write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    print(json.dumps({"assets":len(meshes),"triangles":sum(len(m.faces) for m in meshes),"output":str(OUT)},indent=2))


if __name__ == "__main__":
    main()
