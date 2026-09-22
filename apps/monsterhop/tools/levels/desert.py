"""Mummy Desert: the four levels of the brown zone.

Terrain letters: s sand, t sandstone, c cracked sandstone, b pyramid brick,
H brick with hieroglyph panels (on every floor), o oasis grass, G gold floor,
~ oasis water, % quicksand, # reed bridge.
Props: p palm, c cactus, k small cactus, O obelisk (3 m), s pharaoh statue,
u urn, x sphinx (2x2), z brazier (lit), n lotus column, a scarab statue,
r rock, t tent (2x2), m camel (2x1).
"""
from levels import Level


def zone(lv):
    z = 'desert'
    lv.legend = {
        's': {'top': z + '_blk_sand', 'fill': z + '_fill_sand'},
        't': {'top': z + '_blk_sandstone', 'fill': z + '_fill_sandstone'},
        'c': {'top': z + '_blk_cracked', 'fill': z + '_fill_sandstone'},
        'b': {'top': z + '_blk_brick', 'fill': z + '_fill_brick'},
        'H': {'top': z + '_blk_brick', 'fill': z + '_blk_brick_win'},
        'o': {'top': z + '_blk_oasis', 'fill': z + '_fill_sand'},
        'G': {'top': z + '_blk_gold', 'fill': z + '_fill_sandstone'},
        'D': {'top': z + '_blk_dartwall', 'fill': z + '_fill_brick'},
    }
    lv.props = {
        'p': {'name': z + '_palm'}, 'c': {'name': z + '_cactus'}, 'k': {'name': z + '_cactus_small'},
        'O': {'name': z + '_obelisk'}, 's': {'name': z + '_statue'}, 'u': {'name': z + '_urn'},
        'x': {'name': z + '_sphinx', 'fp': (2, 2)}, 'z': {'name': z + '_brazier'}, 'n': {'name': z + '_column'},
        'a': {'name': z + '_scarab'}, 'r': {'name': z + '_rock'}, 't': {'name': z + '_tent', 'fp': (2, 2)},
        'm': {'name': z + '_camel', 'fp': (2, 1)},
    }
    lv.surf = z + '_surf_water'
    lv.quick = z + '_surf_quicksand'
    lv.deck = z + '_bridge_x'
    lv.deck_y = z + '_bridge_y'
    lv.crate = z + '_crate'
    lv.preview_assets = ['tiles_desert', 'objects', 'chars', 'monsters']
    lv.preview_bg = [40, 26, 16]


def level1():
    lv = Level('desert_1', 'desert', 'Dune Sea', time_s=210, par_s=120)
    zone(lv)
    lv.terrain('''
b3b3b3b3b3b3b3b3b3b3b3b3b3b3b3b3
H3H3b3H3H3b3H3t0t0H3b3H3H3b3H3H3
t0t0t0t0t0t1t1t0t0t1t1t0t0t0t0t0
t0t1t1t0t0t0t0t0t0t0t0t0t0t1t1t0
t0t1t2t0t0t0t0t0t0t0t0t0t0t2t1t0
t0t0t0t0t0t0t0t0t0t0t0t0t0t0t0t0
t0t0t0t0t0t0t0t0t0t0t0t0t0t0t0t0
s0s0t0t0t0t0t0t0t0t0t0t0t0t0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s1s1s0s0s0s0s1s1s0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0%0%0%0s0s0s0s0s0%0%0%0s0s0s0
s0%0%0t0%0%0%0t0%0%0%0%0t0%0%0s0
s0%0t0t0t0%0%0t1t1%0%0t0t0t0%0s0
s0%0%0%0t0%0%0t0%0%0%0t0%0%0%0s0
s0s0s0%0t0t0t0t0%0t0t0t0%0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0o0o0o0o0o0o0o0o0o0o0o0s0s0s0
s0o0o0~0~0~0~0|0~0~0~0o0o0o0s0s0
s0o0~0~0~0~0~0|0~0~0~0~0o0o0s0s0
s0o0~0~0~0~0~0|0~0~0~0~0o0o0s0s0
s0s0o0o0o0o0o0o0o0o0o0o0o0o0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s1s1s0s0s1s2s2s1s0s0s1s1s0s0s1s1
s1s2s1s0s0s1s2s1s0s1s2s2s1s0s0s1
s0s1s0s0%0s0s1s0%0s0s1s1s0%0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
''')
    lv.place('p', [(0, 0), (15, 0), (0, 21), (2, 22), (12, 22), (14, 18)])
    lv.place('c', [(0, 3), (15, 3), (3, 5), (13, 7)])
    lv.place('k', [(8, 1), (4, 16), (11, 16)])
    lv.place('O', [(0, 22), (15, 22)])
    lv.place('n', [(0, 24), (3, 24), (11, 24), (14, 24), (8, 28)])
    lv.place('s', [(2, 25), (13, 25)])
    lv.place('z', [(0, 27), (15, 27), (6, 27), (9, 27)])
    lv.place('r', [(12, 1), (3, 9)])
    lv.place('u', [(5, 25), (10, 25)])
    lv.place('K', [(6, 4), (13, 10), (7, 16), (1, 26), (14, 26)])
    lv.place('o', [(1, 6), (3, 6 + 0), (5, 6), (7, 6), (9, 6), (11, 6), (13, 6), (4, 14), (11, 14),
                   (3, 17), (7, 17), (12, 17), (4, 20), (11, 20), (6, 23), (8, 23)])
    lv.place('P', [(7, 13), (7, 22)])
    lv.place('T', [(14, 11)])
    lv.place('*', [(15, 25)])
    lv.place('E', [(7, 28)])
    lv.place('S', [(7, 1)])
    lv.monster('mummy', 1, 6, 'e', path=[(1, 6), (14, 6)], ms=850, pingpong=True)
    lv.monster('mummy', 4, 23, 'e', path=[(4, 23), (10, 23), (10, 24), (4, 24)], ms=800)
    lv.monster('mummy', 3, 22, 'e', path=[(3, 22), (11, 22)], ms=700, pingpong=True)
    lv.lane('scarab', 0, 7, 'e', 16, size=3, ms=190, gap=5)
    lv.lane('scarab', 0, 19, 'e', 16, size=3, ms=170, gap=6)
    lv.lane('scarab', 15, 21, 'w', 16, size=2, ms=150, gap=5)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv



def level2():
    lv = Level('desert_2', 'desert', 'The Oasis', time_s=220, par_s=130)
    zone(lv)
    lv.map2("""
HHbHHbHttHbHHbHH
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
sstttttttttttss
""".replace('sstttttttttttss', 'ssttttttttttttss') + """
Dsssssssssssssss
ssssssssssssssss
sssssssssssssssD
ssssssssssssssss
ss%%ssssss%%%sss
s%%%ssss%%%%ssss
s%%ssss%%%%sssss
ssss%%sssss%%sss
ssssssssssssssss
oooooooooooooooo
oo~|~~~~~~~~~~oo
o~~|~~~~~oooo~~o
o~~|~~~~~oooo~~o
oo~|~~~~~~~~~~oo
oooooooooooooooo
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
""", """
3333333003333333
0000000000000000
0000011111100000
0000012222100000
0000011111100000
0000000000000000
0000000000000000
0000000000000000
1000000000000000
0000000000000000
0000000000000001
0000000000000000
0000011000000000
0000122100000110
0000011000001221
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
""")
    lv.place('E', [(7, 29)])
    lv.place('n', [(8, 29), (1, 24), (4, 24), (11, 24), (14, 24)])
    lv.place('z', [(5, 28), (10, 28), (0, 26), (15, 26)])
    lv.place('s', [(2, 27), (13, 27)])
    lv.place('p', [(0, 7), (15, 7), (12, 9), (0, 18), (15, 18)])
    lv.place('c', [(2, 4), (5, 4), (9, 4), (12, 4)])
    lv.place('k', [(0, 1), (15, 1), (8, 14)])
    lv.place('m', [(10, 1)])
    lv.place('K', [(7, 26), (10, 10), (6, 16), (14, 4), (1, 21)])
    lv.place('o', [(3, 8), (3, 9), (3, 10), (3, 11), (7, 4), (4, 4), (11, 4), (13, 15), (6, 15), (2, 19), (13, 19),
                   (7, 23), (8, 23), (7, 25), (8, 25)])
    lv.place('*', [(15, 28)])
    lv.chest(11, 10, coins=15)
    lv.place('T', [(3, 14)])
    lv.place('H', [(0, 12)])
    lv.place('P', [(7, 12), (7, 22)])
    lv.place('S', [(7, 1)])
    lv.lane('scarab', 0, 3, 'e', 16, size=3, ms=180, gap=5)
    lv.lane('scarab', 15, 5, 'w', 16, size=2, ms=160, gap=5)
    lv.lane('scarab', 15, 20, 'w', 16, size=3, ms=150, gap=5)
    lv.trap('darts', 0, 21, 'e', period=1800)
    lv.trap('darts', 15, 19, 'w', period=2000, phase=900)
    lv.monster('mummy', 0, 12, 'e', path=[(0, 12), (15, 12)], ms=750, pingpong=True)
    lv.monster('mummy', 2, 13, 'e', path=[(2, 13), (13, 13)], ms=800, pingpong=True)
    lv.monster('mummy', 2, 22, 'e', path=[(2, 22), (13, 22), (13, 23), (2, 23)], ms=700, param=5000)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level3():
    lv = Level('desert_3', 'desert', 'Temple Halls', time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
bbbbbbbbbbbbbbbb
bHHbbHHttHHbbHHb
DttttttttttttttD
bttttttttttttttb
bttttttttttttttD
bbbbbbbttbbbbbbb
bGGGGGGGGGGGGGGb
bGGGGGGGGGGGGGGb
bGGGGGGGGGGGGGGb
bGGGGGGGGGGGGGGb
bGGGGGGGGGGGGGGb
bbbbbbbttbbbbbbb
bttttttttttttttb
bttttttttttttttb
b..............b
bttttttttttttttb
bttttttttttttttb
bbbbbbccccbbbbbb
tttttbccccbtt.tt
Dttttbccccbtt.tt
tttttbccccbtt.tt
tttttbccccbtt.tt
tttttbccccbtt.tt
ttbbbbccccbttbbb
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
tttttttttttttttt
""", """
3333333333333333
3333333003333333
1000000000000001
3000000000000003
3000000000000001
3333333003333333
3000000000000003
3000000220000003
3000000110000003
3000000000000003
3000000000000003
3333333003333333
3000000000000003
3000000000000003
3000000000000003
3000000000000003
3000000000000003
3333330000333333
0000030000300000
1000030000300000
0000030000300000
0000030000300000
0000030000300000
0033330000300333
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
""")
    lv.place('E', [(7, 28)])
    lv.place('n', [(8, 28), (2, 3), (5, 3), (10, 3), (13, 3)])
    lv.place('z', [(0, 0), (15, 0), (1, 12), (14, 12), (1, 19), (14, 19)])
    lv.place('s', [(1, 25), (14, 25)])
    lv.place('u', [(1, 23), (14, 23), (1, 17), (14, 17)])
    lv.place('K', [(1, 9), (14, 9), (7, 22), (14, 16), (2, 26)])
    lv.place('o', [(7, 7), (8, 8), (7, 9), (8, 10), (7, 11), (3, 13), (12, 13), (4, 20), (11, 20), (7, 26), (8, 26),
                   (3, 8), (3, 10)])
    lv.place('*', [(1, 22)])
    lv.chest(13, 23, coins=20, bonus='heart')
    lv.place('T', [(1, 5)])
    lv.place('P', [(7, 13), (7, 19)])
    lv.place('S', [(7, 1)])
    lv.place('C', [(12, 9)])
    # the chasm in the middle hall: a lever lays a bridge over four of its cells
    lv.lever(1, 13, 1)
    for x in (6, 7, 8, 9):
        lv.group_cell(x, 15, 1)
    lv.lane('boulder', 7, 12, 's', 6, ms=260, gap=4)
    lv.lane('boulder', 8, 12, 's', 6, ms=300, gap=5)
    lv.lane('scarab', 1, 20, 'e', 14, size=2, ms=170, gap=5)
    lv.trap('darts', 0, 10, 'e', period=1700)
    lv.trap('darts', 0, 27, 'e', period=1600)
    lv.trap('darts', 15, 27, 'w', period=1600, phase=800)
    lv.trap('darts', 15, 25, 'w', period=1900, phase=400)
    lv.monster('mummy', 1, 14, 'e', path=[(1, 14), (14, 14)], ms=800, pingpong=True)
    lv.monster('mummy', 14, 16, 'w', path=[(14, 16), (1, 16)], ms=700, pingpong=True)
    lv.monster('mummy', 3, 19, 'e', path=[(3, 19), (12, 19), (12, 23), (3, 23)], ms=650)
    lv.monster('mummy', 1, 7, 'n', path=[(1, 7), (4, 7), (4, 11), (1, 11)], ms=800)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level4():
    lv = Level('desert_4', 'desert', "Pharaoh's Tomb", time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
bbbbbbbbbbbbbbbb
HHHHHHHttHHHHHHH
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGG
tttttttttttttttt
%%%t%%%%%%%%t%%%
%%%t%%%%%%%%t%%%
tttttttttttttttt
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
""", """
3333333333333333
3333333003333333
2200000000000022
2100000000000012
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
2100000000000012
2200000000000022
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
    lv.place('E', [(7, 28)])
    lv.place('n', [(8, 28), (4, 25), (11, 25), (4, 18), (11, 18)])
    lv.place('z', [(3, 27), (12, 27), (3, 15), (12, 15), (6, 12), (9, 12)])
    lv.place('s', [(6, 27), (9, 27)])
    lv.place('p', [(0, 0), (15, 0), (0, 8), (15, 8)])
    lv.place('O', [(2, 5), (13, 5)])
    lv.place('K', [(0, 27), (15, 27), (0, 14), (15, 14), (7, 20)])
    lv.place('o', [(3, 11), (3, 12), (12, 11), (12, 12), (5, 21), (10, 21), (5, 23), (10, 23), (7, 16), (8, 16)])
    lv.place('*', [(8, 21)])
    lv.chest(1, 18, coins=15, bonus='hourglass')
    lv.chest(14, 18, coins=15, bonus='heart')
    lv.place('P', [(7, 13)])
    lv.place('S', [(7, 1)])
    lv.lane('scarab', 0, 3, 'e', 16, size=3, ms=160, gap=4)
    lv.lane('scarab', 15, 6, 'w', 16, size=3, ms=150, gap=4)
    lv.lane('scarab', 0, 9, 'e', 16, size=2, ms=140, gap=4)
    # the pharaoh walks a loop round the tomb and lashes out every few seconds
    lv.monster('pharaoh', 2, 17, 'e', path=[(2, 17), (12, 17), (12, 24), (2, 24)], ms=520, param=4500)
    lv.monster('mummy', 1, 16, 'e', path=[(1, 16), (14, 16)], ms=750, pingpong=True)
    lv.monster('mummy', 14, 26, 'w', path=[(14, 26), (1, 26)], ms=700, pingpong=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def levels():
    return [level1(), level2(), level3(), level4()]
