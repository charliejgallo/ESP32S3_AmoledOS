"""Werewolf Woods: the four levels of the green zone.

Terrain letters: g grass, p dirt trail, m moss slabs, r rock, w wooden deck,
d mud, ~ river, # plank bridge.
Props: i pine, a oak, u bush, M glowing mushroom, s stump, l fallen log (2x1),
I fallen log along Y (1x2), r rock, c cabin (2x2), f fence (X), F fence (Y),
n lantern, w flowers (walkable), e fern (walkable), h moon shrine, W mill (2x2).
"""
from levels import Level


def zone(lv):
    z = 'forest'
    lv.legend = {
        'g': {'top': z + '_blk_grass', 'fill': z + '_fill_earth'},
        'p': {'top': z + '_blk_path', 'fill': z + '_fill_earth'},
        'm': {'top': z + '_blk_moss', 'fill': z + '_fill_rock'},
        'r': {'top': z + '_blk_rock', 'fill': z + '_fill_rock'},
        'w': {'top': z + '_blk_plank', 'fill': z + '_fill_earth'},
        'd': {'top': z + '_blk_mud', 'fill': z + '_fill_earth'},
    }
    lv.props = {
        'i': {'name': z + '_pine'}, 'a': {'name': z + '_oak'}, 'u': {'name': z + '_bush'},
        'M': {'name': z + '_mushroom'}, 's': {'name': z + '_stump'}, 'l': {'name': z + '_log_x', 'fp': (2, 1)},
        'I': {'name': z + '_log_y', 'fp': (1, 2)}, 'r': {'name': z + '_rock'}, 'c': {'name': z + '_cabin', 'fp': (2, 2)},
        'f': {'name': z + '_fence_x'}, 'F': {'name': z + '_fence_y'}, 'n': {'name': z + '_lantern'},
        'w': {'name': z + '_flowers', 'walk': True}, 'e': {'name': z + '_fern', 'walk': True},
        'h': {'name': z + '_shrine'}, 'W': {'name': z + '_mill', 'fp': (2, 2)},
    }
    lv.surf = z + '_surf_river'
    lv.deck = z + '_bridge_x'
    lv.deck_y = z + '_bridge_y'
    lv.crate = z + '_crate'
    lv.preview_assets = ['tiles_forest', 'objects', 'chars', 'monsters']
    lv.preview_bg = [8, 18, 14]


def level1():
    lv = Level('forest_1', 'forest', 'The Trail', time_s=210, par_s=120)
    zone(lv)
    lv.terrain('''
g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0p0p0p0g0g0g0g0g0g0g0
g0g0g0g0g0p0p0p0p0p0g0g0g0g0g0g0
g0g0g0g0p0p0p0p0p0p0p0g0g0g0g0g0
g0g0g0g0g0g0p0p0p0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0
~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0
~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
r1r1r1g0g0g0g0p0g0g0g0g0r1r2r2r1
r2r2r1g0g0g0p0p0g0g0g0m1r1r2r2r1
r2r1m1g0g0g0p0g0g0g0g0g0g0r1r1g0
r1m1g0g0g0g0p0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0p0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0
~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0p0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0p0p0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0p0g0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0p0p0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0p0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0p0g0g0g0g0g0g0g0g0
''')
    # a wall of trees at the back, the gate in its gap
    lv.place('i', [(x, 29) for x in range(0, 16, 2)] + [(x, 28) for x in (0, 3, 12, 15)])
    lv.place('a', [(x, 29) for x in range(1, 16, 2) if x != 7])
    lv.place('E', [(7, 28)])
    lv.place('n', [(6, 27), (8, 27), (4, 23), (11, 23), (6, 18), (8, 18), (6, 7), (8, 7)])
    lv.place('M', [(2, 25), (13, 24), (1, 11), (14, 12)])
    lv.place('i', [(0, 26), (15, 26), (0, 20), (15, 20), (10, 15), (4, 13), (13, 3), (1, 1), (14, 0)])
    lv.place('a', [(2, 22), (13, 21), (3, 18), (11, 11), (2, 6), (11, 5)])
    lv.place('u', [(1, 4), (10, 2), (9, 13), (12, 26)])
    lv.place('r', [(3, 5), (9, 5), (5, 24), (10, 25)])
    lv.place('s', [(4, 2), (12, 13)])
    lv.place('w', [(3, 3), (12, 7), (5, 22), (10, 22)])
    lv.place('e', [(8, 3), (2, 14), (13, 18)])
    lv.place('K', [(0, 4), (0, 16), (14, 17), (14, 10), (8, 25)])
    lv.place('o', [(7, 2), (6, 4), (5, 5), (6, 6), (7, 11), (6, 13), (6, 15), (7, 18), (7, 22), (6, 24), (7, 26)])
    lv.place('H', [(15, 14)])
    lv.place('*', [(15, 16)])
    lv.place('P', [(7, 12), (7, 23)])
    lv.place('S', [(7, 0)])
    # the rivers: logs along X, lily pads in the middle of the wide one
    lv.lane('log', 0, 8, 'e', 16, size=3, ms=430, gap=3)
    lv.lane('log', 15, 9, 'w', 16, size=2, ms=360, gap=3)
    lv.lane('log', 0, 19, 'e', 16, size=4, ms=460, gap=2)
    lv.lane('lily', 0, 20, 'e', 16, ms=520, gap=0)
    lv.lane('log', 15, 21, 'w', 16, size=3, ms=390, gap=3)
    # werewolves guard the clearings; crows perch by the trail
    lv.monster('werewolf', 12, 5, 'w')
    lv.monster('werewolf', 3, 24, 'e')
    lv.monster('werewolf', 12, 25, 'w')
    lv.monster('crow', 3, 12, 's')
    lv.monster('crow', 12, 14, 's')
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv



def level2():
    lv = Level('forest_2', 'forest', 'Rushing River', time_s=230, par_s=140)
    zone(lv)
    # rows, far first: 29-27 the gate, 26-25 a stream, 24-22 the cabin's bank,
    # 21-18 a river with a plank bridge on the right, 17-14 rock islands,
    # 13-10 the wide river, 9-7 an island, 6-3 the first river, 2-0 the start
    lv.map2("""
gggggggggggggggg
gggggggpgggggggg
gggggggpgggggggg
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
ggggggpppggggggg
gggggppppggggggg
ggggggppgggggggg
~~~~~~~~~~~~~~|~
~~~~~~~~~~~~~~|~
~~~~~~~~~~~~~~|~
~~~~~~~~~~~~~~|~
ggggggpggggggggg
gggrrrppggrrgggg
ggrrrrppgrrrrggg
gggrrrppggrrgggg
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
ggggggpggggggggg
gggggppggggggggg
ggggppgggggggggg
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
ggggggpggggggggg
gggggggpgggggggg
gggggggpgggggggg
""", """
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0001110000110000
0012221001221000
0001110000110000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
""")
    lv.place('i', [(x, 29) for x in range(0, 16, 2) if x != 6] + [(0, 28), (15, 28)])
    lv.place('a', [(x, 29) for x in range(1, 16, 2) if x != 7])
    lv.place('E', [(7, 28)])
    lv.place('n', [(6, 27), (8, 27), (5, 22), (8, 22), (5, 9), (8, 9), (5, 1), (9, 1)])
    lv.place('c', [(13, 23)])
    lv.place('M', [(0, 22), (15, 22), (0, 14), (15, 14)])
    lv.place('i', [(1, 24), (10, 22), (3, 24), (11, 24), (0, 7), (15, 9), (2, 0), (13, 1)])
    lv.place('u', [(9, 7), (4, 8), (11, 17), (1, 16)])
    lv.place('e', [(3, 7), (12, 7), (2, 22)])
    lv.place('K', [(1, 7), (4, 15), (11, 15), (1, 23), (3, 27)])
    lv.place('o', [(7, 1), (7, 2), (7, 9), (6, 7), (7, 14), (6, 16), (6, 15), (6, 22), (5, 23), (4, 22)])
    lv.place('*', [(15, 27)])
    lv.place('T', [(14, 7)])
    lv.place('P', [(7, 7), (6, 23)])
    lv.place('S', [(7, 0)])
    lv.trap('bear', 10, 8)
    lv.trap('bear', 2, 17)
    lv.trap('bear', 9, 23)
    lv.lane('log', 0, 3, 'e', 16, size=3, ms=400, gap=3)
    lv.lane('log', 15, 4, 'w', 16, size=2, ms=330, gap=3)
    lv.lane('lily', 0, 5, 'e', 16, ms=480, gap=300)
    lv.lane('log', 15, 6, 'w', 16, size=3, ms=380, gap=3)
    lv.lane('log', 0, 10, 'e', 16, size=2, ms=280, gap=4)
    lv.lane('log', 15, 11, 'w', 16, size=3, ms=340, gap=3)
    lv.lane('lily', 0, 12, 'e', 16, ms=520, gap=100)
    lv.lane('log', 0, 13, 'e', 16, size=3, ms=400, gap=3)
    lv.lane('log', 13, 18, 'w', 14, size=3, ms=360, gap=4)
    lv.lane('log', 0, 19, 'e', 14, size=2, ms=300, gap=4)
    lv.lane('lily', 0, 20, 'e', 14, ms=500, gap=700)
    lv.lane('log', 13, 21, 'w', 14, size=3, ms=380, gap=3)
    lv.lane('log', 0, 25, 'e', 16, size=3, ms=360, gap=3)
    lv.lane('log', 15, 26, 'w', 16, size=2, ms=320, gap=4)
    lv.monster('werewolf', 1, 14, 'e')
    lv.monster('werewolf', 14, 8, 'w')
    lv.monster('crow', 15, 16, 's')
    lv.monster('crow', 13, 7, 's')
    lv.monster('crow', 11, 22, 's')
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level3():
    lv = Level('forest_3', 'forest', 'The Old Mill', time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
gggggggggggggggg
gggggggpgggggggg
gggggggpgggggggg
ggggggpppggggggg
gggggppppppggggg
~~~~~~~..~~~~~~~
gggggggppggggggg
ggrrgggpgggggggg
grrrrggpggggrrgg
grrgggppgggrrrrg
ggggggpggggggrrg
gggggggggggggggg
gggggggggggggggg
gg~~~~~~~~~~~~gg
gg~~~~~~~~~~~~gg
gg~~~~~~~~~~~~gg
gg~~~~~~~~~~~~gg
ggggggpggggggggg
gggggggpggggggggg
""".replace('gggggggpggggggggg', 'gggggggpgggggggg') + """
wwwwwwwpgg~~~~~~
wwwwwwwpgg~~~~~~
ggggggppgggggggg
gggggppggggggggg
ggggpggggggggggg
~~~~~~~~~~~~~~~~
ggggggpggggggggg
ggggggpggggggggg
ggggggpggggggggg
gggggggpgggggggg
gggggggpgggggggg
""", """
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0011000000000000
0122100000001100
0110000000012210
0000000000000110
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
""")
    lv.place('i', [(x, 29) for x in range(0, 16, 2) if x != 6] + [(0, 28), (15, 28)])
    lv.place('a', [(x, 29) for x in range(1, 16, 2) if x != 7])
    lv.place('E', [(7, 28)])
    lv.place('W', [(8, 8)])
    lv.place('c', [(1, 1)])
    lv.place('n', [(6, 27), (8, 27), (5, 20), (9, 20), (5, 6), (9, 6), (6, 2), (9, 2)])
    lv.place('M', [(0, 18), (15, 17), (0, 11), (15, 11)])
    lv.place('i', [(3, 18), (12, 18), (14, 2), (13, 0), (11, 22), (3, 22)])
    lv.place('f', [(10, 21), (11, 21), (12, 21), (13, 21), (14, 21), (15, 21)])
    lv.place('u', [(4, 11), (11, 12), (1, 26), (14, 25)])
    lv.place('s', [(12, 6), (3, 6)])
    lv.place('K', [(2, 21), (1, 8), (13, 20), (14, 26), (1, 25)])
    lv.place('o', [(7, 1), (7, 2), (7, 6), (7, 7), (6, 9), (6, 17), (7, 18), (7, 19), (6, 22), (4, 23), (6, 25)])
    lv.place('*', [(15, 0)])
    lv.chest(0, 12, coins=15, bonus='heart')
    lv.place('T', [(15, 14)])
    lv.place('P', [(7, 11), (6, 23)])
    lv.place('S', [(7, 0)])
    # the millrace (row 24): a lever lays the plank bridge; or push the crate in
    lv.lever(5, 25, 1)
    for x in (7, 8):
        lv.group_cell(x, 24, 1)
    lv.place('C', [(3, 25)])
    # the stream by the gate is crossed on a log only
    lv.lane('log', 0, 5, 'e', 16, size=3, ms=380, gap=3)
    # the millpond: logs and lily pads
    lv.lane('log', 13, 13, 'w', 12, size=3, ms=400, gap=3)
    lv.lane('lily', 2, 14, 'e', 12, ms=500, gap=200)
    lv.lane('log', 2, 15, 'e', 12, size=2, ms=320, gap=3)
    lv.lane('log', 13, 16, 'w', 12, size=3, ms=380, gap=3)
    lv.lane('log', 10, 10, 'e', 6, size=2, ms=350, gap=2)
    lv.monster('werewolf', 1, 27, 'e')
    lv.monster('werewolf', 14, 18, 'w')
    lv.monster('werewolf', 3, 4, 'e')
    lv.monster('crow', 12, 22, 's')
    lv.monster('crow', 2, 11, 's')
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level4():
    lv = Level('forest_4', 'forest', 'Moon Clearing', time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
gggggggggggggggg
gggggggpgggggggg
gggggggpgggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
ggggggmmmmgggggg
gggggmmmmmmggggg
gggggmmmmmmggggg
ggggggmmmmgggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
gggggggggggggggg
ggggggggpggggggg
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
ggggggpggggggggg
gggggggpgggggggg
ggggggpggggggggg
gggggggpgggggggg
gggggggpgggggggg
gggggggpgggggggg
""", """
0000000000000000
0000000000000000
0000000000000000
1100000000000011
2100000000000012
1000000000000001
0000000000000000
0000000000000000
0000000000000000
0000001111000000
0000011221100000
0000011221100000
0000001111000000
0000000000000000
0000000000000000
0000000000000000
1000000000000001
2100000000000012
1100000000000011
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
""")
    lv.place('i', [(x, 29) for x in range(0, 16, 2) if x != 6] + [(0, 28), (15, 28)])
    lv.place('a', [(x, 29) for x in range(1, 16, 2) if x != 7])
    lv.place('E', [(7, 28)])
    lv.place('h', [(7, 18)])
    lv.place('n', [(6, 27), (8, 27), (3, 14), (12, 14), (3, 21), (12, 21), (5, 9), (9, 9)])
    lv.place('r', [(3, 23), (12, 23), (5, 16), (10, 16), (2, 11), (13, 11), (7, 12)])
    lv.place('M', [(0, 20), (15, 20), (0, 7), (15, 7)])
    lv.place('i', [(1, 2), (14, 1), (4, 5), (11, 5)])
    lv.place('K', [(0, 25), (15, 25), (0, 12), (15, 12), (7, 19)])
    lv.place('o', [(7, 1), (7, 2), (6, 3), (7, 4), (8, 5), (6, 14), (9, 14), (6, 22), (9, 22), (7, 25), (8, 25)])
    lv.place('*', [(8, 18)])
    lv.chest(1, 17, coins=20, bonus='heart')
    lv.chest(14, 17, coins=20, bonus='hourglass')
    lv.place('P', [(8, 9)])
    lv.place('S', [(7, 0)])
    lv.lane('log', 0, 6, 'e', 16, size=3, ms=380, gap=3)
    lv.lane('log', 15, 7, 'w', 16, size=3, ms=340, gap=3)
    lv.lane('lily', 0, 8, 'e', 16, ms=480, gap=500)
    # the alpha: he charges along rows and columns; the rocks stop him
    lv.monster('alpha', 6, 22, 's')
    lv.monster('werewolf', 1, 14, 'e')
    lv.monster('werewolf', 14, 24, 'w')
    lv.monster('crow', 4, 25, 's')
    lv.monster('crow', 11, 12, 's')
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def levels():
    return [level1(), level2(), level3(), level4()]
