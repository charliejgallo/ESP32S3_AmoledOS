"""Abyss Bay: the four levels of the water-monster zone (zone 6, levels 20-23).

Terrain letters: s sand, r barnacle rock, q mossy quay, k pier planks (as
ground), ~ sea, # / | the pier on stilts over the sea, along X / Y.
Props: h lighthouse (2x2), b wreck bow (2x1), n wreck stern (2x1), r low
rock, R rock spire, p tide pool, l bollard, c stacked crates, t lobster trap,
u buoy (on the sand), a anchor, N fishing net, m harbour lamp, y barrels.
Timed things: the tide (lv.tide: sand the sea floods half of each period),
waves (trap 'wave', dir = where they push: 's' or 'e', the ways the art rolls), piranhas (trap 'piranha' on a
sea cell), whirlpools (whirl(): a deep cell that drags who lingers beside).
Rafts are log lanes, the sinking buoy-rafts lily lanes.
"""
from levels import Level
from _grid import Grid


def zone(lv):
    z = 'bay'
    lv.legend = {
        's': {'top': z + '_blk_sand', 'fill': z + '_fill_sand'},
        'r': {'top': z + '_blk_rock', 'fill': z + '_fill_rock'},
        'q': {'top': z + '_blk_quay', 'fill': z + '_fill_quay'},
        'k': {'top': z + '_blk_plank_x', 'fill': z + '_fill_quay'},
        'j': {'top': z + '_blk_plank_y', 'fill': z + '_fill_quay'},
    }
    lv.props = {
        'h': {'name': z + '_lighthouse', 'fp': (2, 2)}, 'b': {'name': z + '_wreck_bow', 'fp': (2, 1)},
        'n': {'name': z + '_wreck_stern', 'fp': (2, 1)}, 'r': {'name': z + '_rock_low'},
        'R': {'name': z + '_rock_spire'}, 'p': {'name': z + '_tidepool'}, 'l': {'name': z + '_bollard'},
        'c': {'name': z + '_crates'}, 't': {'name': z + '_trap'}, 'u': {'name': z + '_buoy'},
        'a': {'name': z + '_anchor'}, 'N': {'name': z + '_net'}, 'm': {'name': z + '_lamp'},
        'y': {'name': z + '_barrels'},
    }
    lv.surf = z + '_surf_sea'
    lv.deck = z + '_bridge_x'
    lv.deck_y = z + '_bridge_y'
    lv.crate = z + '_crate'
    lv.preview_assets = ['tiles_bay', 'objects', 'chars', 'monsters', 'bosses']
    lv.preview_bg = [6, 16, 28]


def whirl(lv, x, y):
    """a whirlpool: the deep sea's surface, and the pull beside it"""
    c = lv.cells[y][x]
    assert c['kind'] == 2, '%s: whirlpool on %d,%d, not sea' % (lv.name, x, y)
    c['surf'] = lv.asset('bay_surf_deep')
    lv.trap('whirl', x, y)


def gate_row(lv, y, x=7):
    """the harbour's far quay: lamps and barrels, the gate in the middle"""
    lv.place('y', [(k, y + 1) for k in range(0, 16) if k % 3 != 1])
    lv.place('m', [(k, y + 1) for k in range(1, 16, 3)])
    lv.place('l', [(x - 3, y), (x - 2, y), (x + 2, y), (x + 3, y)])
    lv.place('E', [(x, y)])
    lv.exit_dir = 2


def level1():
    lv = Level('bay_1', 'bay', 'The Pier', time_s=210, par_s=125)
    zone(lv)
    g = Grid(16, 30, 's')
    g.rect(0, 7, 15, 9, '~')
    g.rect(7, 7, 7, 9, '|')
    g.rect(0, 10, 15, 13, 'q', 1)
    g.rect(0, 14, 15, 17, '~', 1)
    g.rect(3, 14, 3, 17, '|', 1)
    g.rect(12, 14, 12, 17, '|', 1)
    g.rect(0, 18, 15, 22, 's', 1)
    g.rect(0, 23, 15, 25, '~', 1)
    g.rect(0, 26, 15, 29, 'q', 2)
    g.rect(6, 26, 8, 28, 'k', 2)
    g.apply(lv)
    gate_row(lv, 28)
    # the tidal flat before the first pier: the plank walk in the middle stays dry
    lv.tide([(x, y) for y in (4, 5, 6) for x in range(16) if x != 7], period=10000, phase=2000)
    lv.place('u', [(1, 1), (14, 0)])
    lv.place('a', [(12, 2)])
    lv.place('r', [(3, 0), (10, 1)])
    lv.place('m', [(2, 13), (13, 13), (5, 10), (10, 10)])
    lv.place('l', [(0, 13), (15, 13)])
    lv.place('c', [(3, 11), (12, 11)])
    lv.place('y', [(15, 10)])
    lv.place('h', [(11, 19)])
    lv.place('b', [(1, 20)])
    lv.place('n', [(3, 20)])
    lv.place('p', [(13, 18)])
    lv.place('R', [(15, 22)])
    lv.place('t', [(6, 18)])
    lv.place('K', [(0, 5), (15, 4), (14, 12), (0, 19), (14, 21)])
    lv.place('o', [(7, 1), (7, 3), (7, 5), (7, 8), (7, 11), (3, 15), (12, 16), (7, 19), (7, 22), (7, 27),
                   (5, 12), (9, 20)])
    lv.place('H', [(1, 11)])
    lv.place('*', [(15, 27)])
    lv.place('P', [(7, 12), (7, 20)])
    lv.place('S', [(7, 0)])
    # crabs scuttle the beach and the quay
    lv.monster('crab', 2, 2, 'e', path=[(2, 2), (6, 2)], pingpong=True, ms=600, param=2600)
    lv.monster('crab', 9, 3, 'e', path=[(9, 3), (13, 3)], pingpong=True, ms=560, param=2200)
    lv.monster('crab', 8, 12, 'e', path=[(8, 12), (12, 12)], pingpong=True, ms=520, param=2400)
    lv.monster('crab', 2, 21, 'e', path=[(2, 21), (6, 21)], pingpong=True, ms=540, param=2000)
    # rafts on the first stretch of sea, a jellyfish drifting among them
    lv.lane('log', 0, 7, 'e', 16, size=3, ms=440, gap=3)
    lv.lane('log', 15, 8, 'w', 16, size=2, ms=380, gap=4)
    lv.lane('log', 0, 9, 'e', 16, size=3, ms=460, gap=3)
    lv.monster('jelly', 1, 8, 'e', path=[(1, 8), (5, 8)], pingpong=True, ms=1400)
    # the harbour between the piers
    lv.lane('log', 0, 15, 'e', 16, size=3, ms=420, gap=3)
    lv.lane('log', 15, 16, 'w', 16, size=3, ms=400, gap=3)
    lv.monster('jelly', 8, 14, 'n', path=[(8, 14), (8, 17)], pingpong=True, ms=1300)
    # the fish-man waits under the far bank
    lv.monster('fishman', 5, 23, 's', path=[(5, 23), (5, 21), (9, 21)], pingpong=True, ms=620, param=6000)
    lv.lane('log', 0, 23, 'e', 16, size=4, ms=430, gap=2)
    lv.lane('lily', 0, 24, 'e', 16, ms=520, gap=0)
    lv.lane('log', 15, 25, 'w', 16, size=3, ms=390, gap=3)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level2():
    lv = Level('bay_2', 'bay', 'High Tide', time_s=230, par_s=140)
    zone(lv)
    g = Grid(16, 30, 's')
    g.rect(0, 5, 1, 6, '~')
    g.rect(14, 9, 15, 10, '~')
    g.rect(0, 12, 2, 12, '~')
    g.rect(0, 15, 15, 17, 'q', 1)
    g.rect(0, 18, 15, 20, '~', 1)
    g.rect(0, 21, 15, 25, 's', 1)
    g.rect(12, 22, 13, 23, 'r', 2)
    g.rect(0, 26, 15, 29, 'q', 2)
    g.rect(6, 26, 8, 28, 'k', 2)
    g.apply(lv)
    gate_row(lv, 28)
    # the flat: four bands the sea takes in turn, a wave going up the beach
    for i, y in enumerate((4, 7, 10, 13)):
        lv.tide([(x, y) for x in range(16) if lv.cells[y][x]['kind'] == 0], period=8000, phase=i * 2000)
    lv.tide([(x, 23) for x in range(0, 12)], period=7000, phase=3500)
    lv.place('r', [(4, 2), (11, 1), (4, 9), (12, 6), (6, 11), (9, 14)])
    lv.place('p', [(13, 12), (2, 9)])
    lv.place('a', [(15, 3)])
    lv.place('m', [(2, 17), (13, 17)])
    lv.place('l', [(5, 15), (10, 15)])
    lv.place('c', [(0, 16)])
    lv.place('N', [(3, 22)])
    lv.place('t', [(9, 24)])
    lv.place('u', [(15, 21)])
    lv.place('K', [(0, 3), (15, 8), (1, 11), (15, 16), (13, 22)])
    lv.place('o', [(7, 1), (7, 3), (7, 5), (7, 8), (7, 11), (7, 14), (7, 16), (7, 21), (7, 24), (7, 27),
                   (3, 5), (12, 9)])
    lv.place('T', [(0, 14)])
    lv.place('*', [(0, 25)])
    lv.place('P', [(7, 15), (7, 22)])
    lv.place('S', [(7, 0)])
    # fish-men in the pools at the flat's edges
    lv.monster('fishman', 1, 6, 's', path=[(1, 6), (1, 8), (5, 8)], pingpong=True, ms=600, param=5000)
    lv.monster('fishman', 14, 9, 's', path=[(14, 9), (14, 11), (10, 11)], pingpong=True, ms=600, param=5000)
    lv.monster('fishman', 2, 12, 's', path=[(2, 12), (4, 12), (4, 14)], pingpong=True, ms=620, param=4500)
    # waves break over the quay's sea edge and throw Tommy back a row (the
    # art rolls towards the camera: waves along Y push south, along X east)
    for x, ph in ((2, 0), (5, 700), (9, 1400), (12, 2100)):
        lv.trap('wave', x, 17, 's', period=2800, phase=ph)
    # the piranha channel: rafts across, and cells that boil in turns
    lv.lane('log', 0, 18, 'e', 16, size=3, ms=420, gap=3)
    lv.lane('log', 15, 19, 'w', 16, size=3, ms=380, gap=3)
    lv.lane('log', 0, 20, 'e', 16, size=4, ms=450, gap=2)
    for x, y, ph in ((4, 18, 0), (10, 18, 1500), (6, 19, 800), (12, 19, 2300), (3, 20, 1200), (9, 20, 400)):
        lv.trap('piranha', x, y, period=3000, phase=ph)
    lv.monster('crab', 2, 24, 'e', path=[(2, 24), (6, 24)], pingpong=True, ms=520, param=2200)
    lv.monster('crab', 9, 21, 'e', path=[(9, 21), (11, 21)], pingpong=True, ms=500, param=1800)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level3():
    lv = Level('bay_3', 'bay', 'The Wreck', time_s=240, par_s=150)
    zone(lv)
    g = Grid(16, 30, 's')
    g.rect(0, 4, 15, 8, '~')
    g.rect(7, 4, 7, 8, '|')
    g.rect(0, 9, 15, 13, 's')
    g.rect(0, 14, 15, 15, '~')
    g.rect(0, 16, 15, 21, 'r', 1)
    g.rect(1, 17, 6, 20, 's', 1)
    g.rect(11, 17, 13, 18, '~', 1)
    g.rect(0, 22, 15, 25, '~', 1)
    g.rect(0, 26, 15, 29, 'q', 2)
    g.rect(6, 26, 8, 28, 'k', 2)
    g.apply(lv)
    gate_row(lv, 28)
    whirl(lv, 4, 6)
    whirl(lv, 11, 6)
    whirl(lv, 9, 23)
    lv.place('b', [(5, 11)])
    lv.place('n', [(7, 11)])
    lv.place('a', [(10, 12)])
    lv.place('r', [(1, 10), (14, 9), (0, 1), (15, 2)])
    lv.place('R', [(14, 13), (0, 21), (15, 16)])
    lv.place('p', [(3, 18), (8, 20)])
    lv.place('t', [(9, 17)])
    lv.place('u', [(12, 0)])
    lv.place('C', [(3, 12), (12, 12)])
    lv.place('K', [(1, 9), (15, 11), (1, 19), (14, 20), (0, 27)])
    lv.place('o', [(7, 1), (7, 3), (7, 5), (7, 8), (7, 10), (3, 13), (12, 13), (7, 17), (7, 19), (7, 21), (7, 27),
                   (2, 16), (10, 9)])
    lv.place('H', [(15, 19)])
    lv.place('*', [(13, 10)])
    lv.place('P', [(7, 9), (7, 18)])
    lv.place('S', [(7, 0)])
    # rafts past the whirlpools: whoever rides one over a whirlpool goes under
    lv.lane('log', 0, 4, 'e', 16, size=3, ms=420, gap=3)
    lv.lane('log', 15, 5, 'w', 16, size=3, ms=380, gap=3)
    lv.lane('log', 0, 6, 'e', 16, size=2, ms=460, gap=4)
    lv.lane('log', 15, 7, 'w', 16, size=3, ms=400, gap=3)
    lv.lane('log', 0, 8, 'e', 16, size=3, ms=440, gap=3)
    # the channel: push a crate in, or ride the rafts past the piranhas
    lv.lane('log', 15, 14, 'w', 16, size=3, ms=400, gap=3)
    lv.lane('log', 0, 15, 'e', 16, size=3, ms=420, gap=3)
    for x, y, ph in ((5, 14, 0), (10, 14, 1400), (8, 15, 700), (13, 15, 2000)):
        lv.trap('piranha', x, y, period=2800, phase=ph)
    lv.monster('crab', 1, 2, 'e', path=[(1, 2), (5, 2)], pingpong=True, ms=560, param=2400)
    lv.monster('crab', 9, 1, 'e', path=[(9, 1), (13, 1)], pingpong=True, ms=540, param=2000)
    lv.monster('fishman', 12, 17, 's', path=[(12, 17), (12, 19), (9, 19)], pingpong=True, ms=600, param=5000)
    lv.monster('fishman', 2, 14, 's', path=[(2, 14), (2, 12), (1, 12)], pingpong=True, ms=620, param=4000)
    lv.lane('log', 0, 22, 'e', 16, size=4, ms=430, gap=2)
    lv.lane('lily', 0, 23, 'e', 16, ms=500, gap=0)
    lv.lane('log', 15, 24, 'w', 16, size=3, ms=380, gap=3)
    lv.lane('log', 0, 25, 'e', 16, size=3, ms=450, gap=3)
    lv.monster('jelly', 2, 24, 'e', path=[(2, 24), (6, 24)], pingpong=True, ms=1500)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level4():
    """The lair: the kraken in the middle of the bay; piers around it and
    rafts across. Every few seconds it marks Tommy's row and slams a
    tentacle along it."""
    lv = Level('bay_4', 'bay', "The Kraken's Lair", time_s=220, par_s=130)
    zone(lv)
    g = Grid(16, 30, '~', 1)
    g.rect(0, 0, 15, 3, 'q', 1)
    g.rect(6, 0, 8, 3, 'k', 1)
    g.rect(1, 4, 1, 25, '|', 1)
    g.rect(14, 4, 14, 25, '|', 1)
    g.rect(1, 9, 14, 9, '#', 1)
    g.rect(1, 20, 14, 20, '#', 1)
    g.rect(4, 5, 5, 6, 's', 1)
    g.rect(10, 5, 11, 6, 's', 1)
    g.rect(4, 23, 5, 24, 's', 1)
    g.rect(10, 23, 11, 24, 's', 1)
    g.rect(3, 14, 4, 15, 'r', 1)
    g.rect(11, 14, 12, 15, 'r', 1)
    g.rect(0, 26, 15, 29, 'q', 2)
    g.rect(6, 26, 8, 28, 'k', 2)
    g.apply(lv)
    gate_row(lv, 28)
    lv.place('m', [(0, 2), (15, 2), (3, 1), (12, 1)])
    lv.place('l', [(5, 3), (10, 3)])
    lv.place('R', [(5, 5), (10, 24)])
    lv.place('p', [(11, 6), (4, 23)])
    lv.place('K', [(4, 6), (11, 5), (3, 15), (12, 14), (5, 24)])
    lv.place('o', [(7, 1), (7, 3), (1, 6), (14, 7), (5, 9), (10, 9), (1, 14), (14, 15), (6, 20), (9, 20),
                   (1, 23), (14, 22), (7, 27)])
    lv.place('H', [(11, 24)])
    lv.place('*', [(4, 14)])
    lv.place('P', [(1, 9), (14, 20)])
    lv.place('S', [(7, 0)])
    # rafts from the piers to the rocks beside the kraken
    lv.lane('log', 2, 12, 'e', 12, size=3, ms=460, gap=3)
    lv.lane('log', 13, 17, 'w', 12, size=3, ms=440, gap=3)
    lv.lane('lily', 2, 7, 'e', 12, ms=520, gap=0)
    lv.lane('lily', 2, 22, 'e', 12, ms=540, gap=500)
    lv.monster('jelly', 6, 11, 'e', path=[(6, 11), (9, 11)], pingpong=True, ms=1200)
    lv.monster('jelly', 6, 18, 'e', path=[(9, 18), (6, 18)], pingpong=True, ms=1300)
    for x, y, ph in ((6, 12, 0), (9, 17, 1300)):
        lv.trap('piranha', x, y, period=3200, phase=ph)
    # the boss: 2x2 from (7, 14) in open water; p1 = ms between strikes
    lv.monster('kraken', 7, 14, 's', param=2800)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def levels():
    return [level1(), level2(), level3(), level4()]
