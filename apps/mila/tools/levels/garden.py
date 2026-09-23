"""garden - levels (from tools/gen.py candidates, chosen by hand)."""

LEVELS = [
    # by hand: the tutorial of the plate (one pot holds the gate open, the other goes through)
    dict(title=dict(es='La placa', en='The plate', de='Die Platte'), text='''
#########
#@$ _   #
#   #####
# $ G  .#
#########'''),
    # score 20.5  moves 20  pushes 9  states 119  use s0 f0 g2 fl0 r0 sw3
    dict(title=dict(es='La reja', en='The gate', de='Das Tor'), text='''
--####
###@_#
#  $ #
# $ G#
#. # #
######'''),
    # score 51.0  moves 55  pushes 16  states 4632  use s0 f0 g2 fl0 r0 sw10
    dict(title=dict(es='Macetas', en='Flower pots', de='Blumentöpfe'), text='''
---###
#### #
# $ G#
# $#@#
#. . #
# _$ #
###  #
--####'''),
    # score 38.8  moves 52  pushes 15  states 1537  use s0 f0 g1 fl0 r0 sw6
    dict(title=dict(es='Después de la lluvia', en='After the rain', de='Nach dem Regen'), text='''
--######
--#   .#
--#_   #
-##@.$G#
## $~# #
#$ ~~ ##
#######'''),
    # score 49.2  moves 45  pushes 18  states 6011  use s0 f0 g2 fl0 r0 sw11
    dict(title=dict(es='Dos placas', en='Two plates', de='Zwei Platten'), text='''
#####
#.$ ##
#  $ ##
# $$_ #
##_ @ #
-##G  #
--#.###
--###'''),
    # score 60.7  moves 68  pushes 23  states 15899  use s1 f0 g2 fl0 r0 sw11
    dict(title=dict(es='El enanito', en='The gnome', de='Der Gartenzwerg'), text='''
---#####
####~~~#
#.##~_ #
#G## $ #
# ..$$##
#    $ #
##@#   #
-#######'''),
    # score 67.7  moves 80  pushes 21  states 28130  use s0 f0 g2 fl0 r0 sw14
    dict(title=dict(es='Laberinto verde', en='Green maze', de='Grünes Labyrinth'), text='''
---#####
-### $ #
-# #.$ #
-#G.$  #
-# #_ ##
##$_@ #
#   .##
#  $ #
######'''),
    # score 64.0  moves 56  pushes 21  states 115981  use s1 f0 g2 fl0 r0 sw16
    dict(title=dict(es='Jardín secreto', en='Secret garden', de='Geheimer Garten'), text='''
-----###
---###.#
-###~~G#
## _~~ ##
#@$ $ ._#
#$ $ $ .#
##   #  #
-########'''),
]
