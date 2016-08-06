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

void luaZ_pushactor(lua_State *L, AActor *actor);

// -----------------------------------------------------------------------------
// Helpers for declaring types exposed to Lua with minimal fuss

template<class T>
class _ZLuaProperty {
public:
    const char* name;
    ptrdiff_t offset;

    _ZLuaProperty(const char* name, ptrdiff_t offset) : name(name), offset(offset) {}
    virtual int get(lua_State* L, T* obj) const = 0;

    // Implementations of getting for various types
    int _get(lua_State* L, T* obj, BYTE& attribute) const
    {
        lua_pushinteger(L, attribute);
        return 1;
    }

    int _get(lua_State* L, T* obj, int& attribute) const
    {
        lua_pushinteger(L, attribute);
        return 1;
    }

    int _get(lua_State* L, T* obj, double& attribute) const
    {
        lua_pushnumber(L, attribute);
        return 1;
    }
    // TODO set
};

// This is a stupid subclass that only exists to statically choose some of the
// overloads above.  It also hides the templating over A so we can have a
// homogenous list of properties.
template<class T, class A>
class _ZLuaPropertyImpl : public _ZLuaProperty<T> {
public:
    _ZLuaPropertyImpl(const char* name, ptrdiff_t offset) : _ZLuaProperty<T>(name, offset) {}
    int get(lua_State* L, T* obj) const {
        A& attribute = *(A*)((size_t)obj + this->offset);
        return this->_get(L, obj, attribute);
    }
};

#define ZLuaProperty(cls, name, attribute) new _ZLuaPropertyImpl<cls, decltype(cls::attribute)>(name, static_cast<ptrdiff_t>(myoffsetof(cls, attribute)))


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

// .............................................................................
// Thinker iterator for actors
// NOTE: this was not particularly well thought-out, and I don't claim to
// understand how I'm supposed to use it; it just made an interesting proof of
// concept

// TODO this appears to also iterate over actors that are in another actor's inventory...
static int zlua_allactors_iter(lua_State *L);
static int zlua_allactors(lua_State *L) {
    // Create space for the iterator
    // TODO note must be first etc
    TThinkerIterator<AActor>** iter = (TThinkerIterator<AActor>**)lua_newuserdata(L, sizeof(TThinkerIterator<AActor>*));

    // Set its metatable BEFORE opening it
    luaL_getmetatable(L, "zdoom.TThinkerIterator.AActor");
    lua_setmetatable(L, -2);

    // Create the iterator
    *iter = new TThinkerIterator<AActor>();
    // TODO check for null??
    // Create and return the iterator function, whose upvalue (closed-over
    // value) is the iterator we just created
    lua_pushcclosure(L, zlua_allactors_iter, 1);
    return 1;
}

static int zlua_allactors_iter(lua_State *L) {
    // TODO luaL_checkudata
    TThinkerIterator<AActor>** iter = (TThinkerIterator<AActor>**)lua_touserdata(L, lua_upvalueindex(1));
    AActor *actor = (*iter)->Next();
    if (actor) {
        luaZ_pushactor(L, actor);
        return 1;
    }
    else {
        return 0;
    }
}

static int zlua_tthinkeraactor_gc(lua_State *L) {
    TThinkerIterator<AActor>** iter = (TThinkerIterator<AActor>**)lua_touserdata(L, 1);
    delete *iter;
    return 0;
}

// .............................................................................
// Actor type

static const std::vector<_ZLuaProperty<AActor>*> zlua_actor_props = {
    ZLuaProperty(AActor, "weave_index_xy", WeaveIndexXY),
    ZLuaProperty(AActor, "weave_index_z", WeaveIndexZ),
    ZLuaProperty(AActor, "floorclip", Floorclip),
    ZLuaProperty(AActor, "health", health),
};

static int zlua_actor_index(lua_State *L) {
    AActor *actor = *(AActor**)lua_touserdata(L, 1);
    // TODO check for a destroyed object
    const char* key = luaL_checkstring(L, 2);
    for (auto& prop : zlua_actor_props) {
        fprintf(stderr, "checking %s vs %s...\n", key, prop->name);
        if (!strcmp(key, prop->name)) {
            return prop->get(L, actor);
        }
    }
    if (!strcmp(key, "classname")) {
        lua_pushstring(L, actor->GetClass()->TypeName);
        return 1;
    }
    return 0;
}

static int zlua_actor_tostring(lua_State *L) {
    AActor *actor = *(AActor**)luaL_checkudata(L, 1, "zdoom.AActor");
    // TODO check for a destroyed object
    // TODO maybe include classname
    lua_pushstring(L, "<actor>");
    return 1;
}

static const luaL_Reg zlua_actor_meta[] = {
    {"__index", zlua_actor_index},
    {"__tostring", zlua_actor_tostring},
    {NULL, NULL}
};


/*
TODO this all is leading up to having this as a separate lib, which i don't think i care about yet
static const luaL_reg zdoomlib[] = {
    {"zprint", zlua_zprint},
    {NULL, NULL}
};

int luaopen_zdoom(lua_State *L) {
    luaL_openlib(L, "zdoom", zdoomlib, 0);
    return 1;
}
*/

// -----------------------------------------------------------------------------
// Helpers

lua_State *create_lua_state()
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Actor type
    luaL_newmetatable(L, "zdoom.AActor");
    luaL_setfuncs(L, zlua_actor_meta, 0);
    // TODO should i pop this back off the stack now?

    // Thinker type
    luaL_newmetatable(L, "zdoom.TThinkerIterator.AActor");
    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, zlua_tthinkeraactor_gc);
    lua_settable(L, -3);

    // Built-in functions
    lua_pushcfunction(L, zlua_zprint);
    lua_setglobal(L, "zprint");
    lua_pushcfunction(L, zlua_allactors);
    lua_setglobal(L, "allactors");

    return L;
}

void luaZ_pushactor(lua_State *L, AActor *actor)
{
    AActor **ptr = (AActor**)lua_newuserdata(L, sizeof(AActor**));
    luaL_getmetatable(L, "zdoom.AActor");
    lua_setmetatable(L, -2);
    *ptr = actor;
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
    luaZ_pushactor(current_map_lua, who);
    lua_pushnil(current_map_lua);  // TODO side
    for (int a = 0; a < argcount; a++)
    {
        lua_pushinteger(current_map_lua, args[a]);
    }

    // TODO return value, for the sake of script "results"
    _call_lua(current_map_lua, argcount + 2, 0);
    return true;
}
