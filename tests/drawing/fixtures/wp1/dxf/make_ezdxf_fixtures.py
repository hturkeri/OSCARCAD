# Generates the WP-1 DXF reference fixtures with ezdxf (MIT; developer tool only, never linked).
# Usage: python3 make_ezdxf_fixtures.py <outdir>      -> ezdxf_r2010.dxf, ezdxf_r12.dxf, expected.json
# Content (model space, mm): 80x40 plate outline as LWPOLYLINE with one bulged corner, Ø20 hole, R5 arc, CENTER /
# HIDDEN lines, ELLIPSE, SPLINE, TEXT with %%c/%%p/%%d, MTEXT with a stacked tolerance, INSERT of a block (rotated,
# scaled, 2x1 array), and DIMENSIONs (linear, aligned, diameter, radius, angular) rendered into *D blocks.
import json, math, sys, ezdxf
out = sys.argv[1] if len(sys.argv) > 1 else '.'

def build(ver):
    doc = ezdxf.new(ver, setup=True)
    doc.header['$INSUNITS'] = 4
    msp = doc.modelspace()
    doc.layers.add('VISIBLE', color=7, lineweight=50)
    doc.layers.add('CENTER', color=1, linetype='CENTER', lineweight=25)
    doc.layers.add('HIDDEN', color=5, linetype='HIDDEN', lineweight=25)
    doc.layers.add('DIM', color=3, lineweight=18)
    if ver == 'R12':
        # R12: no LWPOLYLINE -> POLYLINE with bulge
        msp.add_polyline2d([(0, 0, 0, 0, 0), (80, 0, 0, 0, 0), (80, 30, 0, 0, 0.41421356237), (70, 40, 0, 0, 0), (0, 40, 0, 0, 0)],
                           format='xyseb', close=True,
                           dxfattribs={'layer': 'VISIBLE'})
    else:
        msp.add_lwpolyline([(0, 0, 0, 0, 0), (80, 0, 0, 0, 0), (80, 30, 0, 0, 0.41421356237), (70, 40, 0, 0, 0), (0, 40, 0, 0, 0)],
                           format='xyseb', close=True, dxfattribs={'layer': 'VISIBLE'})
    msp.add_circle((40, 20), 10, dxfattribs={'layer': 'VISIBLE'})
    msp.add_arc((0, 20), 5, 270, 90, dxfattribs={'layer': 'VISIBLE'})
    msp.add_line((40, -5), (40, 45), dxfattribs={'layer': 'CENTER'})
    msp.add_line((-5, 20), (85, 20), dxfattribs={'layer': 'CENTER'})
    msp.add_line((20, 0), (20, 40), dxfattribs={'layer': 'HIDDEN'})
    if ver != 'R12':
        msp.add_ellipse((120, 20), major_axis=(15, 0), ratio=0.5, dxfattribs={'layer': 'VISIBLE'})
        msp.add_spline([(100, 50), (110, 60), (120, 50), (130, 60)], dxfattribs={'layer': 'VISIBLE'})
        msp.add_mtext('Ø16\\S+0^-0.5;', dxfattribs={'char_height': 3.5, 'insert': (100, 80), 'attachment_point': 7, 'layer': 'DIM'})
    msp.add_text('%%c20 H7', dxfattribs={'height': 3.5, 'insert': (100, 0), 'layer': 'DIM'})
    msp.add_text('14%%p0.2', dxfattribs={'height': 3.5, 'insert': (100, 10), 'layer': 'DIM'})
    msp.add_text('45%%d', dxfattribs={'height': 3.5, 'insert': (100, 30), 'rotation': 90, 'layer': 'DIM'})
    msp.add_text('ÖLÇEK 1:1', dxfattribs={'height': 3.5, 'insert': (150, 0)})
    blk = doc.blocks.new('BOLT')
    blk.add_circle((0, 0), 3)
    blk.add_line((-4, 0), (4, 0))
    msp.add_blockref('BOLT', (60, 70), dxfattribs={'xscale': 2, 'yscale': 2, 'rotation': 90, 'layer': 'VISIBLE'})
    ins = msp.add_blockref('BOLT', (10, 70), dxfattribs={'layer': 'VISIBLE'})
    ins.dxf.column_count = 2; ins.dxf.column_spacing = 15
    msp.add_linear_dim(base=(0, -10), p1=(0, 0), p2=(80, 0), dimstyle='EZDXF', dxfattribs={'layer': 'DIM'}).render()
    msp.add_linear_dim(base=(-10, 0), p1=(0, 0), p2=(0, 40), angle=90, dimstyle='EZDXF', dxfattribs={'layer': 'DIM'}).render()
    msp.add_aligned_dim(p1=(80, 30), p2=(70, 40), distance=5, dimstyle='EZDXF', dxfattribs={'layer': 'DIM'}).render()
    msp.add_diameter_dim(center=(40, 20), radius=10, angle=45, dimstyle='EZ_RADIUS', dxfattribs={'layer': 'DIM'}).render()
    msp.add_radius_dim(center=(0, 20), radius=5, angle=180, dimstyle='EZ_RADIUS', dxfattribs={'layer': 'DIM'}).render()
    if ver != 'R12':
        msp.add_angular_dim_2l(base=(60, 60), line1=((40, 20), (60, 20)), line2=((40, 20), (60, 40)), dimstyle='EZ_CURVED',
                               dxfattribs={'layer': 'DIM'}).render()
    return doc

for ver, name in (('R2010', 'ezdxf_r2010.dxf'), ('R12', 'ezdxf_r12.dxf')):
    build(ver).saveas(f'{out}/{name}')
json.dump({'linear': [80, 40], 'aligned': math.hypot(10, 10), 'diameter': 20, 'radius': 5, 'angle': 45}, open(f'{out}/expected.json', 'w'))
