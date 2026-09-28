"""Lost Valley: the four levels of the dinosaur zone (zone 5, levels 16-19).

Terrain letters: g jungle grass, d red dirt, b basalt, p stepping-stone path,
n fossil-bone floor, ~ jungle pool, % tar (it swallows who stands still),
# / | a fallen-trunk bridge along X / Y.
Props: f fern, v small fern (walkable), y cycad, i fiddleheads (walkable),
t big-trunk tree, a palm, r ribcage (2x1), R ribcage along Y (1x2), k fossil
skull, u boulder, N nest with eggs, x bone fence (X), X bone fence (Y),
V glowing volcanic rock, A amber crystal, w flowers (walkable), h bush.
Timed things: lava cracks (trap 'lava', dir e/w = a crack along X), geysers
(trap 'vent'), falling rocks (trap 'rock'). Packs of compies are lanes.
"""
from levels import Level
from _grid import Grid


def zone(lv):
    z = 'dino'
    lv.legend = {
        'g': {'top': z + '_blk_grass', 'fill': z + '_fill_dirt'},
        'd': {'top': z + '_blk_dirt', 'fill': z + '_fill_dirt'},
        'b': {'top': z + '_blk_basalt', 'fill': z + '_fill_basalt'},
        'p': {'top': z + '_blk_path', 'fill': z + '_fill_dirt'},
        'n': {'top': z + '_blk_bone', 'fill': z + '_fill_dirt'},
    }
    lv.props = {
        'f': {'name': z + '_fern'}, 'v': {'name': z + '_fern_small', 'walk': True}, 'y': {'name': z + '_cycad'},
        'i': {'name': z + '_fiddlehead', 'walk': True}, 't': {'name': z + '_tree'}, 'a': {'name': z + '_palm'},
        'r': {'name': z + '_ribcage_x', 'fp': (2, 1)}, 'R': {'name': z + '_ribcage_y', 'fp': (1, 2)},
        'k': {'name': z + '_skull'}, 'u': {'name': z + '_boulder'}, 'N': {'name': z + '_nest'},
        'x': {'name': z + '_bonefence_x'}, 'X': {'name': z + '_bonefence_y'}, 'V': {'name': z + '_volcanorock'},
        'A': {'name': z + '_amber'}, 'w': {'name': z + '_flowers', 'walk': True}, 'h': {'name': z + '_bush'},
    }
    lv.surf = z + '_surf_water'
    lv.quick = z + '_surf_tar'
    lv.deck = z + '_bridge_x'
    lv.deck_y = z + '_bridge_y'
    lv.crate = z + '_crate'
    lv.preview_assets = ['tiles_dino', 'objects', 'chars', 'monsters', 'bosses']
    lv.preview_bg = [28, 14, 8]


def gate_row(lv, y, x=7):
    """a wall of trees and palms at the far edge, the exit in its middle"""
    lv.place('t', [(k, y + 1) for k in range(0, 16, 2)])
    lv.place('a', [(k, y + 1) for k in range(1, 16, 2)])
    lv.place('x', [(x - 3, y), (x - 2, y), (x + 2, y), (x + 3, y)])
    lv.place('E', [(x, y)])
    lv.exit_dir = 2


def level1():
    lv = Level('dino_1', 'dino', 'The Steaming Jungle', time_s=210, par_s=120)
    zone(lv)
    g = Grid(16, 30, 'g')
    g.rect(7, 0, 7, 2, 'p')
    g.rect(6, 3, 8, 3, 'p')
    g.row(4, 'd')
    g.row(5, 'd')
    g.rect(5, 6, 9, 7, 'p')
    g.rect(6, 8, 8, 8, 'p')
    g.rect(7, 9, 7, 9, 'p')
    g.rect(0, 7, 1, 8, '%')
    for y in (10, 11, 12):
        g.row(y, '~')
    g.rect(0, 13, 15, 16, 'b', 1)
    g.rect(7, 13, 7, 16, 'p', 1)
    g.row(17, 'd')
    g.row(18, 'd')
    g.rect(8, 19, 8, 19, 'p')
    g.rect(7, 20, 9, 22, 'p')
    g.rect(6, 23, 8, 24, 'p')
    g.rect(3, 21, 4, 22, '%')
    g.rect(13, 23, 14, 24, '%')
    g.row(25, '~')
    g.row(26, '~')
    g.rect(6, 27, 8, 27, 'p')
    g.rect(7, 28, 7, 28, 'p')
    g.apply(lv)
    gate_row(lv, 28)
    lv.place('f', [(3, 1), (11, 2), (13, 0), (1, 3), (14, 3), (4, 9), (11, 8)])
    lv.place('y', [(0, 0), (15, 1)])
    lv.place('k', [(12, 7)])
    lv.place('u', [(4, 14), (11, 14)])
    lv.place('A', [(2, 15)])
    lv.place('N', [(12, 16)])
    lv.place('t', [(0, 9), (15, 9), (0, 19), (15, 19)])
    lv.place('w', [(9, 6), (5, 20)])
    lv.place('r', [(0, 22)])
    lv.place('h', [(2, 27), (13, 27)])
    lv.place('v', [(10, 24), (4, 24)])
    lv.place('K', [(1, 6), (14, 15), (2, 20), (14, 21), (12, 27)])
    lv.place('o', [(7, 1), (7, 2), (7, 3), (6, 6), (8, 7), (7, 9), (7, 13), (7, 16), (8, 19), (8, 21), (7, 24),
                   (7, 27), (3, 6), (12, 21)])
    lv.place('H', [(0, 14)])
    lv.place('*', [(0, 23)])
    lv.place('P', [(7, 14), (7, 22)])
    lv.place('S', [(7, 0)])
    # packs of compies on the red-dirt tracks
    lv.lane('compy', 0, 4, 'e', 16, size=3, ms=280, gap=5)
    lv.lane('compy', 15, 5, 'w', 16, size=2, ms=250, gap=6)
    lv.lane('compy', 0, 17, 'e', 16, size=3, ms=260, gap=4)
    lv.lane('compy', 15, 18, 'w', 16, size=3, ms=300, gap=5)
    # fern trunks drifting on the pools
    lv.lane('log', 0, 10, 'e', 16, size=3, ms=430, gap=3)
    lv.lane('log', 15, 11, 'w', 16, size=2, ms=360, gap=3)
    lv.lane('log', 0, 12, 'e', 16, size=3, ms=460, gap=2)
    lv.lane('log', 15, 25, 'w', 16, size=3, ms=400, gap=3)
    lv.lane('log', 0, 26, 'e', 16, size=4, ms=450, gap=2)
    # geysers on the basalt shelf
    lv.trap('vent', 5, 15, period=2600, phase=0)
    lv.trap('vent', 10, 13, period=2600, phase=1300)
    lv.trap('vent', 9, 16, period=3000, phase=700)
    # raptors hunt in pairs
    loop1 = [(10, 6), (14, 6), (14, 9), (10, 9)]
    lv.monster('raptor', 10, 6, path=loop1, ms=520)
    lv.monster('raptor', 14, 9, path=loop1[2:] + loop1[:2], ms=520)
    loop2 = [(10, 20), (13, 20), (13, 22), (10, 22)]
    lv.monster('raptor', 10, 20, path=loop2, ms=500)
    lv.monster('raptor', 13, 22, path=loop2[2:] + loop2[:2], ms=500)
    lv.monster('ptero', 13, 13, 's')
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level2():
    lv = Level('dino_2', 'dino', 'The Tar Pits', time_s=230, par_s=140)
    zone(lv)
    g = Grid(16, 30, 'g')
    g.rect(6, 0, 8, 2, 'p')
    # the first tar field: stepping stones zigzag across, islands off to the sides
    g.rect(0, 3, 15, 8, '%')
    g.cells([(7, 3), (7, 4), (6, 4), (5, 4), (5, 5), (5, 6), (6, 6), (7, 6), (8, 6), (9, 6), (9, 7), (9, 8)], 'p')
    g.rect(0, 5, 2, 6, 'g')
    g.rect(13, 3, 15, 4, 'g')
    g.rect(12, 7, 13, 8, 'n')
    # trike corridors: red dirt between basalt ledges
    g.rect(0, 9, 15, 12, 'd')
    g.cells([(2, 9), (3, 9), (12, 9), (13, 9), (5, 12), (6, 12), (10, 12), (11, 12)], 'b', 1)
    # a two-row ravine: a crate fills a gap, or the super hop jumps one row
    g.rect(0, 13, 15, 14, '.')
    # the second tar field: rocks fall on the stones
    g.rect(0, 15, 15, 20, '%')
    g.cells([(6, 15), (6, 16), (7, 16), (8, 16), (8, 17), (8, 18), (7, 18), (6, 18), (6, 19), (6, 20),
             (9, 15)], 'p')
    g.rect(12, 16, 15, 18, 'g')
    g.rect(0, 18, 2, 20, 'n')
    # the jungle before the gate
    g.rect(0, 21, 15, 27, 'g')
    g.rect(6, 21, 8, 27, 'p')
    g.rect(0, 23, 3, 24, '%')
    g.rect(12, 25, 15, 26, '%')
    g.rect(6, 28, 8, 28, 'p')
    g.apply(lv)
    gate_row(lv, 28)
    lv.place('f', [(1, 0), (13, 1), (0, 22), (15, 21), (10, 27), (4, 27)])
    lv.place('y', [(0, 2), (15, 0)])
    lv.place('k', [(15, 9)])
    lv.place('u', [(0, 10), (4, 11), (9, 10), (14, 12)])
    lv.place('A', [(15, 17)])
    lv.place('N', [(2, 21)])
    lv.place('R', [(11, 22)])
    lv.place('V', [(3, 12), (12, 12)])
    lv.place('v', [(4, 21), (13, 23)])
    lv.place('i', [(1, 1), (10, 1)])
    lv.place('C', [(7, 12), (9, 12)])
    lv.place('K', [(0, 6), (14, 3), (13, 8), (14, 16), (1, 19)])
    lv.place('o', [(7, 1), (7, 3), (5, 5), (7, 6), (9, 7), (7, 10), (8, 11), (6, 16), (8, 17), (6, 19), (7, 22),
                   (7, 24), (7, 26), (2, 5), (15, 18)])
    lv.place('T', [(13, 4)])
    lv.place('*', [(15, 27)])
    lv.place('P', [(7, 9), (7, 21)])
    lv.place('S', [(7, 0)])
    # triceratops graze the corridors and charge down them
    lv.monster('trike', 1, 10, 'e', path=[(1, 10), (8, 10)], pingpong=True, ms=900)
    lv.monster('trike', 15, 11, 'w', path=[(15, 11), (5, 11)], pingpong=True, ms=850)
    lv.monster('trike', 9, 25, 'e', path=[(9, 25), (11, 25)], pingpong=True, ms=900)
    # rocks from the volcano on the second field's stones
    lv.trap('rock', 7, 16, period=3400, phase=0)
    lv.trap('rock', 8, 18, period=3400, phase=1100)
    lv.trap('rock', 6, 20, period=3400, phase=2200)
    lv.trap('rock', 13, 17, period=4000, phase=500)
    # pterodactyls watch the jungle
    lv.monster('ptero', 4, 25, 's')
    lv.monster('ptero', 11, 21, 's')
    lv.monster('raptor', 1, 26, path=[(1, 26), (5, 26)], pingpong=True, ms=480)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level3():
    lv = Level('dino_3', 'dino', 'The Lava River', time_s=240, par_s=150)
    zone(lv)
    g = Grid(16, 30, 'b', 0)
    g.rect(0, 0, 15, 2, 'g')
    g.rect(6, 0, 8, 2, 'p')
    # the first basalt shelf: rows of cracks that wake in a wave
    g.rect(0, 3, 15, 8, 'b', 1)
    # a jungle pool with trunks
    g.rect(0, 9, 15, 11, '~')
    g.rect(0, 12, 15, 13, 'g', 0)
    # the high shelf, two steps up
    g.rect(0, 14, 15, 15, 'b', 1)
    g.rect(0, 16, 15, 21, 'b', 2)
    g.rect(7, 14, 8, 21, 'p')
    g.rect(7, 14, 8, 15, None, 1)
    # red-dirt track with compies, then a lava field before the gate
    g.rect(0, 22, 15, 23, 'd', 0)
    g.rect(0, 24, 15, 27, 'b', 0)
    g.rect(6, 28, 8, 28, 'p', 0)
    g.apply(lv)
    gate_row(lv, 28)
    lv.place('y', [(0, 0), (15, 2), (1, 13), (14, 12)])
    lv.place('f', [(3, 1), (12, 0), (5, 12), (10, 13)])
    # the cracks run from wall to wall: glowing rocks close their ends
    lv.place('V', [(0, 4), (0, 6), (0, 8), (15, 4), (15, 6), (15, 8), (0, 17), (15, 19), (3, 25), (12, 26)])
    lv.place('k', [(2, 20)])
    lv.place('A', [(13, 16)])
    lv.place('u', [(4, 18), (11, 18)])
    lv.place('K', [(1, 3), (14, 4), (0, 12), (15, 21), (2, 27)])
    lv.place('o', [(7, 1), (7, 3), (8, 5), (7, 7), (7, 12), (8, 14), (7, 16), (8, 18), (7, 20), (7, 24), (8, 26),
                   (14, 13), (3, 16)])
    lv.place('H', [(12, 20)])
    lv.place('*', [(15, 24)])
    lv.place('P', [(8, 12), (7, 21)])
    lv.place('S', [(7, 0)])
    # lava cracks across the first shelf: each row wakes a beat after the last
    for y, ph in ((4, 0), (6, 1000), (8, 2000)):
        for x in range(1, 15):
            lv.trap('lava', x, y, 'e', period=4200, phase=ph + (x % 2) * 150)
    # and across the last field, in columns now
    for x, ph in ((4, 0), (7, 900), (10, 1800), (13, 2700)):
        for y in (24, 25, 26):
            lv.trap('lava', x, y, 'n', period=3600, phase=ph)
    lv.lane('log', 0, 9, 'e', 16, size=3, ms=420, gap=3)
    lv.lane('log', 15, 10, 'w', 16, size=3, ms=380, gap=3)
    lv.lane('log', 0, 11, 'e', 16, size=2, ms=340, gap=3)
    lv.lane('compy', 0, 22, 'e', 16, size=4, ms=240, gap=5)
    lv.lane('compy', 15, 23, 'w', 16, size=3, ms=260, gap=5)
    lv.trap('vent', 4, 16, period=2400, phase=0)
    lv.trap('vent', 11, 16, period=2400, phase=1200)
    lv.trap('vent', 5, 20, period=2800, phase=600)
    lv.trap('rock', 7, 18, period=3000, phase=0)
    lv.trap('rock', 8, 19, period=3000, phase=1500)
    loop = [(1, 16), (3, 16), (3, 19), (1, 19)]
    lv.monster('raptor', 1, 15, path=loop, ms=480)
    lv.monster('raptor', 5, 19, path=loop[2:] + loop[:2], ms=480)
    lv.monster('trike', 10, 15, 'e', path=[(10, 15), (14, 15)], pingpong=True, ms=900)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level4():
    """The lair is a chase: the T-Rex comes up the valley behind Tommy,
    trampling ferns and bones; the keys are along the way and the gate is at
    the far end."""
    lv = Level('dino_4', 'dino', 'The T-Rex Run', time_s=150, par_s=75)
    zone(lv)
    H = 40
    g = Grid(16, H, 'g')
    g.rect(0, 0, 15, 3, 'd')
    g.rect(5, 4, 10, 39, 'p')
    g.rect(0, 9, 3, 10, '%')
    g.rect(12, 12, 15, 13, '%')
    g.rect(2, 17, 4, 18, '%')
    g.rect(0, 19, 15, 20, 'd')
    g.rect(0, 23, 15, 25, '~')
    g.rect(11, 29, 13, 30, '%')
    g.rect(0, 31, 15, 32, 'b')
    g.rect(0, 35, 15, 36, 'd')
    g.rect(6, 38, 8, 38, 'p')
    g.apply(lv)
    gate_row(lv, 38)
    # what the T-Rex tramples (the level is readable after it too)
    lv.place('f', [(4, 6), (11, 7), (1, 12), (14, 16), (6, 14), (9, 21), (3, 27), (12, 33), (5, 34)])
    lv.place('v', [(8, 8), (7, 15), (6, 28), (9, 34)])
    lv.place('y', [(0, 5), (15, 5), (0, 15), (15, 22), (0, 28), (15, 34)])
    lv.place('h', [(10, 11), (3, 22), (12, 27)])
    lv.place('k', [(13, 9), (2, 33)])
    lv.place('u', [(9, 17), (4, 30)])
    lv.place('N', [(1, 26)])
    lv.place('V', [(0, 31), (15, 32)])
    lv.place('K', [(2, 7), (14, 14), (1, 21), (14, 28), (3, 36)])
    lv.place('o', [(7, 5), (8, 9), (7, 12), (8, 16), (7, 19), (8, 22), (7, 27), (8, 30), (7, 33), (8, 36),
                   (13, 21), (2, 29)])
    lv.place('H', [(15, 10)])
    lv.place('*', [(0, 37)])
    lv.place('P', [(7, 18), (7, 29)])
    lv.place('S', [(7, 4)])
    lv.lane('compy', 0, 19, 'e', 16, size=3, ms=260, gap=6)
    lv.lane('compy', 15, 20, 'w', 16, size=3, ms=280, gap=6)
    lv.lane('log', 0, 23, 'e', 16, size=4, ms=380, gap=2)
    lv.lane('log', 15, 24, 'w', 16, size=3, ms=360, gap=2)
    lv.lane('log', 0, 25, 'e', 16, size=4, ms=400, gap=2)
    for x, ph in ((2, 0), (5, 600), (8, 1200), (11, 1800), (14, 2400)):
        lv.trap('lava', x, 31, 'n', period=3000, phase=ph)
        lv.trap('lava', x, 32, 'n', period=3000, phase=ph)
    lv.lane('compy', 0, 35, 'e', 16, size=2, ms=220, gap=5)
    lv.lane('compy', 15, 36, 'w', 16, size=2, ms=240, gap=5)
    lv.trap('rock', 6, 26, period=2800, phase=0)
    lv.trap('rock', 9, 33, period=2800, phase=1400)
    # the boss: 2x2 from (7, 0); p0 = ms per cell it climbs, p1 = ms between stomps
    lv.monster('trex', 7, 0, 'n', ms=640, param=3500)
    lv.monster('ptero', 13, 18, 's')
    lv.preview_size = [1700, 2000]
    lv.preview_look = [8, 20, 0]
    return lv


def levels():
    return [level1(), level2(), level3(), level4()]
