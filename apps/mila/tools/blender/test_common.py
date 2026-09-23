"""Smoke test of ml_common: a floor tile and a wall block, colour + z + shadow."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ml_common as C
a = C.args()
C.reset('living')
C.mat('wood', base=(0.78, 0.52, 0.30), rough=0.6)
C.mat('wall', base=(0.30, 0.55, 0.58), rough=0.8)
t = C.box('tile', 0, 0, -0.05, 1, 1, 0, 'wood', bevel=0.01)
C.render_sprite(a.out, 'test_floor', [t], C.cell(0, 0, 0), passes=('color', 'z'), kind='tile')
w = C.box('wall', 0, 0, 0, 1, 1, C.FLOOR_M, 'wall', bevel=0.01)
C.render_sprite(a.out, 'test_wall', [w], C.cell(0, 0, 0), passes=('color', 'z', 'shadow'), shadow_z=0.0, kind='prop')
C.save_meta(a.out)
