extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "actor.h"
#include "c_console.h"
#include "doomtype.h"
#include "dthinker.h"
#include "p_lnspec.h"
#include "v_font.h"
#include "v_text.h"
#include "w_wad.h"
#include "zstring.h"

void luaZ_pushactor(lua_State *L, AActor *actor);

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

static int zlua_actor_index(lua_State *L) {
    AActor *actor = *(AActor**)lua_touserdata(L, 1);
    // TODO check for a destroyed object
    const char* key = luaL_checkstring(L, 2);
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
// Public functions

// TODO LOL GLOBALS ARE BAD
// TODO unload everything (!) on map exit
static lua_State *current_map_lua = NULL;

void run_temp_lua_code() {
    if (!current_map_lua)
        return;

    lua_call(current_map_lua, 0, 0);





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
        Printf(TEXTCOLOR_RED "Lua error: %s\n", lua_tostring(current_map_lua, -1));
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

    // Find a function with the given name
    int functype = lua_getglobal(current_map_lua, script_name);
    if (functype != LUA_TFUNCTION)
    {
        lua_pop(current_map_lua, 1);
        return false;
    }

    // Arguments: activator, side, then any args from the special
    luaZ_pushactor(current_map_lua, who);
    lua_pushnil(current_map_lua);  // TODO side
    for (int a = 0; a < argcount; a++)
    {
        lua_pushinteger(current_map_lua, args[a]);
    }

    // TODO return value, for the sake of script "results"
    int error = lua_pcall(current_map_lua, argcount + 2, 0, 0);
    if (error) {
        Printf(TEXTCOLOR_RED "Lua error: %s\n", lua_tostring(current_map_lua, -1));
        lua_pop(current_map_lua, 1);  // pop error message
    }
    return true;
}
