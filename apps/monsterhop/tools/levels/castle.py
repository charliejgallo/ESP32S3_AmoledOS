"""Vampire Castle: the four levels of the violet zone.

Terrain letters: f flagstone, r carpet (runs along X), R carpet (runs
along Y), s stone block, w stone with arched windows (on every floor),
M battlement, d wood floor, u rug, ~ moat, # / | plank bridges.
Props: n banner, k bookshelf (X), j bookshelf (Y), c candelabra (lit),
h chandelier stand (lit), q clock, x coffin, g gargoyle, i iron fence (X),
I iron fence (Y), p pillar, r roses, t throne.
"""
from levels import Level


def zone(lv):
    z = 'castle'
    lv.legend = {
        'f': {'top': z + '_blk_flagstone', 'fill': z + '_fill_rock'},
        'r': {'top': z + '_blk_carpet', 'fill': z + '_fill_stone'},
        'R': {'top': z + '_blk_carpet_y', 'fill': z + '_fill_stone'},
        's': {'top': z + '_blk_stone', 'fill': z + '_fill_stone'},
        'w': {'top': z + '_blk_stone', 'fill': z + '_blk_stone_win'},
        'M': {'top': z + '_blk_battlement', 'fill': z + '_fill_stone'},
        'd': {'top': z + '_blk_woodfloor', 'fill': z + '_fill_stone'},
        'u': {'top': z + '_blk_rug', 'fill': z + '_fill_stone'},
    }
    lv.props = {
        'n': {'name': z + '_banner'}, 'k': {'name': z + '_bookshelf_x'}, 'j': {'name': z + '_bookshelf_y'},
        'c': {'name': z + '_candelabra'}, 'h': {'name': z + '_chandelier_stand'}, 'q': {'name': z + '_clock'},
        'x': {'name': z + '_coffin'}, 'g': {'name': z + '_gargoyle'}, 'i': {'name': z + '_ironfence_x'},
        'I': {'name': z + '_ironfence_y'}, 'p': {'name': z + '_pillar'}, 'r': {'name': z + '_roses'},
        't': {'name': z + '_throne'},
    }
    lv.surf = z + '_surf_moat'
    lv.deck = z + '_bridge_x'
    lv.deck_y = z + '_bridge_y'
    lv.crate = z + '_crate'
    lv.preview_assets = ['tiles_castle', 'objects', 'chars', 'monsters']
    lv.preview_bg = [20, 12, 30]


def level1():
    lv = Level('castle_1', 'castle', 'The Moat', time_s=230, par_s=140)
    zone(lv)
    lv.map2("""
wwwwwwwwwwwwwwww
wwwwwwwfwwwwwwww
ffffffffffffffff
ffffffffffffffff
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
ffffffffffffffff
ffffffffffffffff
ffffffffffffssff
ffffffffffffssff
ffffffffffffffff
~~~~~~~~~~~~~~~|
~~~~~~~~~~~~~~~|
~~~~~~~~~~~~~~~|
~~~~~~~~~~~~~~~|
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffssffffffffffff
ffffffffffffffff
~~~|~~~~~~~~|~~~
~~~|~~~~~~~~|~~~
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
""", """
3333333333333333
3333333033333333
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000001200
0000000000001100
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0011000000000000
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
    lv.place('g', [(6, 28), (8, 28)])
    lv.place('c', [(0, 27), (15, 27), (0, 14), (15, 14)])
    lv.place('i', [(0, 23), (1, 23), (2, 23), (3, 23), (11, 23), (12, 23), (13, 23)])
    lv.place('r', [(1, 19), (5, 21), (10, 19), (4, 13), (1, 13), (1, 7), (14, 7), (5, 2), (10, 2)])
    lv.place('h', [(0, 0), (15, 0)])
    lv.place('K', [(1, 26), (14, 27), (0, 20), (15, 12), (2, 4)])
    lv.place('o', [(4, 26), (11, 26), (6, 22), (9, 22), (3, 11), (8, 12), (12, 13), (3, 3), (12, 3), (7, 5),
                   (8, 5)])
    lv.place('*', [(13, 21)])
    lv.place('H', [(15, 22)])
    lv.place('T', [(0, 10)])
    lv.place('P', [(7, 11), (7, 20)])
    lv.place('S', [(7, 1)])
    lv.chest(3, 14, coins=15)
    # the drawbridge comes down with the lever by the fence
    lv.lever(14, 23, 1)
    for y in (24, 25):
        lv.group_cell(7, y, 1)
    # the wide moat: two rafts per row, and a plank bridge at the east end
    for y, ms in ((15, 430), (16, 380), (17, 470), (18, 400)):
        lv.platform([(0, y), (6, y)], ms=ms)
        lv.platform([(8, y), (14, y)], ms=ms + 60)
    lv.lane('bat', 0, 16, 'e', 16, ms=210, gap=9)
    lv.lane('bat', 15, 11, 'w', 16, ms=240, gap=8)
    lv.monster('vampire', 1, 22, 'e', path=[(1, 22), (14, 22)], ms=700, param=5000, pingpong=True)
    lv.monster('vampire', 2, 12, 'e', path=[(2, 12), (13, 12), (13, 10), (2, 10)], ms=650, param=6000)
    lv.monster('vampire', 14, 5, 'w', path=[(14, 5), (1, 5)], ms=750, param=5500, pingpong=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level2():
    lv = Level('castle_2', 'castle', 'The Great Hall', time_s=230, par_s=140)
    zone(lv)
    lv.map2("""
wwwwwwwwwwwwwwww
wwwwwwwRwwwwwwww
fffffffRffffffff
dddffffRfffffddd
dddffffRfffffddd
dddffffRfffffddd
fffffffRffffffff
frrrrrruurrrrrrf
fffffffRffffffff
fffffffRffffffff
fffffffRffffffff
ssfssssRsssssfss
fffffffRffffffff
fffffffRfffffddd
fffffffRfffffddd
frrrrrruurrrrrrf
fffffffRffffffff
dddffffRffffffff
dddffffRffffffff
ssfssssRsssssfss
fffffffRffffffff
fffffffRffffffff
fffffffRffffffff
frrrrrruurrrrrrf
fffffffRffffffff
fffffffRffffffff
fffffffRffffffff
fffffffRffffffff
fffffffRffffffff
fffffffRffffffff
""", """
3333333333333333
3333333033333333
0000000000000000
1110000000000111
1110000000000111
1110000000000111
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
3303333033333033
0000000000000000
0000000000000111
0000000000000111
0000000000000000
0000000000000000
1110000000000000
1110000000000000
3303333033333033
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
    lv.place('n', [(5, 28), (9, 28)])
    lv.place('k', [(1, 27), (2, 27), (13, 27)])
    lv.place('j', [(0, 21), (15, 13), (0, 5)])
    lv.place('h', [(4, 24), (10, 24), (4, 16), (10, 16), (4, 8), (10, 8)])
    lv.place('p', [(5, 20), (9, 20), (5, 12), (9, 12), (5, 3), (9, 3)])
    lv.place('c', [(0, 27), (15, 20), (0, 13), (15, 5)])
    lv.place('q', [(15, 27)])
    lv.place('K', [(1, 25), (14, 16), (0, 12), (15, 4), (12, 26)])
    lv.place('o', [(3, 22), (11, 22), (7, 24), (7, 16), (3, 14), (11, 14), (6, 13), (8, 6), (3, 6), (12, 6),
                   (7, 2), (7, 3)])
    lv.place('*', [(14, 26)])
    lv.place('H', [(1, 11)])
    lv.place('T', [(14, 25)])
    lv.place('P', [(7, 11), (7, 19)])
    lv.place('S', [(7, 0)])
    lv.chest(1, 1, coins=15, bonus='hourglass')
    # the doorways in the two walls bite
    for x, y, ph in ((2, 18, 0), (7, 18, 700), (13, 18, 1400), (2, 10, 350), (7, 10, 1050), (13, 10, 0)):
        lv.trap('spikes', x, y, period=2100, phase=ph)
    for x, y, ph in ((4, 14, 0), (10, 14, 1000), (4, 22, 500), (10, 22, 1500)):
        lv.trap('spikes', x, y, period=2000, phase=ph)
    # suits of armour: the ones on the carpets march, the ones by the pillars
    # only look like they might
    lv.monster('armor', 1, 22, 'e', path=[(1, 22), (14, 22)], ms=650, param=700, pingpong=True)
    lv.monster('armor', 14, 14, 'w', path=[(14, 14), (1, 14)], ms=600, param=600, pingpong=True)
    lv.monster('armor', 1, 6, 'e', path=[(1, 6), (14, 6)], ms=650, param=800, pingpong=True)
    lv.monster('armor', 6, 20, 's')
    lv.monster('armor', 8, 12, 's')
    lv.monster('armor', 6, 3, 's')
    lv.monster('vampire', 3, 26, 'e', path=[(3, 26), (11, 26)], ms=700, param=5000, pingpong=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level3():
    lv = Level('castle_3', 'castle', 'The Clock Tower', time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
MMMMMMMMMMMMMMMM
MMMMMMMdMMMMMMMM
dddddddddddddddd
dddddddddddddddd
................
................
MddddddddddddddM
MddddddddddddddM
MddddddddddddddM
MddddddddddddddM
MddddddddddddddM
MddddddddddddddM
................
................
MssssssssssssssM
MssssssssssssssM
MssssssssssssssM
MssssssssssssssM
MssssssssssssssM
MssssssssssssssM
................
................
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
""", """
3333333333333333
3333333233333333
2222222222222222
2222222222222222
2222222222222222
2222222222222222
3222222222222223
3222222222222223
3222222222222223
3222222222222223
3222222222222223
3222222222222223
1111111111111111
1111111111111111
2111111111111112
2111111111111112
2111111111111112
2111111111111112
2111111111111112
2111111111111112
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
    lv.place('g', [(0, 28), (15, 28)])
    lv.place('q', [(4, 22), (11, 22)])
    lv.place('c', [(1, 23), (14, 23), (1, 15), (14, 15)])
    lv.place('n', [(3, 27), (12, 27)])
    lv.place('r', [(0, 0), (15, 0), (3, 6), (12, 6)])
    lv.place('K', [(1, 26), (14, 21), (1, 13), (14, 12), (2, 3)])
    lv.place('o', [(5, 27), (10, 27), (6, 19), (9, 19), (7, 21), (4, 14), (11, 14), (7, 11), (5, 6), (10, 6),
                   (7, 4), (8, 4)])
    lv.place('*', [(15, 19)])
    lv.place('H', [(13, 26)])
    lv.place('T', [(1, 18)])
    lv.place('P', [(7, 10), (7, 18)])
    lv.place('S', [(7, 1)])
    lv.chest(14, 3, coins=15, bonus='heart')
    # the tower's three shafts: rafts at the bottom, sliding slabs in the
    # middle, and at the top the slabs only run once the lever is thrown
    lv.platform([(0, 8), (6, 8)], ms=420, floor=0)
    lv.platform([(9, 8), (15, 8)], ms=460, floor=0)
    lv.platform([(15, 9), (9, 9)], ms=400, floor=0)
    lv.platform([(6, 9), (0, 9)], ms=440, floor=0)
    lv.platform([(1, 16), (14, 16)], ms=380, floor=1)
    lv.platform([(14, 17), (1, 17)], ms=340, floor=1)
    lv.platform([(3, 24), (3, 25)], ms=650, group=1, floor=2)
    lv.platform([(12, 24), (12, 25)], ms=650, group=1, floor=2)
    lv.lever(8, 22, 1)
    lv.lane('bat', 0, 20, 'e', 16, ms=200, gap=8, z=2)
    lv.lane('bat', 15, 13, 'w', 16, ms=230, gap=9, z=1)
    lv.monster('vampire', 2, 19, 'e', path=[(2, 19), (13, 19)], ms=700, param=5000, pingpong=True)
    lv.monster('vampire', 2, 12, 'e', path=[(2, 12), (6, 12), (6, 14), (2, 14)], ms=650, param=6000)
    lv.monster('vampire', 13, 11, 'w', path=[(13, 11), (9, 11), (9, 14), (13, 14)], ms=650, param=5500)
    lv.monster('vampire', 14, 5, 'w', path=[(14, 5), (1, 5)], ms=750, param=5000, pingpong=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1600]
    lv.preview_look = [8, 15, 1]
    return lv


def level4():
    lv = Level('castle_4', 'castle', "The Count's Chamber", time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
wwwwwwwwwwwwwwww
wwwwwwwuwwwwwwww
ffffffffffffffff
ffffffuuuuffffff
ffffffuuuuffffff
ffffffffffffffff
ffffffffffffffff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
fffuuuuuuuuuufff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
ffffffffffffffff
rrrrrrrRRrrrrrrr
fffffffRRfffffff
fffffffRRfffffff
fffffffRRfffffff
fffffffRRfffffff
""", """
3333333333333333
3333333033333333
0000000000000000
0000001111000000
0000001111000000
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
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
""")
    lv.place('E', [(7, 28)])
    lv.place('t', [(8, 26)])
    lv.place('c', [(5, 26), (10, 26), (0, 27), (15, 27)])
    lv.place('n', [(3, 28), (12, 28)])
    lv.place('p', [(2, 20), (13, 20), (2, 14), (13, 14), (2, 8), (13, 8)])
    lv.place('x', [(5, 22), (10, 22), (5, 6), (10, 6)])
    lv.place('h', [(0, 17), (15, 17), (0, 5), (15, 5)])
    lv.place('j', [(0, 2), (15, 2)])
    lv.place('K', [(0, 24), (15, 24), (0, 11), (15, 11), (7, 15)])
    lv.place('o', [(3, 23), (12, 23), (6, 21), (9, 21), (4, 12), (11, 12), (4, 17), (11, 17), (7, 7), (8, 7),
                   (1, 1), (14, 1)])
    lv.place('*', [(6, 25)])
    lv.place('P', [(7, 4), (7, 21)])
    lv.place('S', [(7, 0)])
    lv.chest(1, 26, coins=15, bonus='heart')
    lv.chest(14, 26, coins=15, bonus='hourglass')
    for x, y, ph in ((4, 22, 0), (11, 22, 1000), (4, 6, 500), (11, 6, 1500)):
        lv.trap('spikes', x, y, period=2000, phase=ph)
    # the Count paces round his rug and throws out a ring of bats
    lv.monster('count', 3, 10, 'e', path=[(3, 10), (12, 10), (12, 19), (3, 19)], ms=520, param=3500)
    lv.monster('vampire', 1, 23, 'e', path=[(1, 23), (14, 23)], ms=700, param=5000, pingpong=True)
    lv.monster('vampire', 14, 7, 'w', path=[(14, 7), (1, 7)], ms=700, param=5500, pingpong=True)
    lv.lane('bat', 0, 3, 'e', 16, ms=220, gap=8)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def levels():
    return [level1(), level2(), level3(), level4()]
