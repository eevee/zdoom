/*
 * Glue for making Lua be a thing in ZDoom.
 *
 * This is an experiment.  I guarantee nothing.  No one has authorized this or
 * said this will be merged into ZDoom.  I may not finish it.  I may change any
 * and all of the API at any time.  DO NOT build elaborate mods against this
 * expecting them to still work next week.
 */

#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <sol.hpp>

#include "actor.h"
#include "c_console.h"
#include "cmdlib.h"
#include "doomtype.h"
#include "dthinker.h"
#include "p_lnspec.h"
#include "v_font.h"
#include "v_text.h"
#include "w_wad.h"
#include "zstring.h"

// -----------------------------------------------------------------------------
// Lua functions

// TODO need a log() wrapper too
static int zlua_zprint(lua_State *L) {
    int argc = lua_gettop(L);
    FString msg;
    size_t len;
    const char* s;
    for (int a = 1; a <= argc; a++)
    {
        s = luaL_tolstring(L, a, &len);
        msg.AppendCStrPart(s, len);
        if (a < argc)
            msg += " ";
    }
    //Printf("%s\n", s);
    // TODO should allow changing font of course
    C_MidPrint(SmallFont, msg);
    return 0;
}

// -----------------------------------------------------------------------------
// Lua types

// -----------------------------------------------------------------------------
// Helpers

lua_State *create_lua_state()
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    sol::state_view lua(L);

    // Actor type
    lua.new_usertype<AActor>("zdoom.AActor",
        "__tostring", [](AActor& actor) { return "<actor>"; },
        "classname", sol::property([](AActor& actor) -> const char* { return actor.GetClass()->TypeName; }),

        "health", &AActor::health,
        "floorclip", &AActor::Floorclip,
        "weave_index_xy", &AActor::WeaveIndexXY,
        "weave_index_z", &AActor::WeaveIndexZ);

    // Built-in functions
    lua["zprint"] = zlua_zprint;
    // NOTE: this was not particularly well thought-out, and I don't claim to
    // understand how I'm supposed to use it; it just made an interesting proof
    // of concept
    lua["allactors"] = [] {
        TThinkerIterator<AActor> iter;
        return sol::as_function([iter]() mutable {
            return iter.Next();
        });
    };

    return L;
}


// -----------------------------------------------------------------------------
// Internal stuff

static void _dump_lua_stack(lua_State *L) {
    int i;
    int top = lua_gettop(L);

    fprintf(stderr, "--- begin Lua stack dump\n");
    for (i = 1; i <= top; i++) {
        int t = lua_type(L, i);
        fprintf(stderr, "%d\t", i);
        switch (t) {
            case LUA_TSTRING:  /* strings */
            fprintf(stderr, "`%s'", lua_tostring(L, i));
            break;

            case LUA_TBOOLEAN:  /* booleans */
            fprintf(stderr, lua_toboolean(L, i) ? "true" : "false");
            break;

            case LUA_TNUMBER:  /* numbers */
            fprintf(stderr, "%g", lua_tonumber(L, i));
            break;

            default:  /* other values */
            fprintf(stderr, "%s", lua_typename(L, t));
            break;
        }

        fprintf(stderr, "\n");
    }

    fprintf(stderr, "--- end Lua stack dump\n");
}

static int get_lua_traceback(lua_State *L) {
    //lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    fprintf(stderr, "hi from get_lua_traceback %s\n", lua_tostring(L, -1));
    return 1;
}

static bool _call_lua(lua_State* L, int argc, int retc)
{
    // Stick the traceback generator just before the function itself
    lua_pushcfunction(L, get_lua_traceback);
    lua_insert(L, -2 - argc);

    int error = lua_pcall(L, argc, 0, -2 - argc);
    lua_pop(L, -1 - retc);  // pop errfunc
    if (error) {
        Printf(TEXTCOLOR_RED "Lua error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);  // pop error message
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Public interface
// Really bad at the moment, don't use anything

// TODO LOL GLOBALS ARE BAD
// TODO unload everything (!) on map exit
static lua_State *current_map_lua = NULL;

void run_temp_lua_code() {
    if (!current_map_lua)
        return;

    // TODO this breaks if there's no function on top.  of course.
    _call_lua(current_map_lua, 0, 0);

    // TODO need to keep the startup thing working too, somehow?  should it be loaded early too?
    return;
    int error;

    lua_State *L = create_lua_state();

    // TODO figure out the actual directory structure
    // TODO allow multiple
    // TODO this runs on map start, not game start
    int lumpnum = Wads.CheckNumForFullName("lua/startup.lua");
    if (lumpnum != -1) {
        int len = Wads.LumpLength(lumpnum);
        // TODO should this be BYTE?  lua needs a char pointer
        char *script = new char[len];
        Wads.ReadLump(lumpnum, script);
        // TODO how do i tell lua this is from a file?
        // TODO seems to need a blank line at the end of the file...???
        error = luaL_dostring(L, script);
        if (error) {
            Printf(TEXTCOLOR_RED "Lua error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);  // pop error message
        }
        // TODO should the compiled lua stick around somewhere...?  maybe it doesn't matter?
        delete[] script;
    }
    lua_close(L);
}

// Call a Lua function from a special, using a script number
// TODO step 1: on map load, load the map's lua if any...  into...  a what?  what kind of object?
// TODO extra: make a wrapper type for a running lua script that has the same interface as an acs script

void temp_compile_map_lua(FileReader* fr, int len)
{
    if (current_map_lua)
    {
        lua_close(current_map_lua);
    }
    current_map_lua = create_lua_state();

    int error;

    // TODO should this be BYTE?  lua needs a char pointer
    char *script = new char[len];
    fr->Read(script, len);
    // TODO hmmm need map name
    error = luaL_loadbuffer(current_map_lua, script, len, "<map script>");
    if (error) {
        Printf(TEXTCOLOR_RED "Error loading Lua script: %s\n", lua_tostring(current_map_lua, -1));
        lua_pop(current_map_lua, 1);  // pop error message
    }
}

// TODO this needs to check flags, and stuff
bool temp_call_lua_function_as_acs(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags)
{
    if (!current_map_lua)
        return false;

    // Lua functions are always named, and a named script always has a negative
    // script number
    if (script > 0)
        return false;
    FName script_fname = FName(ENamedName(-script));
    if (!script_fname.IsValidName())
        return false;

    const char *script_name = script_fname.GetChars();

    // Find the thing with the given name.  If it's the wrong type, well,
    // that's your fault.
    lua_getglobal(current_map_lua, script_name);

    // Arguments: activator, side, then any args from the special
    sol::stack::push(current_map_lua, who);
    lua_pushnil(current_map_lua);  // TODO side
    for (int a = 0; a < argcount; a++)
    {
        lua_pushinteger(current_map_lua, args[a]);
    }

    // TODO return value, for the sake of script "results"
    _call_lua(current_map_lua, argcount + 2, 0);
    return true;
}
