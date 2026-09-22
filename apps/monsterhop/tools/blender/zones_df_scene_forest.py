"""Monster Hop - Werewolf Woods: the compose scenes (see zones_df_scene.py)."""
from zones_df_scene import build, LOG_DZ

Z = 'forest'
LEGEND = {
    'g': dict(top='grass', fill='forest_fill_earth'),
    'p': dict(top='path', fill='forest_fill_earth', salt=2),
    'r': dict(top='rock', fill='forest_fill_rock', salt=4),
    'm': dict(top='moss', fill='forest_fill_rock', salt=6),
    '~': dict(surf='forest_surf_river'),
}


def sample():
    # back of the level first (y = 11) ... front (y = 0); x = 0..11
    types = [
        "rrrrrggggggg",
        "rrrrrgggpggg",
        "rrrrrggpgggg",
        "mmmmgggpgggg",
        "gggggggpgggg",
        "ggggggpggggg",
        "ggggggpggggg",
        "~~~~~~~~~~~~",
        "~~~~~~~~~~~~",
        "ggggggpggggg",
        "gggggggpgggg",
        "gggggggpgggg",
    ]
    heights = [
        "222221111000",
        "222221100000",
        "222221000000",
        "111100000000",
        "000000000000",
        "000000000000",
        "000000000000",
        "............",
        "............",
        "000000000000",
        "000000000000",
        "000000000000",
    ]
    log = [["forest_logfloat_" + p, x, 3, 0, {"at": [x + 0.5, 3.5, LOG_DZ]}]
           for p, x in (("w", 4), ("m", 5), ("e", 6))]
    items = [
        ['forest_pine', 4, 6],
        ['forest_pine', 9, 6],
        ['forest_pine', 2, 9],
        ['forest_oak', 8, 1],
        ['forest_oak', 3, 4],
        ['forest_mushroom', 5, 5],
        ['forest_mushroom', 9, 2],
        ['forest_lantern', 7, 5],
        ['forest_lantern', 6, 1],
        ['tommy_test', 5, 3, {"palette": "tommy", "at": [5.5, 3.5, LOG_DZ]}],
    ]
    return build(Z, LEGEND, types, heights, items, look_at=[5.0, 6.0, 0.5], bg=[10, 22, 18], extra_items=log)


LEGEND.update({
    'k': dict(top='plank', fill='forest_fill_earth'),
    'u': dict(top='mud', fill='forest_fill_earth', salt=3),
})


def scene():
    """The full set: a woodcutter's cabin with a plank porch and a lantern, a
    mill, a fenced trail down to the river, a plank bridge, floating logs and a
    sinking lily pad, a mud patch with a bear trap, the moon gate."""
    types = [
        "rrrrrggggggg",
        "rrrrgggggggg",
        "gggggggggggg",
        "ggkkgggggggg",
        "gggggpgggggg",
        "gggggpgggggg",
        "gggggpgggggg",
        "~~~~~~~~k~~~",
        "~~~~~~~~k~~~",
        "gggggguugpgg",
        "ggggguuggpgg",
        "gggggggggpgg",
    ]
    heights = [
        "111110000000",
        "111100000000",
        "000000000000",
        "000000000000",
        "000000000000",
        "000000000000",
        "000000000000",
        "........0...",
        "........0...",
        "000000000000",
        "000000000000",
        "000000000000",
    ]
    # the bridge column: pits with the plank bridge on them instead of blocks
    types[7] = "~~~~~~~~~~~~"
    types[8] = "~~~~~~~~~~~~"
    heights[7] = "............"
    heights[8] = "............"
    from zones_df_scene import LOG_DZ
    logs = [["forest_logfloat_" + p, x, 3, 0, {"at": [x + 0.5, 3.5, LOG_DZ]}] for p, x in (("w", 3), ("m", 4), ("e", 5))]
    extra = logs + [["forest_bridge_y", 9, 3, 0, {"at": [9.5, 3.5, 0.0]}],
                    ["forest_bridge_y", 9, 4, 0, {"at": [9.5, 4.5, 0.0]}],
                    ["forest_lily_00", 7, 4, 0, {"at": [7.5, 4.5, -0.17]}],
                    ["forest_lily_02", 1, 3, 0, {"at": [1.5, 3.5, -0.17]}]]
    items = [
        ['forest_cabin', 2, 8],
        ['forest_lantern', 5, 8],
        ['forest_mill', 7, 7],
        ['forest_gate_02', 5, 10, {"floor": 0}],
        ['forest_pine', 1, 10],
        ['forest_oak', 9, 9],
        ['forest_shrine', 3, 6],
        ['forest_fence_y', 6, 5],
        ['forest_fence_y', 6, 6],
        ['forest_flowers', 4, 5],
        ['forest_fern', 8, 5],
        ['forest_bush', 10, 6],
        ['forest_log_x', 1, 5],
        ['forest_mushroom', 2, 4],
        ['forest_stump', 4, 2],
        ['forest_beartrap_00', 6, 1],
        ['forest_crate', 8, 1],
        ['forest_rock', 3, 0],
        ['forest_fence_x', 10, 2],
        ['forest_lantern', 8, 2],
        ['tommy_test', 9, 4, {"palette": "tommy", "floor": 0}],
    ]
    return build(Z, LEGEND, types, heights, items, look_at=[6.7, 5.3, 0.5], bg=[10, 22, 18], extra_items=extra)
