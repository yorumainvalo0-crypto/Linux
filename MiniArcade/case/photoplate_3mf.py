#!/usr/bin/env python3
"""Packs the photo plate STLs into one 3MF: plate and ruler as objects with
two parts (base + marks), stop and pins as plain objects, laid out on one bed."""
import struct, sys, zipfile, os

def read_stl(path):
    data = open(path, 'rb').read()
    n = struct.unpack_from('<I', data, 80)[0]
    verts, index, tris = [], {}, []
    for i in range(n):
        off = 84 + i * 50 + 12
        tri = []
        for k in range(3):
            v = struct.unpack_from('<3f', data, off + k * 12)
            key = (round(v[0], 5), round(v[1], 5), round(v[2], 5))
            if key not in index:
                index[key] = len(verts)
                verts.append(key)
            tri.append(index[key])
        if len(set(tri)) == 3:
            tris.append(tri)
    return verts, tris

def bbox(verts):
    xs, ys, zs = zip(*verts)
    return min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)

def mesh_xml(oid, name, verts, tris, pindex):
    out = [f'<object id="{oid}" name="{name}" type="model" pid="1" pindex="{pindex}"><mesh><vertices>']
    out += [f'<vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in verts]
    out.append('</vertices><triangles>')
    out += [f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris]
    out.append('</triangles></mesh></object>')
    return ''.join(out)

d = sys.argv[1]
out = sys.argv[2]
L = lambda f: read_stl(os.path.join(d, f))
plate_b, plate_m = L('photoplate_base.stl'), L('photoplate_marks.stl')
ruler_b, ruler_m = L('photoplate_ruler_base.stl'), L('photoplate_ruler_marks.stl')
stop, pins = L('photoplate_stop.stl'), L('photoplate_stoppin.stl')

res, items = [], []
oid = 2
def add_mesh(name, m, color):
    global oid
    res.append(mesh_xml(oid, name, m[0], m[1], color)); oid += 1
    return oid - 1
def add_group(name, parts, x, y):
    global oid
    comps = ''.join(f'<component objectid="{p}"/>' for p in parts)
    res.append(f'<object id="{oid}" name="{name}" type="model"><components>{comps}</components></object>')
    items.append(f'<item objectid="{oid}" transform="1 0 0 0 1 0 0 0 1 {x:.3f} {y:.3f} 0"/>')
    oid += 1

# layout on the bed (mm): plate at the left, the rest to its right
pb = bbox(plate_b[0]); rb = bbox(ruler_b[0]); sb = bbox(stop[0]); qb = bbox(pins[0])
x0, y0 = 20, 20
add_group('Fotoplatte', [add_mesh('Platte Basis', plate_b, 0), add_mesh('Platte Markierungen', plate_m, 1)], x0 - pb[0], y0 - pb[1])
rx = x0 + (pb[3] - pb[0]) + 10
for i in range(2):     # two height rulers
    add_group(f'Hoehenlineal {i+1}', [add_mesh('Lineal Basis', ruler_b, 0), add_mesh('Lineal Markierungen', ruler_m, 1)],
              rx - rb[0], y0 + i * 18 - rb[1])
add_group('Eckanschlag', [add_mesh('Anschlag', stop, 0)], rx - sb[0], y0 + 45 - sb[1])
add_group('Stifte Anschlag', [add_mesh('Stifte', pins, 0)], rx + 40 - qb[0], y0 + 45 - qb[1])

model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
         '<model unit="millimeter" xml:lang="de-DE" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
         '<metadata name="Title">MiniArcade Fotoplatte</metadata>'
         '<resources><basematerials id="1"><base name="weiss" displaycolor="#F2F2F2FF"/><base name="schwarz" displaycolor="#111111FF"/></basematerials>'
         + ''.join(res) + '</resources><build>' + ''.join(items) + '</build></model>')
ct = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
      '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml', ct)
    z.writestr('_rels/.rels', rels)
    z.writestr('3D/3dmodel.model', model)
print(out, os.path.getsize(out), 'bytes;', oid - 2, 'objects')
