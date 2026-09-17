/*
 * Replaces Lua's linit.c: the same luaL_openlibs(), with the list of
 * libraries that are actually on board.
 *
 * Missing, on purpose, and each one for a reason:
 *   io       there is one filesystem, the microSD, and a script gets to it
 *            through the AmoledOS binding, not through fopen on any path.
 *   os       os.execute, os.exit and os.tmpname on a watch; the clock comes
 *            from the HAL instead, which knows about the RTC and the timezone.
 *   package  require() loads .so files: that is the loader we are trying to
 *            get away from.
 *   debug    setmetatable on anything and raw access to upvalues: it undoes
 *            any sandbox built above it.
 *
 * Their sources are not copied into lua/ either, so this is not a switch
 * somebody can flip by accident: the code is not in the binary.
 */
#include "lprefix.h"

#include <stddef.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

static const luaL_Reg loadedlibs[] = {
    {LUA_GNAME,        luaopen_base},
    {LUA_COLIBNAME,    luaopen_coroutine},
    {LUA_TABLIBNAME,   luaopen_table},
    {LUA_STRLIBNAME,   luaopen_string},
    {LUA_MATHLIBNAME,  luaopen_math},
    {LUA_UTF8LIBNAME,  luaopen_utf8},
    {NULL, NULL}
};

LUALIB_API void luaL_openlibs(lua_State *L)
{
    const luaL_Reg *lib;
    for (lib = loadedlibs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);  /* remove the library from the stack */
    }
}
