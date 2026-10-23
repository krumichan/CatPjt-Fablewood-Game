#!/usr/bin/env python3
"""Build the Fablewood ranger's authored static parts without a DCC dependency.

All vertices are centimeters in Unreal coordinates: +X face, +Y right, +Z up.
Body pieces use the capsule origin; limb pieces use shoulder/elbow/hip/knee
pivots documented in PARTS. Import WITHOUT baking an axis or unit conversion.
Each OBJ contains one material slot, explicit smooth/facet normals and UVs.
Geometry is deterministic and contains no external assets or Python packages.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path

PI = math.pi


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, s):
    return tuple(x * s for x in a)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def unit(a):
    length = math.sqrt(sum(x * x for x in a))
    return mul(a, 1.0 / length) if length > 1e-12 else (0.0, 0.0, 1.0)


def lerp(a, b, t):
    return add(mul(a, 1.0 - t), mul(b, t))


class Mesh:
    def __init__(self, name, pivot="capsule origin"):
        self.name, self.pivot = name, pivot
        self.vertices, self.uvs, self.faces = [], [], []

    def vertex(self, p, uv=(0.0, 0.0)):
        self.vertices.append(p)
        self.uvs.append(uv)
        return len(self.vertices) - 1

    def tri(self, a, b, c, flat=False):
        if len({a, b, c}) == 3:
            normal = cross(sub(self.vertices[b], self.vertices[a]),
                           sub(self.vertices[c], self.vertices[a]))
            if sum(n * n for n in normal) > 1e-12:
                self.faces.append((a, b, c, flat))

    def quad(self, a, b, c, d, flat=False):
        self.tri(a, b, c, flat)
        self.tri(a, c, d, flat)

    def rings(self, rings, close_ends=True, flat=False):
        """Join CCW rings ordered from bottom to top."""
        ids = []
        for row, ring in enumerate(rings):
            ids.append([self.vertex(p, (j / (len(ring) - 1), row / (len(rings) - 1)))
                        for j, p in enumerate(ring)])
        for row in range(len(ids) - 1):
            for j in range(len(ids[0]) - 1):
                self.quad(ids[row][j], ids[row][j + 1],
                          ids[row + 1][j + 1], ids[row + 1][j], flat)
        if close_ends:
            for row, reverse in ((0, True), (-1, False)):
                ring = rings[row][:-1]
                center = tuple(sum(p[k] for p in ring) / len(ring) for k in range(3))
                ci = self.vertex(center, (0.5, 0.5))
                for j in range(len(ids[row]) - 1):
                    a, b = ids[row][j], ids[row][j + 1]
                    self.tri(ci, b, a, flat) if reverse else self.tri(ci, a, b, flat)

    def loft(self, sections, segments=24, folds=0.0, fold_count=9, flat=False):
        """Profiles are (z, x-center, x-radius, y-radius[, y-center])."""
        rings = []
        for row, section in enumerate(sections):
            z, cx, rx, ry, *cy = section
            ring = []
            for j in range(segments + 1):
                theta = 2 * PI * j / segments
                ripple = 1.0 + folds * math.cos(fold_count * theta + row * 0.37)
                ring.append((cx + rx * math.cos(theta) * ripple,
                             (cy[0] if cy else 0.0) + ry * math.sin(theta) * ripple, z))
            rings.append(ring)
        self.rings(rings, flat=flat)

    def ellipsoid(self, center, radius, segments=20, rows=10):
        sections = []
        for row in range(rows + 1):
            angle = -PI / 2 + PI * (row + 0.025) / (rows + 0.05)
            sections.append((center[2] + radius[2] * math.sin(angle), center[0],
                             radius[0] * math.cos(angle), radius[1] * math.cos(angle), center[1]))
        self.loft(sections, segments)

    def tube(self, points, radii, segments=10, flatten=1.0, flat=False, smooth=0):
        if smooth and len(points) > 2:
            path, widths = [], []
            original_radii = radii if isinstance(radii, (list, tuple)) else [radii] * len(points)
            for i in range(len(points) - 1):
                p0, p1 = points[max(0, i - 1)], points[i]
                p2, p3 = points[i + 1], points[min(len(points) - 1, i + 2)]
                for j in range(smooth):
                    t = j / smooth
                    path.append(tuple(.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t
                        + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t * t
                        + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t * t * t)
                        for k in range(3)))
                    widths.append(original_radii[i] * (1 - t) + original_radii[i + 1] * t)
            points, radii = path + [points[-1]], widths + [original_radii[-1]]
        # Parallel transport prevents a 90-degree frame kink as a hair lock bends.
        rings, previous_u = [], None
        for i, p in enumerate(points):
            tangent = unit(sub(points[min(i + 1, len(points) - 1)], points[max(0, i - 1)]))
            ref = (0.0, 0.0, 1.0) if abs(tangent[2]) < 0.9 else (0.0, 1.0, 0.0)
            if previous_u is None:
                u = unit(cross(ref, tangent))
            else:
                projection = sub(previous_u, mul(tangent, sum(a * b for a, b in zip(previous_u, tangent))))
                u = unit(projection) if sum(v * v for v in projection) > 1e-8 else unit(cross(ref, tangent))
            previous_u = u
            v = cross(tangent, u)
            radius = radii[i] if isinstance(radii, (list, tuple)) else radii
            ring = [add(p, add(mul(u, math.cos(j * 2 * PI / segments) * radius),
                                mul(v, math.sin(j * 2 * PI / segments) * radius * flatten)))
                    for j in range(segments + 1)]
            rings.append(ring)
        # rings() winding assumes their normal points toward the next ring.
        self.rings(rings, flat=flat)

    def save(self, folder):
        if not self.faces:
            raise ValueError(self.name + ": empty mesh")
        sums = [(0.0, 0.0, 0.0)] * len(self.vertices)
        face_normals = []
        for a, b, c, flat in self.faces:
            area_normal = cross(sub(self.vertices[b], self.vertices[a]),
                                sub(self.vertices[c], self.vertices[a]))
            face_normals.append(unit(area_normal))
            if not flat:
                for index in (a, b, c):
                    sums[index] = add(sums[index], area_normal)
        # Weld only normals at coincident UV seam vertices, retaining UV seams.
        welded = {}
        for p, n in zip(self.vertices, sums):
            key = tuple(round(v, 5) for v in p)
            welded[key] = add(welded.get(key, (0.0, 0.0, 0.0)), n)
        normals = [unit(welded[tuple(round(v, 5) for v in p)]) for p in self.vertices]
        normals.extend(face_normals)
        lines = [
            "# Fablewood ranger | generated by Tools/Art/create_character_meshes.py",
            "# centimeters; +X forward, +Y right, +Z up; scale 1",
            "# pivot: " + self.pivot, "o " + self.name,
        ]
        lines.extend("v %.6f %.6f %.6f" % p for p in self.vertices)
        lines.extend("vt %.6f %.6f" % uv for uv in self.uvs)
        lines.extend("vn %.6f %.6f %.6f" % n for n in normals)
        lines.extend(("usemtl FablePaint", "s 1"))
        for index, (a, b, c, flat) in enumerate(self.faces):
            normal_index = len(self.vertices) + index + 1
            refs = ["%d/%d/%d" % (v + 1, v + 1, normal_index if flat else v + 1)
                    for v in (a, b, c)]
            lines.append("f " + " ".join(refs))
        path = folder / (self.name + ".obj")
        path.write_text("\n".join(lines) + "\n", encoding="ascii")
        bounds = [(round(min(p[k] for p in self.vertices), 2),
                   round(max(p[k] for p in self.vertices), 2)) for k in range(3)]
        # Geometry validity, independent of Unreal's importer.
        assert all(math.isfinite(c) for p in self.vertices + normals for c in p)
        assert all(abs(sum(c * c for c in n) - 1.0) < 1e-5 for n in normals)
        print(f"{self.name:25} {len(self.vertices):5} vertices {len(self.faces):5} triangles | {bounds}")
        return len(self.faces)


PARTS = {}


def part(name, pivot="capsule origin"):
    mesh = Mesh("SM_Hero" + name, pivot)
    PARTS[name] = mesh
    return mesh


def garment_panel(mesh, sections, gaps, segments=32):
    """An open, tailored vest: upper front widens into an actual V neckline."""
    ids, boundaries = [], [[], []]
    for row, ((z, cx, rx, ry), gap) in enumerate(zip(sections, gaps)):
        ring = []
        for j in range(segments + 1):
            theta = gap + (2 * PI - 2 * gap) * j / segments
            p = (cx + rx * math.cos(theta), ry * math.sin(theta), z)
            ring.append(mesh.vertex(p, (j / segments, row / (len(sections) - 1))))
            if j == 0:
                boundaries[0].append(p)
            if j == segments:
                boundaries[1].append(p)
        ids.append(ring)
    for row in range(len(ids) - 1):
        for j in range(segments):
            mesh.quad(ids[row][j], ids[row][j + 1], ids[row + 1][j + 1], ids[row + 1][j])
    # Solid, thin leather has a reverse surface, so the open neck is visible from above.
    back = []
    for row in ids:
        back.append([mesh.vertex(add(mesh.vertices[v], (-0.32, 0, 0)), mesh.uvs[v]) for v in row])
    for row in range(len(back) - 1):
        for j in range(segments):
            mesh.quad(back[row][j + 1], back[row][j], back[row + 1][j], back[row + 1][j + 1])
    for j in range(segments):
        mesh.quad(ids[0][j + 1], ids[0][j], back[0][j], back[0][j + 1])
        mesh.quad(ids[-1][j], ids[-1][j + 1], back[-1][j + 1], back[-1][j])
    for row in range(len(ids) - 1):
        mesh.quad(ids[row][0], ids[row + 1][0], back[row + 1][0], back[row][0])
        mesh.quad(ids[row + 1][-1], ids[row][-1], back[row][-1], back[row + 1][-1])
    return boundaries


def cloth(mesh, surface, columns=24, rows=24, thickness=0.5):
    """Closed, folded fabric sheet; surface is parameterized across, down."""
    front, back = [], []
    for row in range(rows + 1):
        v = row / rows
        front.append([])
        back.append([])
        for col in range(columns + 1):
            u = col / columns
            p = surface(u, v)
            front[-1].append(mesh.vertex(add(p, (-thickness / 2, 0, 0)), (u, v)))
            back[-1].append(mesh.vertex(add(p, (thickness / 2, 0, 0)), (u, v)))
    for row in range(rows):
        for col in range(columns):
            mesh.quad(front[row][col], front[row][col + 1],
                      front[row + 1][col + 1], front[row + 1][col])
            mesh.quad(back[row][col + 1], back[row][col],
                      back[row + 1][col], back[row + 1][col + 1])
    for row in range(rows):
        mesh.quad(front[row + 1][0], front[row][0], back[row][0], back[row + 1][0])
        mesh.quad(front[row][-1], front[row + 1][-1], back[row + 1][-1], back[row][-1])
    for col in range(columns):
        mesh.quad(front[0][col], front[0][col + 1], back[0][col + 1], back[0][col])
        mesh.quad(front[-1][col + 1], front[-1][col], back[-1][col], back[-1][col + 1])


def build_body():
    shirt = part("Shirt")
    shirt.loft([(-14, 0, 10, 15), (-8, 0, 11, 16), (0, 0, 10.5, 16),
                (10, -0.5, 10.2, 16.5), (22, -1, 11.8, 19), (33, -1, 11.5, 21),
                (42, -1.5, 9.8, 22), (47, -1, 7.5, 14), (51, 0, 6.0, 7.1)],
               36, folds=0.025)
    # Turned collar arcs frame the neck without the former conical hat silhouette.
    for side in (-1, 1):
        shirt.tube([(7.2, side * 2.2, 45), (6.3, side * 5.5, 49.5),
                    (2, side * 7, 52), (-4.5, side * 4.6, 51.5)],
                   [1.3, 1.8, 1.5, 0.7], 10, flatten=0.6)

    vest = part("Vest")
    profiles = [(-10, 0, 11.95, 17.0), (-2, 0, 11.65, 17.0), (11, -.5, 12.2, 18.3),
                (24, -1, 13.4, 20.6), (35, -1.2, 12.8, 22), (43, -1.5, 10.6, 20.4)]
    edges = garment_panel(vest, profiles, [0.10, 0.10, 0.13, 0.18, .34, .84])
    trim = part("VestSeams")
    for edge in edges:
        trim.tube([add(p, (.25, 0, 0)) for p in edge], .48, 8)
    for side in (-1, 1):
        trim.tube([(9.4, side * 12, -8), (9.3, side * 13, 4),
                   (9.8, side * 14, 19), (8.5, side * 16, 33)], .3, 7)
    # Hem is fitted and curved, not a rectangular block.
    trim.tube([(11.85 * math.cos(t), 16.95 * math.sin(t), -9.2)
               for t in [2 * PI * i / 48 for i in range(49)]], .48, 8)
    laces = part("Laces")
    for z in (1, 6, 11, 16, 21):
        width = 2.4 + .055 * z
        laces.tube([(12, -width, z), (12.7, 0, z + 1.9),
                    (12, width, z + 3.2)], .29, 6)
    belt = part("Belt")
    belt.loft([(-13, 0, 12, 17.4), (-12.4, 0, 12.6, 18),
               (-6.1, 0, 12.6, 18), (-5.5, 0, 12, 17.4)], 40)
    belt.tube([(13, 4, -9), (13.8, 9, -13), (13.1, 11, -21)],
              [1.7, 1.65, 1.5], 8, flatten=.3)
    fittings = part("Fittings")
    fittings.tube([(13.8, -3.7, -12.5), (14.1, 3.7, -12.5), (14.1, 4, -6.2),
                   (13.8, -3.7, -6.2), (13.8, -3.7, -12.5)], .65, 8)
    fittings.tube([(14.5, 0, -9.2), (14.6, 3.5, -9.2)], .35, 8)
    for theta in (.5, .85, 1.2, 1.6, 2.2, 2.8, 3.5, 4.1, 4.7, 5.1, 5.5, 5.85):
        fittings.ellipsoid((12.8 * math.cos(theta), 18.2 * math.sin(theta), -9),
                           (.65, .65, .8), 8, 4)
    for side in (-1, 1):
        fittings.ellipsoid((5, side * 10, 47.5), (1.8, 1.8, 1.1), 12, 6)

    hips = part("Hips")
    hips.loft([(-21, -.5, 8, 14), (-14, -.5, 10.2, 17), (-6, 0, 10.4, 16)], 28, .02)
    sash = part("WaistCloth")
    for direction in (-1, 1):
        def panel(u, v, d=direction):
            t = 2 * u - 1
            return (d * (12 + 2 * math.sin(v * PI) + 1.2 * math.cos(u * PI * 6) * v),
                    t * (14.8 - 3 * v), -12 - 34 * v + 4 * abs(t) * v)
        cloth(sash, panel, 16, 14, .4)
    pouch = part("Pouch")
    pouch.loft([(-27, -1.5, 3.5, 5.2, -21.5), (-25, -1.5, 5, 6, -21.5),
                (-12, -2, 5.4, 6.4, -21.5), (-8, -2, 4.7, 5.7, -21.5)], 24)
    # Rolled flap, seam and brass closure are separate sculpted surfaces.
    trim.tube([(2.4, -27, -15), (3.7, -25, -18), (4.2, -21.5, -19),
               (3.7, -18, -18), (2.4, -16, -15)], .45, 8)
    fittings.ellipsoid((4, -21.5, -18), (.6, 1.1, 1.3), 10, 6)


def build_head():
    skin = part("Head")
    skin.loft([(49, 0, 4.8, 5.2), (51.5, 0, 5, 5.4), (56, .3, 5, 5.5),
               (59, 1, 5.5, 5.7)], 28)
    neck_vertices = len(skin.vertices)
    skin.loft([(62.8, 2.1, 3.5, 4), (64.2, 2, 5.7, 6), (66.5, 1.5, 7, 7.4),
               (70, .4, 8.9, 8.6), (74.5, -.2, 9.7, 9.4), (78, -.7, 10, 9.5),
               (82, -1, 9.8, 9.2), (86, -1.5, 8.7, 8.2), (88.4, -1.5, 6.3, 6.4),
               (89.5, -1.5, 2.8, 3.4)], 36)
    skin.loft([(71.2, 9.0, 1.2, 1.4), (72.1, 10.6, 2.5, 1.8),
               (73.1, 11.1, 2.3, 1.65), (75.3, 10.2, 1.3, 1.2),
               (78, 9.4, .65, .9)], 16)
    for side in (-1, 1):
        skin.ellipsoid((-1.4, side * 9.4, 75), (2.5, 1.9, 4.4), 16, 10)
        skin.tube([(-.6, side * 10.8, 71.6), (1, side * 11, 73),
                   (1, side * 11, 76.5), (-.3, side * 10.9, 78.5)],
                  [.38, .55, .52, .3], 8)
    eye_white = part("EyeWhites")
    eye_dark = part("Eyes")
    lips = part("Lips")
    for side in (-1, 1):
        eye_white.ellipsoid((8.95, side * 4.5, 77), (.7, 2.15, 1), 16, 8)
        eye_dark.ellipsoid((9.57, side * 4.5, 77), (.28, .8, .84), 12, 8)
    lips.tube([(9.8, -2.7, 69.5), (10.1, -1, 69.9), (10.3, 0, 69.6),
               (10.1, 1, 69.9), (9.8, 2.7, 69.5)], .35, 8, flatten=.65)

    hair = part("Hair")
    rings = []
    for row in range(9):
        t = row / 8
        ring = []
        for j in range(41):
            theta = j * 2 * PI / 40
            # The fringe is higher in front; the back follows the nape.
            hem = 77.5 + 4.2 * math.cos(theta) + .8 * math.sin(theta * 3)
            latitude = t * PI / 2 * .985
            radial = math.cos(latitude)
            ring.append((-1.5 + 10.7 * math.cos(theta) * radial,
                         10.3 * math.sin(theta) * radial,
                         hem + (91.3 - hem) * math.sin(latitude)))
        rings.append(ring)
    hair.rings(rings, close_ends=True)
    # Overlapping swept locks, each with a broad root and an asymmetrical pointed tip.
    for i in range(16):
        theta = i * 2 * PI / 16
        forward = math.cos(theta)
        root = (-1.5 + 7.1 * math.cos(theta), 7 * math.sin(theta), 88.3)
        mid = (-1 + 10.6 * math.cos(theta + .12), 10.1 * math.sin(theta + .12), 84)
        end = (-1.3 + 10.9 * math.cos(theta + .27), 10.8 * math.sin(theta + .27),
               76.6 + forward * 4.5 + 1.2 * math.sin(i * 1.8))
        tip = add(end, (.1, .8 * math.cos(theta), -2.7))
        hair.tube([root, lerp(root, mid, .55), mid, end, tip],
                  [1.7, 2.7, 2.9, 1.7, .05], 10, flatten=.55, smooth=4)
    # Five thick crown sweeps produce a soft, broken silhouette instead of a helmet.
    for i in range(5):
        y = (i - 2) * 3.1
        dome = -.065 * y * y + .65 * math.sin(i * 1.7)
        hair.tube([(-8.6, y - .7, 85.3 + dome), (-6.0, y, 89.2 + dome),
                   (-.5 + .4 * i, y + 1.2, 91.2 + dome),
                   (6.6, y + 1, 86.8 + dome), (9.4, y - .5, 80.8 + .7 * math.sin(i * 2))],
                  [1.2, 2.1, 2.35, 1.9, .08], 12, flatten=.7, smooth=5)
    for side in (-1, 1):
        hair.tube([(9.0, side * 2.8, 79.2), (9.1, side * 4.9, 79.5),
                   (8.5, side * 6.5, 79.1)], [.42, .55, .17], 8, flatten=.65)
    # Keep the collar in place while shortening the exposed neck to adult proportions.
    skin.vertices[neck_vertices:] = [add(v, (0, 0, -5)) for v in skin.vertices[neck_vertices:]]
    for mesh in (eye_white, eye_dark, lips, hair):
        mesh.vertices = [add(v, (0, 0, -5)) for v in mesh.vertices]


def build_cape():
    cape = part("Cape", "shoulder cape pivot (-10, 0, 48)")
    border = part("CapeBorder", "same as Cape; attach to Cape component")
    def surface(u, v):
        t = 2 * u - 1
        width = 17 + 18 * math.sin(v * PI * .68)
        x = -6 - 20 * v + 5 * math.sin(v * PI)
        x += (1.0 + 3.0 * v) * math.cos(u * PI * 8 + .4 * v)
        y = t * width
        z = -2 - 84 * v - 4.0 * abs(t) * (1 - v) + 4 * math.cos(t * PI) * v ** 5
        return (x, y, z)
    cloth(cape, surface, 28, 30, .8)
    for u in (0, 1):
        border.tube([surface(u, i / 30) for i in range(31)], .65, 8)
    border.tube([surface(i / 40, 1) for i in range(41)], .8, 8)
    border.tube([surface(i / 28, 0) for i in range(29)], .65, 8)
    # Shoulder collar opens at the throat, and covers the seam to the moving cape.
    collar = part("Mantle")
    for side in (-1, 1):
        collar.tube([(5.0, side * 7, 49), (-.5, side * 13, 47.5),
                     (-6.3, side * 21, 44), (-9.6, side * 24.5, 40)],
                    [2.2, 3.8, 4.5, 2.7], 14, flatten=.55)
    def shoulder_back(u, v):
        t = 2 * u - 1
        return (-5 - 12 * v + .7 * math.cos(u * PI * 4) * v,
                t * (8 + 17 * v), 51 - 10 * v - 2 * abs(t) * v)
    cloth(collar, shoulder_back, 16, 8, .65)


def build_limbs():
    sleeve = part("Sleeve", "shoulder pivot (0, +/-23, 42)")
    sleeve.loft([(-28.5, 1, 5.4, 5.8), (-25, 1, 6.1, 6.5),
                 (-19, .6, 7.5, 7.4), (-10, .1, 8.4, 8.5), (-2, -.3, 8, 8),
                 (3.5, -.5, 5.5, 5.6), (5, -.5, 2.7, 3)], 28, .04, 7)
    cuff = part("SleeveCuff", "shoulder pivot")
    cuff.loft([(-28.8, 1, 5.7, 6.1), (-28, 1, 7, 7.2), (-26, 1, 7.4, 7.6),
               (-24.3, 1, 7.2, 7.4), (-23, 1, 6.5, 6.8)], 28, .025)
    forearm = part("Forearm", "elbow pivot: shoulder + (1, 0, -28)")
    forearm.loft([(-25.8, .7, 3.3, 3.1), (-22, .4, 3.6, 3.4),
                  (-15, .1, 4.7, 4.3), (-7, 0, 5.1, 4.8), (2, 0, 5, 4.8)], 24)
    bracer = part("Bracer", "elbow pivot")
    bracer.loft([(-24, .7, 3.7, 3.5), (-22.6, .6, 4.3, 4.0),
                 (-13, .2, 5.3, 4.9), (-8.1, 0, 5.65, 5.2), (-7.4, 0, 5.3, 4.9)], 24)
    bracer_trim = part("BracerTrim", "elbow pivot")
    for z, rx, ry in ((-22, 4.35, 4.05), (-9, 5.7, 5.25)):
        bracer_trim.loft([(z - .6, .4, rx, ry), (z + .6, .4, rx, ry)], 24)
    glove = part("Glove", "elbow pivot")
    glove.loft([(-35, 1.3, 1.9, 2.3), (-33.6, 1.5, 3.1, 3.7),
                (-29, 1.3, 3.4, 3.8), (-24, .8, 3.7, 3.6),
                (-22.8, .7, 3.8, 3.5)], 24)
    glove.tube([(3.5, -2.2, -26), (5, -2.7, -28.6), (4.8, -2.4, -31.3)],
               [1.45, 1.45, .65], 12)
    thigh = part("Thigh", "hip pivot (-1, +/-10, -8)")
    thigh.loft([(-41, 1, 5.5, 5.8), (-36, 1, 5.9, 6.4), (-27, .8, 6.8, 7),
                (-18, .3, 7.7, 7.5), (-6, 0, 8.5, 8.5), (5, 0, 8.5, 8.5)], 28, .025, 7)
    shin = part("Shin", "knee pivot: hip + (1, 0, -39)")
    shin.loft([(-16, 0, 4.9, 5.3), (-8, -.3, 5.3, 5.6), (4, 0, 5.6, 5.9)], 24, .025)
    boot = part("Boot", "knee pivot; sole reaches -45, ground z=-92")
    boot.loft([(-43.2, 6.2, 14.3, 6.1), (-41, 6.2, 14.6, 6.2),
               (-38, 6.3, 14.1, 6.15), (-35, 4.2, 11.4, 5.9), (-32, 1.3, 7.1, 5.4),
               (-29, .0, 5.5, 5.4), (-25, -.4, 5.3, 5.45),
               (-22, -.3, 6, 5.9), (-20, -.3, 5.8, 5.7), (-18, -.3, 6.2, 6.1),
               (-13, -.3, 6.2, 6.1), (-8, -.3, 6.4, 6.3), (-5, -.3, 6, 6)], 32, .013)
    sole = part("BootSole", "knee pivot")
    sole.loft([(-45, 6.2, 14.2, 6.1), (-44.4, 6.2, 14.9, 6.6),
               (-42.6, 6.2, 14.9, 6.6), (-42.2, 6.2, 14.4, 6.3)], 32)
    straps = part("BootStraps", "knee pivot")
    for bottom, top, rx, ry in ((-9, -5.8, 6.8, 6.7), (-30, -27.8, 6, 5.8)):
        straps.loft([(bottom, -.3, rx, ry), (top, -.3, rx, ry)], 28)
    # Instep seam and tongue give the boots an intentional footwear silhouette.
    straps.tube([(16, 0, -36), (12, 0, -33.5), (7, 0, -31),
                 (5.5, 0, -26), (5.7, 0, -18)], .4, 8)


def build_sword():
    blade = part("SwordBlade", "right elbow pivot, held in right glove")
    # Diamond section and faceted bevels have their own explicit hard normals.
    rings = []
    for z, width, depth in [(-99, .035, .035), (-91, 1.5, .43),
                            (-81, 2.15, .62), (-43, 3.15, .8), (-35, 3.1, .8)]:
        rings.append([(3.6 + depth, 0, z), (3.6, width, z),
                      (3.6 - depth, 0, z), (3.6, -width, z), (3.6 + depth, 0, z)])
    blade.rings(rings, flat=True)
    fuller = part("SwordFuller", "right elbow pivot")
    for direction in (-1, 1):
        fuller.tube([(3.6 + direction * .68, 0, -83), (3.6 + direction * .81, 0, -71),
                     (3.6 + direction * .83, 0, -42)], [.08, .23, .23], 6, flatten=.25)
    hilt = part("SwordHilt", "right elbow pivot")
    hilt.tube([(3.6, -10.5, -31.8), (3.6, -8.5, -33.1), (3.6, -4.5, -34),
               (3.6, 0, -34.2), (3.6, 4.5, -34), (3.6, 8.5, -33.1),
               (3.6, 10.5, -31.8)], [1, 1.3, 1.4, 1.7, 1.4, 1.3, 1], 12, flatten=.8)
    hilt.ellipsoid((3.6, 0, -14.5), (2.1, 2.1, 2.9), 12, 8)
    hilt.loft([(-34, 3.6, 2, 2), (-32, 3.6, 1.9, 1.9),
               (-31.5, 3.6, 1.5, 1.5)], 12)
    grip = part("SwordGrip", "right elbow pivot")
    grip.loft([(-32, 3.6, 1.45, 1.45), (-30, 3.6, 1.7, 1.7),
               (-20, 3.6, 1.55, 1.55), (-16.7, 3.6, 1.4, 1.4)], 16)
    helix = [(3.6 + 1.65 * math.cos(i * PI / 8),
              1.65 * math.sin(i * PI / 8), -31 + i * .095) for i in range(145)]
    grip.tube(helix, .18, 6)
    leaf = part("Leaf", "skill effect; authored at full visible size")
    def surface(u, v):
        t = 2 * u - 1
        width = max(.08, math.sin(v * PI) * 3)
        return ((v - .5) * 11, t * width, .65 * (1 - t * t) * math.sin(v * PI))
    cloth(leaf, surface, 8, 12, .12)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[2] / "ArtSource" / "Meshes")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    build_body()
    build_head()
    build_cape()
    build_limbs()
    build_sword()
    total = sum(mesh.save(args.output) for mesh in PARTS.values())
    print(f"Generated {len(PARTS)} single-material meshes; {total} triangles. All finite, nondegenerate and normalised.")


if __name__ == "__main__":
    main()

