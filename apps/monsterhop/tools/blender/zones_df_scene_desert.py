"""Monster Hop - Mummy Desert: the compose scenes (see zones_df_scene.py)."""
from zones_df_scene import build

Z = 'desert'
LEGEND = {
    's': dict(top='sand', fill='desert_fill_sand'),
    'S': dict(top='sandstone', fill='desert_fill_sandstone', salt=3),
    'b': dict(top='brick', fill='desert_fill_brick'),
    'w': dict(top='desert_blk_brick_win', under=['desert_blk_brick_win'], fill='desert_fill_brick'),
    'W': dict(top='brick', under=['desert_blk_brick_win'], fill='desert_fill_brick'),
    'o': dict(top='oasis', fill='desert_fill_sand', salt=5),
    '~': dict(surf='desert_surf_water'),
    'q': dict(surf='desert_surf_quicksand'),
}


def sample():
    # back of the level first (y = 11) ... front (y = 0); x = 0..11
    types = [
        "ssssssssssss",
        "ssssssssssss",
        "ssssssssssss",
        "sswbwbwbssss",
        "ssSSSSSSssss",
        "ssSSSSSSssss",
        "sssssqssssss",
        "ssssqqssssss",
        "ssssssooooss",
        "sssssso~~oss",
        "sssssso~~oss",
        "ssssssooooss",
    ]
    heights = [
        "112222222110",
        "112222222110",
        "112222222110",
        "013333331100",
        "011111111000",
        "001111110000",
        "00000.001100",
        "0000..001100",
        "000000000000",
        "0000000..000",
        "0000000..000",
        "000000000000",
    ]
    items = [
        ['desert_brazier', 3, 6],
        ['desert_brazier', 6, 6],
        ['desert_obelisk', 4, 1],
        ['desert_palm', 6, 3],
        ['desert_palm', 9, 2],
        ['desert_cactus', 8, 5],
        ['desert_cactus', 3, 3],
        ['tommy_test', 7, 4, {"palette": "tommy"}],
    ]
    return build(Z, LEGEND, types, heights, items, look_at=[5.3, 5.6, 0.6], bg=[40, 28, 20])


LEGEND.update({
    'G': dict(top='gold', fill='desert_fill_sandstone', salt=8),
    'c': dict(top='cracked', fill='desert_fill_sandstone', salt=9),
    'D': dict(top='desert_dartwall', fill='desert_fill_brick'),
})


def scene():
    """The full set in a game situation: a temple gate between lotus columns
    on a gilded floor, a hieroglyph wall, a dart trap, a rolling boulder on a
    cracked road, the sphinx, an oasis channel with a reed bridge, a camel."""
    types = [
        "ssssssssssss",
        "ssssssssssss",
        "ssssssssssss",
        "swbwbwbwssss",
        "sSGGGGGSssss",
        "sSSSSSSSssss",
        "sssssssssss",
        "sssccccsDsss",
        "sssssssssss",
        "sssssssssss",
        "ssssoo~~~~oo",
        "ssssoooooooo",
    ]
    types[6] = "sssssssssss" + "s"
    types[8] = "sssssssssss" + "s"
    types[9] = "ssssssoooooo"
    heights = [
        "222222222110",
        "222222222110",
        "222222222110",
        "033333331100",
        "011111110000",
        "011111110000",
        "000000000000",
        "000000001000",
        "000000000000",
        "000000000000",
        "000000....00",
        "000000000000",
    ]
    items = [
        ['desert_gate_03', 4, 7],
        ['desert_column', 2, 7],
        ['desert_column', 6, 7],
        ['desert_brazier', 3, 6],
        ['desert_brazier', 5, 6],
        ['desert_urn', 1, 6],
        ['desert_urn', 7, 6],
        ['desert_statue', 8, 6],
        ['desert_scarab', 3, 5],
        ['desert_crate', 6, 5],
        ['desert_boulder_x_03', 3, 4],
        ['desert_dart_y', 8, 3, {"at": [8.5, 3.3, 0.0]}],
        ['desert_sphinx', 4, 2],
        ['desert_camel', 8, 2],
        ['desert_bridge_y', 7, 1, {"at": [7.5, 1.5, 0.0]}],
        ['desert_cactus_small', 6, 3],
        ['desert_rock', 3, 2],
        ['desert_palm', 10, 0],
        ['desert_cactus', 9, 5],
        ['tommy_test', 7, 3, {"palette": "tommy"}],
    ]
    return build(Z, LEGEND, types, heights, items, look_at=[5.8, 4.9, 0.6], bg=[40, 28, 20])
