# Lua 5.4.8, as it comes from lua.org

    https://www.lua.org/ftp/lua-5.4.8.tar.gz
    sha256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    374332 bytes, published 2025-05-21

Copied from `lua-5.4.8/src/`. Lua is free software under the MIT licence; the
notice is at the bottom of `lua.h`, where its authors put it.

## What was left out, and why

| File | Why |
|---|---|
| `lua.c`, `luac.c` | The desktop executables. They bring `main()`, and `project_so()` sweeps up the whole tree with `GLOB_RECURSE`: leaving them in puts two `main()` in the `.so`. |
| `liolib.c` | `io`: one filesystem, reached through the AmoledOS binding and not through `fopen` on any path the script fancies. |
| `loslib.c` | `os`: `os.execute`, `os.exit`, `os.tmpname`. The clock comes from the HAL, which knows about the RTC and the timezone. |
| `loadlib.c` | `require` and `package`: it loads `.so` files, which is the loader we are trying to get away from. |
| `ldblib.c` | The `debug` library undoes any sandbox built above it. |
| `linit.c` | Replaced by `../lua_init.c`, which opens the same way with the list of libraries that are actually here. |
| `lua.hpp` | The C++ wrapper. |

Not copying them is the point: the code is not in the binary, so it is not a
switch anyone can flip by accident.

## The one patch

`luaconf.h`, line 125. Lua ships

    #define LUA_32BITS	0

and the build needs it at 1 (32-bit integers, single-precision floats: the
ESP32-S3 has no double-precision FPU). The three lines added make it
overridable so the value lives in `apps/lua/CMakeLists.txt` and in
`sim/CMakeLists.txt`, where it can be read, instead of buried in a vendored
header:

    #if !defined(LUA_32BITS)
    #define LUA_32BITS	0
    #endif

Any other change to this directory should be listed here, or the next upgrade
will quietly undo it.
