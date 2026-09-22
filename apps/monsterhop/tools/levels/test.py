"""A test level on the neutral test tiles (assets/_test): the engine's bench."""
from levels import Level


def levels():
    lv = Level('test_1', 'test', 'Test Field', time_s=120, par_s=60)
    lv.legend = {'g': {'top': 'blk_grass', 'fill': 'blk_dirt'},
                 'd': {'top': 'blk_dirt', 'fill': 'blk_dirt'}}
    lv.surf = 'blk_dirt'
    lv.terrain('''
g0g0g0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g1g1g0g0g0g0g0
g0g0g2g2g0g1g1g0g0g0g0g0
g0g0g2g3g0g0g0g0g0d0d0g0
g0g0g0g0g0g0g0g0g0d0d0g0
g0g0g0g0..g0g0g0g0g0g0g0
g0g0g0g0g0g0g1g1g1g0g0g0
g0g0g0g0g0g0g1g2g1g0g0g0
g0g0g0g0g0g0g1g1g1g0g0g0
g0g0g0g0g0g0g0g0g0g0g0g0
g0g0d0d0d0d0d0d0d0d0g0g0
g0g0g0g0g0g0g0g0g0g0g0g0
g0g0g0g0g0g0g0g0g0g0g0g0
g0g0g0g1g1g0g0g0g0g0g0g0
g0g0g0g1g1g0g0g0g0g0g0g0
g0g0g0g0g0g0g0g0g0g0g0g0
''')
    lv.things('''
.........E..
.....K......
...K........
..........o.
..........o.
............
.......K....
............
............
....o.o.o...
............
..K.......K.
............
............
............
.....S......
''')
    lv.preview_assets = ['_test']
    lv.preview_size = [1100, 1000]
    lv.preview_look = [6, 8, 0]
    lv.preview_bg = [20, 26, 34]
    return [lv]
