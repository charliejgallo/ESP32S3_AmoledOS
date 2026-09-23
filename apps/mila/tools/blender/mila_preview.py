#!/usr/bin/env python3
"""Mila's preview scenes through ../compose.py: python3 mila_preview.py ../../assets/mila

Writes <dir>/_sample.png (game scale) and _sample_casita.png (casita scale),
each with a nearest-neighbour _2x copy. Needs <dir>/_floor (mila.py --floor).
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import compose  # noqa: E402

D = os.path.abspath(sys.argv[1])
PAL = json.load(open(os.path.join(D, 'palettes.json')))


def mila(frame, x, y, items=()):
    out = [[frame, x, y, 0]]
    for it in items:
        out.append([it + '_' + frame[5:], x, y, 0, {'palette': it}])
    return out


def save(sc, name):
    img = compose.render_scene(sc, D)
    img.save(os.path.join(D, name + '.png'))
    img.resize((img.width * 2, img.height * 2), 0).save(os.path.join(D, name + '_2x.png'))
    with open(os.path.join(D, name + '_scene.json'), 'w') as fh:
        json.dump(sc, fh, indent=0)
    print('wrote', name)


# game scale: 5 x 8 cells
it = [['mila_floor', x, y, 0] for y in range(-1, 10) for x in range(-1, 6)]
it += mila('mila_idle_n_00', 0, 7, ('hat_crown', 'neck_pearls'))
it += mila('mila_idle_e_00', 1, 7, ('hat_party', 'neck_fish'))
it += mila('mila_idle_s_00', 2, 7, ('hat_bow', 'neck_bell'))
it += mila('mila_idle_w_00', 3, 7, ('hat_witch', 'neck_bow'))
it += mila('mila_win_s_04', 4, 7, ('hat_party',))
it += mila('mila_walk_e_02', 0, 5, ('hat_beret', 'neck_bandana'))
it += mila('mila_walk_n_01', 1, 5, ('hat_bunny',))
it += mila('mila_walk_s_03', 2, 5, ('hat_flower', 'neck_dots'))
it += mila('mila_walk_w_04', 3, 5, ('hat_beanie', 'neck_dots'))
it += mila('mila_yawn_s_05', 4, 5)
it += mila('mila_push_e_02', 0, 3, ('neck_bandana',)) + [['mila_ball', 1, 3, 0]]
it += [['mila_ball', 3, 3, 0]] + mila('mila_push_w_02', 4, 3, ('hat_bow',))
it += mila('mila_push_s_03', 0, 1) + [['mila_ball', 0, 0, 0]]
it += [['mila_ball', 2, 2, 0]] + mila('mila_push_n_00', 2, 1, ('neck_bell',))
it += mila('mila_idle_s_02', 4, 1, ('hat_beanie', 'neck_bow'))
save(dict(assets=['.', '_floor'], size=[368, 448], look_at=[2.5, 4.3, 0.25], bg=[0, 0, 0], palettes=PAL,
          items=it), '_sample')

# casita scale: 3 x 5 cells
it = [['mila_floor_c', x, y, 0] for y in range(-2, 8) for x in range(-1, 5)]
it += mila('mila_c_sit_s_00', 0, 4, ('hat_bunny', 'neck_dots'))
it += mila('mila_c_purr_s_02', 1, 4, ('hat_flower', 'neck_bow'))
it += mila('mila_c_groom_s_04', 2, 4, ('hat_crown',))
it += mila('mila_c_sleep_s_00', 0, 2, ('hat_beanie',))
it += mila('mila_c_pounce_e_05', 1, 2)
it += mila('mila_c_meow_s_01', 2, 2, ('hat_witch', 'neck_bell'))
it += mila('mila_c_belly_s_00', 0, 0, ('hat_bow', 'neck_fish'))
it += mila('mila_c_jump_w_02', 2, 0, ('hat_beret', 'neck_bandana'))
save(dict(assets=['.', '_floor'], size=[368, 448], look_at=[1.7, 2.75, 0.2], zoom=1.5, bg=[0, 0, 0], palettes=PAL,
          items=it), '_sample_casita')
