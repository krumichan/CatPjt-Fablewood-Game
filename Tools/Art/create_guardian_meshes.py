"""Build the Fablewood forest guardian: centimetres, Z up, +X forward.

No external packages. Every OBJ contains one material slot and explicit smooth
normals/UVs. Arm and leg files use the component pivots documented in ASSEMBLY.
The body, bark, face and foliage share the actor's capsule-centred origin.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import random

TAU = math.tau
ASSEMBLY = {"left_shoulder": [0, -48, 26], "right_shoulder": [0, 48, 26],
            "left_hip": [0, -24, -43], "right_hip": [0, 24, -43]}


def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def mul(a, k): return tuple(x * k for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a):
    length = math.sqrt(dot(a, a))
    return mul(a, 1 / length) if length > 1e-9 else (0, 0, 1)
def mix(a, b, t): return add(mul(a, 1-t), mul(b, t))


class Mesh:
    def __init__(self, name):
        self.name, self.vertices, self.uvs, self.faces = name, [], [], []

    def vertex(self, p, uv=(0, 0)):
        self.vertices.append(tuple(p)); self.uvs.append(uv)
        return len(self.vertices) - 1

    def triangle(self, a, b, c):
        # Tapered leaves deliberately meet at their tips: omit zero-area faces.
        normal = cross(sub(self.vertices[b], self.vertices[a]), sub(self.vertices[c], self.vertices[a]))
        if dot(normal, normal) > 1e-12:
            self.faces.append((a, b, c))

    def quad(self, a, b, c, d):
        self.triangle(a, b, d); self.triangle(b, c, d)

    def sweep(self, controls, sides=12, subdivisions=3, ridges=0.09, twist=0.5):
        """Parallel-transport loft; controls are x,y,z,radiusA,radiusB.

        Sculpted radial grooves and Catmull-Rom centre lines replace primitive
        cylinders. End caps make every branch closed, including narrow twigs.
        """
        samples = []
        for i in range(len(controls)-1):
            p0, p1 = controls[max(0, i-1)], controls[i]
            p2, p3 = controls[i+1], controls[min(len(controls)-1, i+2)]
            for j in range(subdivisions):
                t = j/subdivisions
                samples.append(tuple(0.5*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t)
                                     for a,b,c,d in zip(p0,p1,p2,p3)))
        samples.append(controls[-1])
        rings = []
        frame = None
        for i, sample in enumerate(samples):
            p = sample[:3]
            tangent = unit(sub(samples[min(i+1,len(samples)-1)][:3], samples[max(0,i-1)][:3]))
            if frame is None:
                reference = (1,0,0) if abs(tangent[2]) > .7 else (0,0,1)
                frame = unit(cross(reference, tangent))
            else:
                frame = unit(sub(frame, mul(tangent, dot(frame,tangent))))
            side = cross(tangent, frame)
            t = i/(len(samples)-1)
            ring = []
            for j in range(sides+1):
                theta = TAU*j/sides + twist*t
                bark = 1 + ridges*math.cos(theta*5 + t*2.8) + ridges*.3*math.sin(theta*9-t*4)
                offset = add(mul(frame, math.cos(theta)*max(.12,sample[3])*bark),
                             mul(side, math.sin(theta)*max(.12,sample[4])*bark))
                ring.append(self.vertex(add(p,offset),(j/sides,t)))
            rings.append(ring)
        for low, high in zip(rings,rings[1:]):
            for j in range(sides): self.quad(low[j],low[j+1],high[j+1],high[j])
        first = self.vertex(samples[0][:3],(.5,0))
        last = self.vertex(samples[-1][:3],(.5,1))
        for j in range(sides):
            self.triangle(first,rings[0][j+1],rings[0][j])
            self.triangle(last,rings[-1][j],rings[-1][j+1])

    def leaf(self, base, tip, width, normal=(0,0,1), height=1.5):
        axis = unit(sub(tip,base))
        side = unit(cross(normal,axis))
        face = unit(cross(axis,side))
        top, bottom = [], []
        stations = (0,.16,.4,.66,.87,1)
        for t in stations:
            spine = add(mix(base,tip,t),mul(face,math.sin(math.pi*t)*height*.65))
            w = width * math.sin(math.pi*t)**.75
            top_row, bottom_row = [], []
            for q in (-1,-.5,0,.5,1):
                p = add(spine,mul(side,w*q))
                top_row.append(self.vertex(add(p,mul(face,math.sin(math.pi*t)*(1-abs(q))*height)),((q+1)/2,t)))
                bottom_row.append(self.vertex(add(p,mul(face,-.35*math.sin(math.pi*t))),((q+1)/2,t)))
            top.append(top_row); bottom.append(bottom_row)
        for i in range(len(stations)-1):
            for j in range(4):
                self.quad(top[i][j],top[i+1][j],top[i+1][j+1],top[i][j+1])
                self.quad(bottom[i][j],bottom[i][j+1],bottom[i+1][j+1],bottom[i+1][j])
            self.quad(top[i][0],bottom[i][0],bottom[i+1][0],top[i+1][0])
            self.quad(top[i][4],top[i+1][4],bottom[i+1][4],bottom[i][4])

    def face_plate(self, polygon, front_x, depth=4, ridge=2):
        """Carved mask/bark plate; polygon is its Y/Z silhouette."""
        # Positive Y/Z winding has a +X normal.
        area = sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(polygon,polygon[1:]+polygon[:1]))
        if area < 0: polygon = list(reversed(polygon))
        center = (front_x+ridge,sum(p[0] for p in polygon)/len(polygon),sum(p[1] for p in polygon)/len(polygon))
        back_center = (front_x-depth,center[1],center[2])
        front = [self.vertex((front_x,y,z),(y/100+.5,z/200+.5)) for y,z in polygon]
        back = [self.vertex((front_x-depth,y,z),(y/100+.5,z/200+.5)) for y,z in polygon]
        c, d = self.vertex(center),self.vertex(back_center)
        for i in range(len(front)):
            j=(i+1)%len(front)
            self.triangle(c,front[i],front[j]); self.triangle(d,back[j],back[i])
            self.quad(front[i],back[i],back[j],front[j])

    def write(self, directory):
        normals = [(0.,0.,0.) for _ in self.vertices]
        for a,b,c in self.faces:
            n=cross(sub(self.vertices[b],self.vertices[a]),sub(self.vertices[c],self.vertices[a]))
            for i in (a,b,c): normals[i]=add(normals[i],n)
        path=directory/(self.name+'.obj')
        with path.open('w',encoding='ascii',newline='\n') as stream:
            stream.write('# Fablewood sculpted guardian | cm | Z up | X forward\n')
            stream.write('o '+self.name+'\nusemtl FablePaint\ns 1\n')
            for p in self.vertices: stream.write('v %.6f %.6f %.6f\n'%p)
            for uv in self.uvs: stream.write('vt %.6f %.6f\n'%uv)
            for n in normals: stream.write('vn %.6f %.6f %.6f\n'%unit(n))
            for face in self.faces:
                stream.write('f '+' '.join(f'{i+1}/{i+1}/{i+1}' for i in face)+'\n')
        assert all(math.isfinite(n) for p in self.vertices for n in p)
        assert self.faces and all(max(f)<len(self.vertices) for f in self.faces)
        return {'asset':self.name,'vertices':len(self.vertices),'triangles':len(self.faces),
                'bounds_cm':[[round(min(p[a] for p in self.vertices),2) for a in range(3)],
                             [round(max(p[a] for p in self.vertices),2) for a in range(3)]]}


def build_guardian():
    rng=random.Random(214)
    meshes={}
    def part(suffix):
        m=Mesh('SM_Guardian'+suffix); meshes[suffix]=m; return m
    body,bark,face,eyes,crown,tips=(part(n) for n in ('Body','Bark','Face','Eyes','Crown','CrownTips'))
    # Bent chest and pelvis, with a narrower root waist and a broad shoulder arch.
    body.sweep([(0,0,-52,18,23),(-8,1,-34,29,27),(-7,-2,-5,37,31),(-9,0,28,46,33),
                (-5,0,48,35,28),(3,0,62,22,20),(6,0,87,19,22),(1,0,100,13,17)],20,4,.13,.6)
    # Ropes of wood growing diagonally across the torso.
    for sign in (-1,1):
        for i in range(5):
            z=-39+i*15
            body.sweep([(24,sign*6,z-8,3.6,4.2),(32,sign*18,z+1,4,5),
                        (24,sign*35,z+20,3.5,4),(-1,sign*45,z+27,1.2,2)],8,3,.16,.5)
            bark.leaf((31,sign*5,z-9),(29,sign*(27+i*.6),z+23),6.0,(1,0,0),3.0)
        # Tall, asymmetrical antlers grow from the sides of the mask.
        body.sweep([(0,sign*19,88,8,9),(-4,sign*32,104,6.3,7),(-8,sign*43,123,4,4.5),
                    (1,sign*48,142+sign*6,1.2,1.5)],12,4,.2,.8)
        body.sweep([(-6,sign*33,107,4,4),(4,sign*47,114,3,3),(8,sign*61,133,0.8,1)],9,3,.17,.6)
        body.sweep([(-7,sign*42,123,3,3),(-19,sign*49,136,1.5,2),(-16,sign*57,144,0.5,.6)],8,3,.1,.8)
        # Spinal bark strips continue onto the back, so the guardian reads in 360 degrees.
        for i in range(5):
            theta=math.pi*.55+i*.19
            start=(-27-4*math.sin(i),sign*(12+i*4),-30+i*6)
            end=(-27,sign*(15+i*5),30+i*4)
            bark.leaf(start,end,7,(-1,sign*.3,0),3)
    # A dark recessed mask backing remains visible between the separate raised plates.
    face.face_plate([(-5,101),(-19,94),(-27,82),(-24,63),(-12,48),(0,40),
                     (12,48),(24,63),(27,82),(19,94),(5,101)],37,5,1)
    bark.face_plate([(-5,101),(-8,90),(-5,78),(-7,64),(0,47),(7,64),(5,78),(8,90),(5,101)],43,4,4)
    for sign in (-1,1):
        def poly(points): return [(sign*y,z) for y,z in points]
        bark.face_plate(poly([(7,90),(15,100),(25,91),(27,84),(16,84),(9,80)]),42,5,3)
        bark.face_plate(poly([(10,68),(24,77),(24,63),(14,49),(7,47)]),41,5,4)
        bark.face_plate(poly([(4,53),(11,52),(8,39),(3,43)]),42,4,1)
        eyes.face_plate(poly([(7,78),(15,80),(24,83),(19,73),(12,71)]),44.6,1.2,1.8)
        # Broad overhanging moss and pointed leaves form a crown, not a spherical hat.
        for i in range(34):
            t=rng.random()
            base=(-14+rng.uniform(-13,13),sign*rng.uniform(19,48),rng.uniform(21,64))
            tip=add(base,(rng.uniform(-9,15),sign*rng.uniform(7,18),rng.uniform(-13,14)))
            (crown if i%4 else tips).leaf(base,tip,rng.uniform(3.5,7.5),(.35,sign*.5,.85),rng.uniform(1,2.4))
        for i in range(18):
            z=rng.uniform(98,136)
            y=sign*(26+(z-98)*.55)
            base=(-7,y,z)
            tip=add(base,(rng.uniform(-10,15),sign*rng.uniform(7,17),rng.uniform(0,13)))
            (tips if i%3==0 else crown).leaf(base,tip,rng.uniform(3,6),(.6,sign*.3,.8),1.4)
        # Two articulated limbs retain a shoulder-local origin.
        side='L' if sign<0 else 'R'
        arm,arm_bark=part('Arm'+side),part('ArmBark'+side)
        arm.sweep([(0,0,0,16,15),(-7,sign*18,-5,18,14),(-2,sign*32,-22,15,13),
                   (9,sign*37,-43,11,12),(22,sign*42,-61,14,11),(26,sign*44,-69,10,8)],16,4,.2,1.1)
        # Intertwined long fibres and splayed, bent branch fingers.
        for i in range(4):
            arm.sweep([(5+i*2,sign*(i*5-6),2,3.5,4),
                       (11+i,sign*(26+i*2),-21,3.3,3.5),
                       (17+i,sign*(33+i*3),-46,2,2.7),(28,sign*(39+i*3),-66,.8,1.2)],8,4,.1,.4)
            arm.sweep([(23,sign*(34+i*6),-60,4.8,5),(39+i*3,sign*(36+i*7),-68,3.5,3.8),
                       (47+i*3,sign*(33+i*8),-80+abs(i-1.5)*2,.8,1.3)],10,3,.15,.5)
            arm_bark.leaf((11,sign*(i*7-8),2),(16,sign*(21+i*5),-30),6,(1,0,.2),3)
            arm_bark.leaf((18,sign*(27+i*4),-28),(29,sign*(35+i*6),-60),4.5,(1,0,.1),2.4)
        arm.sweep([(-10,sign*20,-8,5,5),(-21,sign*31,2,3,3),(-20,sign*40,15,.7,1)],10,3,.15,.7)
        # Root thighs fork into three broad planted toes and a rear anchoring root.
        leg,leg_bark=part('Leg'+side),part('LegBark'+side)
        leg.sweep([(0,0,0,13,17),(-7,sign*6,-15,16,17),(1,sign*11,-30,14,15),
                   (13,sign*13,-43,12,13),(21,sign*14,-49,9,10)],16,4,.16,.8)
        for i in range(3):
            leg.sweep([(10,sign*(i*7+5),-35,7,8),(25,sign*(i*9+4),-47,7,5),
                       (43+i*4,sign*(i*11+1),-54,4,2),(51+i*4,sign*(i*12),-55,.5,.5)],10,4,.13,.4)
            leg_bark.leaf((11,sign*(i*6-3),-1),(19,sign*(i*7+5),-37),5.2,(1,0,.1),2.5)
        leg.sweep([(-8,sign*10,-29,7,6),(-24,sign*15,-45,5,4),(-34,sign*26,-55,1,1)],10,3,.15,.7)
    return list(meshes.values())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,default=Path(__file__).resolve().parents[2]/'ArtSource'/'Meshes')
    args=parser.parse_args()
    directory=args.output_dir.resolve(); directory.mkdir(parents=True,exist_ok=True)
    results=[mesh.write(directory) for mesh in build_guardian()]
    print(json.dumps({'output':str(directory),'units':'cm','up':'Z','forward':'X',
                      'assembly_cm':ASSEMBLY,'meshes':results,'total_triangles':sum(m['triangles'] for m in results)},indent=2))


if __name__=='__main__': main()
