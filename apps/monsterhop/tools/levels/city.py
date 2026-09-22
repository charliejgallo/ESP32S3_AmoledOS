"""Zombie Town: the four levels of the grey zone.

Terrain letters (two characters per cell: letter + floor):
  a asphalt   l asphalt with a lane line   c crosswalk   s sidewalk
  o concrete  g dead grass   j scrap   b brick wall   B building (roof on
  brick)   W building with windows   X boarded-up building   ~ sewer water   # a grate bridge
Things (one character): the common ones (K key, o coin, P checkpoint,
E exit, S start, H heart, T hourglass, B chest, C crate, L lever) and
props: l lamp, h hydrant, d dumpster, x car along X (2 cells), y car along
Y (2 cells), t trash can, n cone, r barricade, u bus stop, m mailbox,
e dead tree, b bench, q phone booth, i tyres, w news box, f fence (X),
F fence (Y), k street sign, R barricade (Y), v bench (Y).
"""
from levels import Level


def zone(lv):
    z = 'city'
    lv.legend = {
        'a': {'top': z + '_blk_asphalt', 'fill': z + '_fill_earth'},
        'l': {'top': z + '_blk_asphalt_line', 'fill': z + '_fill_earth'},
        'c': {'top': z + '_blk_crosswalk', 'fill': z + '_fill_earth'},
        's': {'top': z + '_blk_sidewalk', 'fill': z + '_fill_concrete'},
        'o': {'top': z + '_blk_concrete', 'fill': z + '_fill_concrete'},
        'g': {'top': z + '_blk_grass', 'fill': z + '_fill_earth'},
        'j': {'top': z + '_blk_scrap', 'fill': z + '_fill_earth'},
        'b': {'top': z + '_blk_brick', 'fill': z + '_fill_brick'},
        'B': {'top': z + '_blk_roof', 'fill': z + '_fill_brick'},
        'W': {'top': z + '_blk_roof', 'fill': z + '_blk_brick_win'},
        'X': {'top': z + '_blk_roof', 'fill': z + '_blk_brick_boarded'},
    }
    lv.props = {
        'l': {'name': z + '_lamp'}, 'h': {'name': z + '_hydrant'}, 'd': {'name': z + '_dumpster'},
        'x': {'name': z + '_car_x', 'fp': (2, 1)}, 'y': {'name': z + '_car_y', 'fp': (1, 2)},
        't': {'name': z + '_trashcan'}, 'n': {'name': z + '_cone'}, 'r': {'name': z + '_barricade_x'},
        'u': {'name': z + '_busstop'}, 'm': {'name': z + '_mailbox'}, 'e': {'name': z + '_deadtree'},
        'b': {'name': z + '_bench_x'}, 'q': {'name': z + '_phonebooth'}, 'i': {'name': z + '_tires'},
        'w': {'name': z + '_newsbox'}, 'f': {'name': z + '_fence_x'}, 'F': {'name': z + '_fence_y'},
        'k': {'name': z + '_sign_x'}, 'R': {'name': z + '_barricade_y'}, 'v': {'name': z + '_bench_y'},
    }
    lv.surf = z + '_surf_sewer'
    lv.deck = z + '_bridge_x'
    lv.deck_y = z + '_bridge_y'
    lv.crate = z + '_crate'
    lv.preview_assets = ['tiles_city', 'objects', 'chars', 'monsters']
    lv.preview_bg = [14, 16, 22]


def level1():
    lv = Level('city_1', 'city', 'Main Street', time_s=200, par_s=110)
    zone(lv)
    lv.terrain('''
g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0
g0g1g1g0g0g0g0g0g0g0g0g0g1g1g0g0
g0g1g0g0g0g0g0g0g0g0g0g0g0g1g0g0
g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
~0~0~0|0~0~0~0~0~0~0~0~0|0~0~0~0
~0~0~0|0~0~0~0~0~0~0~0~0|0~0~0~0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
a0a0a0a0a0a0a0c0c0a0a0a0a0a0a0a0
l0l0l0l0l0l0l0c0c0l0l0l0l0l0l0l0
a0a0a0a0a0a0a0c0c0a0a0a0a0a0a0a0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s0o1s0s0s0s0s0s0s0s0s0s0
W3W3W3W3W3o2a0a0a0a0a0W2W2W2W2W2
W3W3W3W3W3b1a0a0a0a0a0W2W2W2W2W2
W3W3W3W3W3a0a0a0a0a0a0W2W2W2W2W2
W3W3W3W3W3a0a0a0a0a0a0W2W2W2W2W2
W3W3W3W3W3a0a0a0a0a0a0W2W2W2W2W2
b1b1a0a0a0a0a0a0a0a0a0a0a0b1b1b1
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
a0a0a0a0a0a0a0c0c0a0a0a0a0a0a0a0
l0l0l0l0l0l0l0c0c0l0l0l0l0l0l0l0
a0a0a0a0a0a0a0c0c0a0a0a0a0a0a0a0
s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0s0
g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0g0
''')
    lv.things('''
ffffffffffffffff
fffffffEffffffff
l.......o......l
................
.eK.....b.....e.
................
....o.o.o.o.H...
b..............b
.......P.......K
................
................
t..l....o...l..h
................
.......o........
................
.d..h.......d..K
....o.o..u....l.
.K.............C
.....o..o.......
.....h..d.......
........K.T.....
.....o..........
.......o..o.....
l...........h..l
.......P........
.......o........
.......o........
.......o........
l......S.......l
e..............e
''')
    # the traffic: runaway cars on the two avenues (lanes along X)
    lv.lane('car', 0, 2, 'e', 16, size=2, ms=240, gap=6)
    lv.lane('car', 15, 4, 'w', 16, size=2, ms=300, gap=7)
    lv.lane('car', 0, 17, 'e', 16, size=2, ms=210, gap=5)
    lv.lane('car', 15, 19, 'w', 16, size=2, ms=260, gap=6)
    # the zombies
    lv.monster('zombie', 1, 5, 'e', path=[(1, 5), (14, 5)], ms=750, pingpong=True)
    lv.monster('zombie', 12, 15, 'w', path=[(12, 15), (2, 15)], ms=650, pingpong=True, notice=True)
    lv.monster('zombie', 2, 23, 'e', path=[(2, 23), (6, 23), (6, 25), (2, 25)], ms=700)
    lv.monster('zombie', 10, 22, 'n', path=[(10, 22), (10, 26), (13, 26), (13, 22)], ms=650, notice=True)
    lv.monster('zombie', 7, 21, 'e', path=[(1, 21), (14, 21)], ms=600, pingpong=True)
    lv.monster('zombiedog', 1, 27, 'e', path=[(1, 27), (14, 27)], ms=170, param=900, pingpong=True)
    lv.place('*', [(1, 18)])
    lv.exit_dir = 2
    lv.start = (7, 1, 0)
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level2():
    lv = Level('city_2', 'city', 'The Sewers', time_s=220, par_s=130)
    zone(lv)
    lv.map2("""
bbbbbbbbbbbbbbbb
bbbbbbbobbbbbbbb
oooooooooooooooo
oooooooooooooooo
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
oooooooooooooooo
oooooooooooooooo
oooooooooooooooo
~~~|~~~~~~~~|~~~
~~~|~~~~~~~~|~~~
oooooooooooooooo
oobboooooooobboo
oobboooooooobboo
oooooooooooooooo
oooooobbbboooooo
oooooobbbboooooo
oooooooooooooooo
~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~
oooooooooooooooo
oooooooooooooooo
jjoooooooooooojj
oooooooooooooooo
oooooooooooooooo
oooooooooooooooo
oooooooooooooooo
oooooooooooooooo
oooooooooooooooo
oooooooooooooooo
""", """
3333333333333333
3333333033333333
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
0022000000002200
0022000000002200
0000000000000000
0000001221000000
0000001221000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
1100000000000011
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
""")
    lv.place('E', [(7, 28)])
    lv.place('l', [(0, 26), (15, 26), (0, 18), (15, 18), (0, 9), (15, 9)])
    lv.place('t', [(4, 27), (11, 27), (5, 21), (10, 21)])
    lv.place('d', [(1, 2)])
    lv.place('i', [(5, 2), (11, 4), (13, 0)])
    lv.place('r', [(14, 12)])
    lv.place('K', [(1, 22), (14, 15), (7, 14), (0, 4), (13, 27)])
    lv.place('o', [(3, 22), (5, 22), (10, 22), (12, 22), (8, 14), (3, 15), (12, 15), (1, 12), (7, 6), (7, 4),
                   (8, 8), (2, 26), (13, 26)])
    lv.place('*', [(3, 17)])
    lv.place('H', [(15, 7)])
    lv.place('T', [(1, 13)])
    lv.place('P', [(4, 12), (7, 21)])
    lv.place('S', [(7, 1)])
    lv.place('C', [(8, 9), (3, 9)])
    lv.chest(14, 1, coins=15)
    # the upper channel: a lever lowers the grate bridge
    lv.lever(14, 23, 1)
    for y in (24, 25):
        for x in (7, 8):
            lv.group_cell(x, y, 1)
    for x, y, per, ph in ((4, 23, 2200, 0), (10, 22, 2200, 1100), (7, 18, 1800, 0), (2, 8, 2000, 500), (12, 8, 2000, 1500),
                          (7, 5, 2400, 800)):
        lv.trap('vent', x, y, period=per, phase=ph)
    lv.monster('zombie', 1, 22, 'e', path=[(1, 22), (14, 22)], ms=720, pingpong=True, notice=True)
    lv.monster('zombie', 15, 15, 'w', path=[(15, 15), (0, 15)], ms=650, pingpong=True)
    lv.monster('zombie', 5, 12, 'n', path=[(5, 12), (5, 15), (10, 15), (10, 12)], ms=600)
    lv.monster('zombie', 2, 7, 'e', path=[(2, 7), (13, 7)], ms=700, pingpong=True, notice=True)
    lv.monster('zombiedog', 0, 3, 'e', path=[(0, 3), (15, 3)], ms=170, param=900, pingpong=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level3():
    lv = Level('city_3', 'city', 'The Junkyard', time_s=230, par_s=140)
    zone(lv)
    lv.map2("""
XXXXXXXXXXXXXXXX
XXXXXXXoXXXXXXXX
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
ssssssssssssssss
aaaaaaaaaaaaaaaa
llllllllllllllll
aaaaaaaaaaaaaaaa
ssssssssssssssss
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
................
................
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
gggjjjjjjjjjjggg
gggjjjjjjjjjjggg
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
jjjjjjjjjjjjjjjj
ssssssssssssssss
ssssssssssssssss
ssssssssssssssss
""", """
3333333333333333
3333333033333333
0000000000000000
1100000000000011
2100000000000012
0000011001100000
0000012002100000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
0000000000000000
3323333333333233
0010000000000100
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
    lv.place('i', [(3, 26), (12, 26), (4, 25), (11, 25), (1, 11), (14, 11), (6, 3), (12, 2)])
    lv.place('d', [(0, 27), (15, 27)])
    lv.place('n', [(4, 18), (11, 18), (4, 22), (11, 22)])
    lv.place('x', [(2, 7), (10, 5), (5, 9)])
    lv.place('y', [(13, 7)])
    lv.place('l', [(0, 18), (15, 22)])
    lv.place('k', [(8, 22)])
    lv.place('K', [(0, 25), (6, 23), (14, 20), (13, 16), (1, 5)])
    lv.place('o', [(9, 23), (7, 26), (8, 26), (2, 20), (6, 20), (10, 20), (7, 15), (8, 15), (3, 10), (12, 10),
                   (4, 6), (9, 3), (10, 3)])
    lv.place('*', [(15, 2)])
    lv.place('H', [(0, 9)])
    lv.place('T', [(15, 14)])
    lv.place('P', [(7, 14), (7, 18)])
    lv.place('S', [(7, 1)])
    lv.place('C', [(4, 11), (11, 11), (8, 9)])
    lv.chest(15, 25, coins=20, bonus='heart')
    # the avenue through the yard
    lv.lane('car', 0, 21, 'e', 16, size=2, ms=230, gap=6)
    lv.lane('car', 15, 19, 'w', 16, size=2, ms=270, gap=5)
    lv.monster('zombie', 1, 27, 'e', path=[(1, 27), (14, 27)], ms=700, pingpong=True, notice=True)
    lv.monster('zombie', 2, 17, 'e', path=[(2, 17), (13, 17)], ms=650, pingpong=True, notice=True)
    lv.monster('zombie', 3, 14, 'e', path=[(3, 14), (12, 14)], ms=750, pingpong=True)
    lv.monster('zombie', 1, 10, 'e', path=[(1, 10), (1, 6), (8, 6), (8, 10)], ms=650)
    lv.monster('zombiedog', 15, 4, 'w', path=[(15, 4), (0, 4)], ms=160, param=1000, pingpong=True)
    lv.monster('zombiedog', 0, 24, 'e', path=[(0, 24), (15, 24)], ms=170, param=1200, pingpong=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def level4():
    lv = Level('city_4', 'city', 'City Hall', time_s=240, par_s=150)
    zone(lv)
    lv.map2("""
WWWWWWWWWWWWWWWW
WWWWWWWoWWWWWWWW
ssssssssssssssss
oooooooooooooooo
oooooooooooooooo
oggooooooooooggo
oggooooooooooggo
oooooooooooooooo
oooooooooooooooo
ooooooobbooooooo
ooooooobbooooooo
oooooooooooooooo
oooooooooooooooo
oggooooooooooggo
oggooooooooooggo
oooooooooooooooo
ssssssssssssssss
aaaaaaaccaaaaaaa
lllllllcclllllll
aaaaaaaccaaaaaaa
ssssssssssssssss
gggggggssggggggg
gggggggssggggggg
gggggggssggggggg
gggggggssggggggg
gggggggssggggggg
ssssssssssssssss
aaaaaaaaaaaaaaaa
ssssssssssssssss
ssssssssssssssss
""", """
3333333333333333
3333333033333333
0000000000000000
0000000000000000
0000000000000000
0110000000000110
0110000000000110
0000000000000000
0000000000000000
0000000220000000
0000000220000000
0000000000000000
0000000000000000
0110000000000110
0110000000000110
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
    lv.place('l', [(0, 27), (15, 27), (3, 20), (12, 20), (0, 13), (15, 13), (0, 3), (15, 3)])
    lv.place('b', [(4, 25), (11, 25)])
    lv.place('v', [(3, 18), (12, 18)])
    lv.place('e', [(1, 8), (14, 7), (3, 5), (12, 4)])
    lv.place('h', [(6, 13), (9, 13)])
    lv.place('m', [(10, 1)])
    lv.place('u', [(4, 2)])
    lv.place('K', [(1, 24), (14, 16), (0, 20), (15, 22), (2, 6)])
    lv.place('o', [(4, 22), (11, 22), (4, 17), (11, 17), (7, 23), (8, 23), (7, 16), (8, 16), (5, 7), (10, 7),
                   (7, 5), (8, 5)])
    lv.place('*', [(7, 20)])
    lv.place('H', [(14, 25)])
    lv.place('T', [(1, 15)])
    lv.place('P', [(7, 14), (7, 9)])
    lv.place('S', [(7, 0)])
    lv.chest(13, 8, coins=20, bonus='hourglass')
    lv.lane('car', 0, 12, 'e', 16, size=2, ms=220, gap=5)
    lv.lane('car', 15, 10, 'w', 16, size=2, ms=250, gap=6)
    lv.lane('car', 0, 2, 'e', 16, size=2, ms=260, gap=7)
    # the brute stomps round the statue; the shockwave reaches the next cells
    lv.monster('brute', 4, 17, 'e', path=[(4, 17), (11, 17), (11, 22), (4, 22)], ms=520, param=4000)
    lv.monster('zombie', 1, 26, 'e', path=[(1, 26), (14, 26)], ms=650, pingpong=True, notice=True)
    lv.monster('zombie', 14, 14, 'w', path=[(14, 14), (1, 14)], ms=700, pingpong=True)
    lv.monster('zombie', 2, 4, 'e', path=[(2, 4), (5, 4), (5, 8), (2, 8)], ms=650)
    lv.monster('zombie', 13, 3, 'n', path=[(13, 3), (13, 8), (10, 8), (10, 3)], ms=650, notice=True)
    lv.exit_dir = 2
    lv.preview_size = [1700, 1500]
    lv.preview_look = [8, 15, 0]
    return lv


def levels():
    return [level1(), level2(), level3(), level4()]
