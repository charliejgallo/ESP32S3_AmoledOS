"""kitchen - levels (from tools/gen.py candidates, chosen by hand)."""

LEVELS = [
    # score 7.1  moves 6  pushes 3  states 13  use s1 f0 g0 fl0 r0 sw1
    dict(title=dict(es='Un charco', en='A puddle', de='Eine Pfütze'), text='''
-###
-#@#
-#$##
##  #
# ~ #
#.~~#
#####'''),
    # score 17.4  moves 21  pushes 6  states 333  use s1 f0 g0 fl0 r0 sw4
    dict(title=dict(es='Piso resbaloso', en='Slippery floor', de='Rutschiger Boden'), text='''
#####
#.  #
# $ ##
# $~~#
## ~~#
-#@ .#
-#####'''),
    # score 22.9  moves 25  pushes 9  states 1244  use s2 f0 g0 fl0 r0 sw4
    dict(title=dict(es='Galletitas', en='Cookies', de='Kekse'), text='''
--#####
--#  .#
###  .#
# ~ $@#
# ~#  #
##~~$ #
-#~ ###
-####'''),
    # score 37.3  moves 46  pushes 9  states 2831  use s2 f0 g0 fl0 r0 sw9
    dict(title=dict(es='La heladera', en='The fridge', de='Der Kühlschrank'), text='''
-####
-#  ###
-#$ # #
##~.$ #
#~~ . #
#~~~$##
### .@#
--#####'''),
    # score 46.3  moves 44  pushes 19  states 15185  use s2 f0 g0 fl0 r0 sw10
    dict(title=dict(es='Merienda', en='Snack time', de='Kaffeezeit'), text='''
######
#    #
## $ #
# $ .##
#.  @ #
##$~~.#
#  ~~~#
## ~###
-####'''),
    # score 46.4  moves 66  pushes 20  states 30874  use s3 f0 g0 fl0 r0 sw6
    dict(title=dict(es='Patinando', en='Skating', de='Schlittern'), text='''
#####
#~~~#
#~ ~###
# @~$ #
## ~  #
-#$# ##
-# . .#
-#  $.#
-#   ##
-#####'''),
    # score 47.3  moves 46  pushes 18  states 135699  use s3 f0 g0 fl0 r0 sw12
    dict(title=dict(es='La pileta', en='The sink', de='Die Spüle'), text='''
--#####
--#  .#
### $ #
#  ~$ ##
# $~~~~##
# # ~ ~~#
# .    @#
# . $.###
#######'''),
    # score 60.9  moves 77  pushes 21  states 150577  use s7 f0 g0 fl0 r0 sw13
    dict(title=dict(es='Gran limpieza', en='Big clean-up', de='Großputz'), text='''
----####
#####  #
# $~~$ #
# ~~~~.#
#  ~~~ #
## $. ##
-# $. #
-##@.##
--# ##
--###'''),
]
