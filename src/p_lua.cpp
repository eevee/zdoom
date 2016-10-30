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
#include "g_shared/a_pickups.h"
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
// Dummy types used for exposing to Lua

// Actor inventory
// There is no actor inventory container object; an actor's inventory is just a
// pointer to the first thing in its inventory, and the rest are a linked list.
// This is a dummy object that wraps an actor and provides some standard
// inventory access.
// TODO is this the sort of thing that builtin ACS functions should be
// rewritten in terms of?
struct ZLuaInventory {
    TObjPtr<AActor> actor;
    ZLuaInventory(AActor& actor) : actor(&actor) {}

    AInventory* find_by_name(const char* type) {
        if (stricmp(type, "Armor") == 0)
        {
            type = "BasicArmor";
        }
        // TODO the acs version of this function also accepts "health", but that's not an item

        PClassActor *info = PClass::FindActor (type);
        if (info == NULL)
        {
            // TODO unsure this is right
            throw sol::error("no such actor class");
        }

        return this->find(info);
    }

    AInventory* find(PClassActor* cls)
    {
        if (!cls->IsDescendantOf(RUNTIME_CLASS(AInventory)))
        {
            // TODO unsure this is right
            throw sol::error("not an inventory item");
        }

        return this->actor->FindInventory(cls);
    }

    std::function<AInventory* ()> iter()
    {
        TObjPtr<AInventory> item= this->actor->Inventory;
        // TODO maybe mark a few more methods const
        return [item]() mutable {
            AInventory* ret = item;
            if (ret)
                item = ret->NextInv();
            return ret;
        };
    }
};

// -----------------------------------------------------------------------------
// Helpers

lua_State *create_lua_state()
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    sol::state_view lua(L);

    // Actor class type
    // TODO contrary to my expectations, most of the stuff you see in DECORATE
    // doesn't actually live here!  it seems to live on a single "default"
    // actor.  so this might need to be a goofy wrapper type as well, if we
    // want to allow lua to examine actor types directly
    lua.new_usertype<PClassActor>("zdoom.PClassActor");

    // Actor type
    lua.new_usertype<AActor>("zdoom.AActor",
        "__tostring", [](AActor& actor) { return "<actor>"; },
        "classname", sol::property([](AActor& actor) -> const char* { return actor.GetClass()->TypeName; }),

        "inventory", sol::property([](AActor& actor) -> ZLuaInventory { return ZLuaInventory(actor); }),

        "health", &AActor::health,
        "floorclip", &AActor::Floorclip,
        "weave_index_xy", &AActor::WeaveIndexXY,
        "weave_index_z", &AActor::WeaveIndexZ);

    // Actor inventory type
    lua.new_usertype<ZLuaInventory>("zdoom.AActor.inventory",
        // TODO should be inventory for <actor> "__tostring", [](AActor& actor) { return "<actor>"; },
        "find", sol::overload(&ZLuaInventory::find, &ZLuaInventory::find_by_name),
        "iter", &ZLuaInventory::iter);

    // Inventory item type (inherits from AActor)
    lua.new_usertype<AInventory>("zdoom.AInventory",
        "__tostring", [](AInventory& actor) { return "<item>"; },
        // TODO is this supposed to be inherited automatically??
        "classname", sol::property([](AInventory& actor) -> const char* { return actor.GetClass()->TypeName; }),

        "amount", &AInventory::Amount,
        "max_amount", &AInventory::MaxAmount,
        sol::base_classes, sol::bases<AActor>());

    // Built-in functions
    lua["zprint"] = zlua_zprint;
    // NOTE: this was not particularly well thought-out, and I don't claim to
    // understand how I'm supposed to use it; it just made an interesting proof
    // of concept
    // TODO i am not 100% sure how lifetime works here
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
