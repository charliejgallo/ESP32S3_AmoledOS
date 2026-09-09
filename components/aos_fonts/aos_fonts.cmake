# Makes aos_montserrat_14 LVGL's default font, replacing lv_font_montserrat_14.
#
# LVGL needs a default font for anything nobody styled explicitly - it is the
# .ptr of the text_font entry in lv_style.c's default table, and what
# lv_theme_get_font_*() falls back to. Without this, turning the Montserrat
# fonts off in menuconfig leaves LVGL referencing lv_font_montserrat_14 and the
# firmware fails to link.
#
# It has to be a -D and cannot be a menuconfig option, because the Kconfig
# choice only lists LVGL's own built-in fonts, and each entry *selects* the font
# it names: picking "Montserrat 14" as the theme font turns
# CONFIG_LV_FONT_MONTSERRAT_14 back on no matter what you set by hand. The way
# out is to point the Kconfig choice at UNSCII 8 - the cheapest option that is
# not a Montserrat - and then override the result here. lv_conf_internal.h
# checks '#ifndef LV_FONT_DEFAULT' before it looks at CONFIG_LV_FONT_DEFAULT, so
# a -D wins.
#
# With that override in place nothing references lv_font_unscii_8, so the linker
# never pulls its object out of the archive and the Kconfig choice costs zero
# bytes - but only after gen_symbols.py stopped exporting it. Exporting a symbol
# IS referencing it: the table alone was worth 7.4 KB of unscii and 35.8 KB of
# lv_font_montserrat_14_aligned, a font nothing in the project ever named.
#
# Firmware only. The .so apps do NOT get these two definitions: project_so()
# assembles its compiler command line by hand and does not quote it, so the "&"
# and the parentheses reach /bin/sh and every app dies with "syntax error near
# unexpected token '('". They are not needed there either - LV_FONT_DEFAULT is
# only used inside LVGL's own .c files, which an app never compiles. See the
# note in apps/common.cmake.
idf_build_set_property(COMPILE_DEFINITIONS
    "LV_FONT_CUSTOM_DECLARE=LV_FONT_DECLARE(aos_montserrat_14)" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS
    "LV_FONT_DEFAULT=&aos_montserrat_14" APPEND)
