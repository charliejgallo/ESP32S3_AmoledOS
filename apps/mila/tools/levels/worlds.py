"""The worlds, in map order (bottom to top). DESIGN.md section 4.

A new world: a dict here, tools/levels/<id>.py with its LEVELS, its kit and
map panel in the art (SPEC.md), and nothing in the code unless it brings a
new mechanic. `id` is forever: progress is saved under it.
"""

WORLDS = [
    dict(id='living', name=dict(es='El living', en='Living room', de='Wohnzimmer'),
         need=0, music=0, mech=(), gift='hat_bow'),
    dict(id='kitchen', name=dict(es='La cocina', en='Kitchen', de='Küche'),
         need=8, music=1, mech=('wet',), gift='neck_bandana'),
    dict(id='garden', name=dict(es='El jardín', en='Garden', de='Garten'),
         need=18, music=2, mech=('plate', 'wet'), gift='hat_flower'),
    dict(id='attic', name=dict(es='El altillo', en='Attic', de='Dachboden'),
         need=30, music=3, mech=('flap', 'plate'), gift='hat_beret'),
    dict(id='roofs', name=dict(es='Los tejados', en='Rooftops', de='Dächer'),
         need=44, music=4, mech=('hole', 'ball', 'flap', 'wet'), gift='hat_crown'),
]
