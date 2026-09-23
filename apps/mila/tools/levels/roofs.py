"""roofs - levels (from tools/gen.py candidates, chosen by hand)."""

LEVELS = [
    # score 17.0  moves 14  pushes 5  states 114  use s0 f1 g0 fl0 r0 sw4
    dict(title=dict(es='Un hueco', en='A gap', de='Eine Lücke'), text='''
######
# o@.#
## $ #
-# $ #
-##  #
--####'''),
    # score 20.5  moves 20  pushes 7  states 234  use s0 f1 g0 fl0 r0 sw4
    dict(title=dict(es='Puentes de cajón', en='Crate bridges', de='Kistenbrücken'), text='''
######
#.   #
#$$$@#
#  ###
# o.#
#####'''),
    # score 38.2  moves 60  pushes 16  states 4735  use s0 f0 g0 fl0 r2 sw5
    dict(title=dict(es='La pelota', en='The ball', de='Der Ball'), text='''
#####
#   ##
# $b #
# .  #
##   #
#    #
#  # #
# .@##
#####'''),
    # score 41.0  moves 51  pushes 11  states 7401  use s0 f1 g0 fl0 r1 sw8
    dict(title=dict(es='Rodando', en='Rolling', de='Rollen'), text='''
-###
-# #
-#o####
-#o $ #
##$ # #
#@$b  #
# . . #
#######'''),
    # score 33.1  moves 32  pushes 10  states 22702  use s0 f1 g0 fl1 r2 sw7
    dict(title=dict(es='Chimeneas', en='Chimneys', de='Schornsteine'), text='''
#######
#.# # #
#o .h #
#@ $# #
# $.b #
# $ ###
#   #
#####'''),
    # score 46.6  moves 40  pushes 16  states 137551  use s0 f1 g0 fl0 r2 sw11
    dict(title=dict(es='Noche de lluvia', en='Rainy night', de='Regennacht'), text='''
--######
-##  $@#
##  . .#
# $  b #
#  $ $##
#. o#o#
###~# #
--#####'''),
    # score 39.5  moves 45  pushes 16  states 291557  use s0 f1 g0 fl0 r4 sw10
    dict(title=dict(es='Luna llena', en='Full moon', de='Vollmond'), text='''
-######
## .  #
#  $b #
#  b  #
#    .##
#v#. o #
#    $##
## @  #
-#    #
-######'''),
    # score 53.5  moves 48  pushes 19  states 1344997  use s0 f1 g0 fl0 r2 sw12
    dict(title=dict(es='Sobre la ciudad', en='Over the town', de='Über der Stadt'), text='''
----#####
--### . #
--# $   #
--#     #
--#$@ # #
--# $b#.#
###$ .  #
# oo   ##
########'''),
]
