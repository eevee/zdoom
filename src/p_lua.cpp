extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "actor.h"
#include "doomtype.h"
#include "dthinker.h"
#include "v_text.h"
#include "w_wad.h"

static int zlua_zprint(lua_State *L) {
    const char* s = luaL_checkstring(L, 1);
    if (!s) {
        return 0;
    }
    Printf("%s\n", s);
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
    TThinkerIterator<AActor>** iter = (TThinkerIterator<AActor>**)lua_touserdata(L, lua_upvalueindex(1));
    AActor *actor = (*iter)->Next();
    if (actor) {
        AActor **ptr = (AActor**)lua_newuserdata(L, sizeof(AActor**));
        luaL_getmetatable(L, "zdoom.AActor");
        lua_setmetatable(L, -2);
        *ptr = actor;
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
    const char* key = luaL_checkstring(L, 2);
    if (!strcmp(key, "classname")) {
        lua_pushstring(L, actor->GetClass()->TypeName);
        return 1;
    }
    return 0;
}


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

void run_temp_lua_code() {
    int error;

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Actor type
    luaL_newmetatable(L, "zdoom.AActor");
    lua_pushstring(L, "__index");
    lua_pushcfunction(L, zlua_actor_index);
    lua_settable(L, -3);

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
