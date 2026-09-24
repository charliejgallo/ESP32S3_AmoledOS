#!/usr/bin/env python3
"""
Generates the symbol table the firmware lends to dynamic apps.

It reads the build's static libraries and emits
components/aos_dynapp/aos_symbols.c with one entry per global symbol. That way
an external app can call LVGL, the HAL and the UI runtime as if it had been
compiled inside the firmware.

Usage:
    idf.py build                 # first pass, so the .a files exist
    python3 tools/gen_symbols.py
    idf.py build                 # second pass, now with the table

With --libs you can widen or narrow which libraries are exported.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Libraries whose symbols the apps see. LVGL is the big one; the rest is
# AmoledOS's own API.
# Note: LVGL's managed component produces liblvgl__lvgl.a, not liblvgl.a.
DEFAULT_LIBS = ["lvgl__lvgl", "lvgl_port_lib",
                "aos_hal", "aos_ui", "aos_apps", "aos_board", "aos_fonts",
                # aos_hal_usb_* and aos_hal_mdns_* are declared in aos_hal.h but
                # live here (aos_usb depends on aos_hal, not the other way round):
                # without this line a .so calling the USB port fails only at load.
                "aos_usb"]

# libc and libm functions nearly any app will need and that the table
# elf_loader brings does NOT include (snprintf, for instance: there you only
# get printf, fprintf and vfprintf). They are added by hand because they do not
# live in our libraries.
EXTRA_SYMBOLS = [
    # stdio
    "snprintf", "vsnprintf", "sprintf", "sscanf", "puts", "putchar",
    # string
    "memcpy", "memmove", "memset", "memcmp",
    "strlen", "strnlen", "strcmp", "strncmp", "strcpy", "strncpy",
    "strcat", "strchr", "strrchr", "strstr", "strcasecmp",
    # stdlib
    "abs", "labs", "atoi", "atol", "strtol", "strtoul", "strtof", "strtod",
    "qsort", "malloc", "calloc", "realloc", "free",
    # getenv: on the board it always returns NULL because there is no
    # environment, and that is exactly what we want. The apps use it for their
    # development switches (GEMAS_AUTO, CLIMA_DEMO...), which therefore live in
    # the same binary that goes onto the SD without costing anything or
    # changing the behaviour.
    "getenv",
    # esp_new_jpeg (branch video): a prebuilt library, so it never shows up as
    # a lib*.a under build/ for the loop below to walk. Naming its decoder
    # entry points here is also what links them into the firmware: nothing
    # else references them, and the table's ESP_ELFSYM_EXPORT() does.
    "jpeg_dec_open", "jpeg_dec_parse_header", "jpeg_dec_get_outbuf_len",
    "jpeg_dec_get_process_count", "jpeg_dec_process", "jpeg_dec_close",
    "jpeg_calloc_align", "jpeg_free_align",
    # math
    "sinf", "cosf", "tanf", "atan2f", "sqrtf", "fabsf",
    "floorf", "ceilf", "roundf", "powf", "fmodf",
    # logarithms and exponentials: without log10f there are no decibels and
    # without log2f there are no cents, which means that without these neither
    # the tuner nor the noise meter can be written. They were missing because
    # until now no app had measured anything on a logarithmic scale.
    "logf", "log10f", "log2f", "expf", "exp2f", "hypotf",
    # files: an app that stores something on the microSD (the recorder, a
    # viewer, anything with data of its own) needs these. elf_loader's own
    # table brings fwrite, but tools/build_apps.sh's check looks only at this
    # table, so it goes in anyway.
    "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "rewind", "fflush",
    "remove", "rename", "unlink", "mkdir", "stat",
    "opendir", "readdir", "closedir",
    # v0.6.0, for the 3D viewer: newlib's FILE buffer is 128 bytes, so a
    # 4 MB STL read with fread went through VFS, FATFS and the SD driver 128
    # bytes at a time. setvbuf gives a stream a real buffer. (Unbuffered is
    # no way out: newlib-nano then reads ONE byte per call.)
    "setvbuf",
    # time: to put a date on whatever is stored
    "time", "localtime_r", "gmtime_r", "mktime",
    # Compiler helpers. The ESP32-S3 has a single-precision FPU but neither
    # floating-point division nor 64-bit arithmetic: gcc resolves those by
    # calling these routines, which on the S3 live in ROM (nm shows them as
    # absolute symbols). Without exporting them, any app dividing a float
    # compiles fine and fails only when loaded: better to lend them, since they
    # cost one pointer each.
    "__divsf3", "__mulsf3", "__addsf3", "__subsf3",
    "__floatsisf", "__floatunsisf", "__fixsfsi", "__fixunssfsi",
    "__divdi3", "__udivdi3", "__moddi3", "__umoddi3",
    # Conversion between float and double. The S3's FPU is single precision,
    # which means this is not "extra" floating-point arithmetic: it is dragged
    # in by any snprintf("%f", x) with a float, because variadics promote to
    # double at the call. It happened to Remoto's diagnostics screen, which
    # prints the accelerometer's three readings, and it will happen to the next
    # app that shows a number with a decimal point.
    "__extendsfdf2", "__truncdfsf2",
    # --------------------------------------------------------------------
    # What an interpreter needs (branch lua). Every one of these came out of
    # build_apps.sh's own check on lua.so: the list is not a guess.
    #
    # setjmp/longjmp are the ones that cannot be worked around. Lua raises
    # errors by longjmp-ing back to the pcall that is holding the fort, and on
    # Xtensa that is not portable C: it has to spill the register windows,
    # which only libc's routine knows how to do. They are also the reason the
    # interpreter cannot be "just an app": these two are the firmware's to
    # lend. The rest could have been written inside the .so -memchr and
    # strspn are four lines each- but they are plain libc that was missing
    # from the table anyway, and a copy inside every .so is a copy to keep.
    #
    # __errno and __getreent are newlib's per-task errno, dragged in by
    # strtof and by anything that opens a file; _ctype_ is the table behind
    # isalpha() and toupper(), which string.upper() uses. The three start
    # with an underscore, so collect() would refuse them: EXTRA_SYMBOLS does
    # not go through that filter, which is exactly why they can be here.
    "setjmp", "longjmp",
    "__errno", "__getreent", "_ctype_",
    "abort", "clock", "localeconv", "strerror",
    "memchr", "strpbrk", "strspn", "strcoll",
    "acosf", "asinf", "frexpf", "ldexpf",
    # stdio beyond what was already lent: lauxlib reads a script with
    # freopen/getc/feof/ferror, and print() writes with fputs/fputc.
    "feof", "ferror", "fputc", "fputs", "freopen", "getc",
    # The allocator with a choice of RAM. Measured on the board on
    # 2026-09-17: with plain malloc, a Lua state that grew to 77 KB pulled
    # the free executable RAM from 107 K down to 56.8 K, because
    # CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024 sends anything smaller than
    # 1 KB to internal RAM and Lua allocates in crumbs. With these three an
    # app can say where its heap goes, which is the only way an interpreter
    # fits without eating the scarce RAM.
    "heap_caps_malloc", "heap_caps_realloc", "heap_caps_free",
]

# Symbols that are never exported: internals of the compiler, of the linker or
# of the loader itself (exporting them breaks the resolution).
EXCLUDE_PREFIXES = ("_", ".", "$", "__")
EXCLUDE_EXACT = {"elf_find_sym", "elf_find_sym_default", "elf_set_symbol_resolver"}


def find_nm():
    for candidate in ("xtensa-esp32s3-elf-nm", "xtensa-esp-elf-nm"):
        path = subprocess.run(["which", candidate], capture_output=True, text=True)
        if path.returncode == 0:
            return path.stdout.strip()
    # last resort: look inside the toolchain's installation
    tools = os.path.expanduser("~/.espressif/tools")
    for base, _dirs, files in os.walk(tools):
        for name in files:
            if name.endswith("xtensa-esp-elf-nm") or name.endswith("xtensa-esp32s3-elf-nm"):
                return os.path.join(base, name)
    return None


# Objects that are never exported: LVGL's demos and examples bring enormous
# images with them and exporting them forces the linker to include them.
#
# The fonts LVGL ships fall in the same bag and for the same reason. The six
# Montserrats are switched off in sdkconfig -they are replaced by the
# aos_montserrat_*, which reach Latin-1- but two remain compiled that depend on
# other options: lv_font_montserrat_14_aligned (35.8 KB) and lv_font_unscii_8
# (7.4 KB, which exists only because Kconfig's "theme default font" list forces
# you to pick one of LVGL's). Nobody uses them: they were in the binary ONLY
# because this table exported them, and exporting a symbol is referencing it.
#
# The filter names the data families, not the whole lv_font_ prefix: the apps
# do need lv_font_get_glyph_dsc() and company.
SKIP_OBJECTS = re.compile(
    r"(lv_demo|lv_example|lv_100ask|_demo_|assets"
    r"|lv_font_montserrat|lv_font_unscii|lv_font_dejavu"
    r"|lv_font_simsun|lv_font_source_han_sans)")


def collect(nm, archive):
    out = subprocess.run([nm, "-g", "--defined-only", archive],
                         capture_output=True, text=True)
    if out.returncode != 0:
        return []

    symbols = []
    pattern = re.compile(r"^[0-9a-fA-F]*\s+([TWDBR])\s+(\S+)$")
    skipping = False

    for raw in out.stdout.splitlines():
        line = raw.strip()

        # nm groups by archive member: "lv_example_win_1.c.obj:"
        if line.endswith(":") and ".obj" in line:
            skipping = bool(SKIP_OBJECTS.search(line))
            continue

        if skipping:
            continue

        match = pattern.match(line)
        if not match:
            continue
        name = match.group(2)
        if name.startswith(EXCLUDE_PREFIXES) or name in EXCLUDE_EXACT:
            continue
        if SKIP_OBJECTS.search(name):
            continue
        symbols.append(name)
    return symbols


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default=os.path.join(ROOT, "build"),
                        help="ESP-IDF build folder")
    parser.add_argument("--libs", nargs="*", default=DEFAULT_LIBS,
                        help="components to export")
    parser.add_argument("--output",
                        default=os.path.join(ROOT, "components", "aos_dynapp",
                                             "aos_symbols.c"))
    args = parser.parse_args()

    nm = find_nm()
    if not nm:
        sys.exit("xtensa-esp32s3-elf-nm not found; you need 'source ~/esp/esp-idf/export.sh'")

    archives = []
    for lib in args.libs:
        found = False
        for base, _dirs, files in os.walk(args.build):
            for name in files:
                if name == f"lib{lib}.a":
                    archives.append(os.path.join(base, name))
                    found = True
        if not found:
            print(f"  warning: lib{lib}.a not found, skipping it")

    if not archives:
        sys.exit("no libraries to export; run 'idf.py build' first")

    symbols = []
    seen = set()

    for name in EXTRA_SYMBOLS:
        if name not in seen:
            seen.add(name)
            symbols.append(name)

    for archive in archives:
        for name in collect(nm, archive):
            if name not in seen:
                seen.add(name)
                symbols.append(name)
    symbols.sort()

    with open(args.output, "w") as out:
        out.write("/*\n")
        out.write(" * GENERADO POR tools/gen_symbols.py - no editar a mano.\n")
        out.write(" *\n")
        out.write(" * Simbolos que el firmware le presta a las apps dinamicas.\n")
        out.write(f" * Librerias: {', '.join(args.libs)}\n")
        out.write(f" * Mas {len(EXTRA_SYMBOLS)} funciones de libc/libm agregadas a mano.\n")
        out.write(f" * Total: {len(symbols)} simbolos.\n")
        out.write(" */\n\n")
        out.write("#include <stddef.h>\n")
        out.write('#include "private/elf_symbol.h"\n\n')
        out.write("#pragma GCC diagnostic push\n")
        out.write('#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"\n')
        for name in symbols:
            out.write(f"extern int {name};\n")
        out.write("#pragma GCC diagnostic pop\n\n")
        out.write("const struct esp_elfsym aos_symbol_table[] = {\n")
        for name in symbols:
            out.write(f"    ESP_ELFSYM_EXPORT({name}),\n")
        out.write("    ESP_ELFSYM_END,\n")
        out.write("};\n")

    print(f"{len(symbols)} symbols exported into {args.output}")
    print("now run 'idf.py build' again")


if __name__ == "__main__":
    main()
