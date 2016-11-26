/*
 * Glue for making Lua be a thing in ZDoom.
 *
 * This is an experiment.  I guarantee nothing.  No one has authorized this or
 * said this will be merged into ZDoom.  I may not finish it.  I may change any
 * and all of the API at any time.  DO NOT build elaborate mods against this
 * expecting them to still work next week.
 */

#include <string>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <sol.hpp>

#include "p_lua.h"

#include "actor.h"
#include "c_console.h"
#include "cmdlib.h"
#include "doomtype.h"
#include "dthinker.h"
#include "i_system.h"
#include "g_shared/a_pickups.h"
#include "p_lnspec.h"
#include "v_font.h"
#include "v_text.h"
#include "w_wad.h"
#include "zstring.h"
#include "thingdef/thingdef.h"


TObjPtr<DLuaThinker> DLuaThinker::active_thinker = nullptr;

class DLuaThinker_StupidPrivacyHack
{
public:
    static void compile_lua(FileReader* fr, int len);
    static bool call_named_script(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags);
};


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

// Actor type index
struct ZLuaClassIndex {
    PClass* get(const char* name) {
        return PClass::FindClass(name);
    }
};

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

    std::function<std::tuple<AInventory*, decltype(AInventory::Amount)> ()>
    iter()
    {
        TObjPtr<AInventory> item = this->actor->Inventory;
        // TODO maybe mark a few more methods const
        return [item]() mutable {
            AInventory* ret = item;
            if (ret)
                item = ret->NextInv();
            return std::make_tuple(ret, ret ? ret->Amount : 0);
        };
    }
};

// -----------------------------------------------------------------------------
// Helpers



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

void temp_unload_lua() {
    // TODO this would also need to unload any lingering coroutines and whatnot
    if (DLuaThinker::active_thinker) {
        delete DLuaThinker::active_thinker;
    }
}

// TODO this function is confused about whether it's for executing global startup code or initializing map code
void run_temp_lua_code() {
    return;
    /*
    if (!current_map_lua)
        return;

    // TODO this breaks if there's no function on top.  of course.
    _call_lua(current_map_lua, 0, 0);
    */

    // TODO need to keep the startup thing working too, somehow?  should it be loaded early too?
    return;
    int error;

    // TODO LOL DON'T RUN THIS.
    lua_State *L;// = create_lua_state();

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

void temp_compile_map_lua(FileReader* fr, int len)
{
    DLuaThinker_StupidPrivacyHack::compile_lua(fr, len);
}

bool temp_call_lua_function_as_acs(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags)
{
    return DLuaThinker_StupidPrivacyHack::call_named_script(who, where, script, map, args, argcount, flags);
}



struct _PendingLuaScript
{
public:
    sol::coroutine coro;
    // TODO i guess technically this could be done with subclasses
    enum {
        DELAY,
        SECTOR_TAG,
    } waiting_type;
    int tag_or_duration;

    bool ready() {
        return (this->tag_or_duration <= 0);
    }

    void tick() {
        if (this->ready()) return;

        this->tag_or_duration--;
    }
};

class DLuaThinker::Impl
{
public:
    Impl() {
        fprintf(stderr, "creating an impl\n");
        this->populate_lua_state();
    }

    void populate_lua_state();
    void compile_lua(FileReader* fr, int len);
    bool call_named_script(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags);

    void tick();

    sol::state lua;
    std::vector<_PendingLuaScript> pending_scripts;
};

void DLuaThinker::Impl::populate_lua_state()
{
    lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::coroutine,
        sol::lib::string,
        // sol::lib::os,  -- NOPE
        sol::lib::math,  // TODO may need to replace some of these
        sol::lib::table
        // sol::lib::debug,  TODO unsure how kosher this is
        // sol::lib::bit32,  TODO unnecessary in 5.3?
        // sol::lib::io,  TODO certainly not, BUT a similar interface to pk3 contents would be cool
        // sol::lib::ffi,  -- NOPE
        // sol::lib::jit,  -- NOPE
    );

    // Actor class type
    // TODO contrary to my expectations, most of the stuff you see in DECORATE
    // doesn't actually live here!  it seems to live on a single "default"
    // actor.  so this might need to be a goofy wrapper type as well, if we
    // want to allow lua to examine actor types directly
    lua.new_usertype<PClassActor>("zdoom.PClassActor");

    // Actor class type
    // TODO should this be PClass or PClassActor?
    // TODO so, a tricky thing about /class/ access is that most of the
    // interesting properties just exist on a "prototype" actor object.  how do
    // i get all of those without duplicating all the stuff below?
    lua.new_usertype<PClass>("zdoom.PClass",
        "__tostring", [](PClass& cls) { return "<class: ...>"; },  // TODO TypeName
        "name", sol::property([](PClass& cls) -> const char* { return cls.TypeName; }),

        "is_runtime", &PClass::bRuntimeClass);

    // Actor type
    lua.new_usertype<AActor>("zdoom.AActor",
        "__tostring", [](AActor& actor) { return "<actor>"; },
        "class", &AActor::GetClass,
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
        "__pairs", &ZLuaInventory::iter,
        "iter", &ZLuaInventory::iter);

    // Inventory item type (inherits from AActor)
    lua.new_usertype<AInventory>("zdoom.AInventory",
        "__tostring", [](AInventory& actor) { return "<item>"; },
        // TODO is this supposed to be inherited automatically??
        "classname", sol::property([](AInventory& actor) -> const char* { return actor.GetClass()->TypeName; }),

        "amount", &AInventory::Amount,
        "max_amount", &AInventory::MaxAmount,
        sol::base_classes, sol::bases<AActor>());

    lua.new_usertype<ZLuaClassIndex>("zdoom.PClass.index",
        sol::meta_function::index, &ZLuaClassIndex::get);
    lua["zclasses"] = ZLuaClassIndex{};

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

    // TODO HAHA SO THIS IS VERY BAD ACTUALLY
    // While it would be NICE to have level-scoped actor classes, I'm not sure
    // that actually works.  I don't know how ZDoom deletes actors, if it can
    // even do so at all.
    lua["zdeclare_class"] = [](sol::table args) {
        FName name(args.get<const char*>("name"));
        PClass* parent = args.get<PClass*>("parent");
        FScriptPosition sc("<lua>", 0);  // TODO get file and line of caller
        Baggage bag;
        PClassActor* cls = CreateNewActor(sc, name, parent->TypeName, false);

		cls->DoomEdNum = -1;
		// TODO cls->SourceLumpName = Wads.GetLumpFullPath(sc.LumpNum);
		// TODO SetReplacement(sc, info, replaceName);
		ResetBaggage(&bag, static_cast<PClassActor *>(cls->ParentClass));
		bag.Info = cls;
		// TODO? bag->Lumpnum = sc.LumpNum;

        sol::optional<int> health = args["health"];
        if (health) {
            ((AActor*) cls->Defaults)->health = *health;
        }

        FinishActor(sc, cls, bag);
        return cls;
    };

    lua["zspawn"] = [](PClass* cls) {
		FActorIterator iterator(1);
		AActor *aspot;

		while ((aspot = iterator.Next())) {
            // TODO the acs functions do stuff before and after this
            Spawn(static_cast<PClassActor*>(cls), aspot->Pos(), ALLOW_REPLACE);
            Spawn("TeleportFog", aspot->Pos(), ALLOW_REPLACE);
		}
    };
}

// Call a Lua function from a special, using a script number
// TODO step 1: on map load, load the map's lua if any...  into...  a what?  what kind of object?
// TODO extra: make a wrapper type for a running lua script that has the same interface as an acs script
void DLuaThinker::Impl::compile_lua(FileReader* fr, int len)
{
    //int error;

    // TODO should this be BYTE?  lua needs a char pointer
    // TODO i totally hate that i have to make a copy here to get a
    // std::string, but i think it needs fixing in FileReader
    char *script = new char[len];
    fr->Read(script, len);
    // TODO hmmm need map/script name; how do i tell sol about them?
    std::string script_cpp(script, len);
    // TODO this should be lua.load(), but then i need to store the load_result somewhere
    // TODO also this seems to crash immediately, nice
    lua.script(script_cpp);
    delete script;
    /*
    error = luaL_loadbuffer(current_map_lua, script, len, "<map script>");
    if (error) {
        Printf(TEXTCOLOR_RED "Error loading Lua script: %s\n", lua_tostring(current_map_lua, -1));
        lua_pop(current_map_lua, 1);  // pop error message
    }
    */
}

// TODO this needs to check flags, and stuff
bool DLuaThinker::Impl::call_named_script(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags)
{
    fprintf(stderr, "trying to run script %d\n", script);
    // Lua functions are always named, and a named script always has a negative
    // script number
    if (script > 0)
        return false;
    FName script_fname = FName(ENamedName(-script));
    if (!script_fname.IsValidName())
        return false;

    // Copy the args into something iterable
    // TODO avoid this copy
    std::vector<int> vecargs(argcount);
    for (int a = 0; a < argcount; a++) {
        vecargs[a] = args[a];
    }

    // Find and call the function with standard arguments.
    std::string script_name = script_fname.GetChars();
    sol::coroutine coro = lua[script_name];
    auto result = coro(
        who,  // Activator
        nullptr,  // TODO activated side
                  // TODO or...  maybe i want the first arg to be a general context object?  idk
        sol::as_args(vecargs));
    if (coro.status() == sol::call_status::ok) {
        // Call returned or otherwise exited normally
        // TODO how the fuck do i just check what type this is?
        sol::optional<nullptr_t> maybe_nil = result;
        if (maybe_nil) {
            // Returned nil, use default behavior of success
            fprintf(stderr, "nil!\n");
            return true;
        }
        // TODO does this work, or is it susceptible to lua bool casting?
        sol::optional<bool> maybe_bool = result;
        if (maybe_bool) {
            fprintf(stderr, "bool!\n");
            // TODO erm.  how do i communicate a "result value" back to the switch?
            return true;
        }
        // TODO raise?  can i hook into sol's type machinery for consistent complaining?
        return true;
    }
    else if (coro.status() == sol::call_status::yielded) {
        // TODO what happens here if the return value was wrong?
        // TODO lol uh if the second arg is a /string/, this just continues the
        // script when you press the switch again, WHAT
        // TODO wait actually that is a REALLY INTERESTING IDEA and easy with coros
        std::tuple<std::string, int> ret = result;
        fprintf(stderr, "delay: %d\n", std::get<1>(ret));
        pending_scripts.push_back(
            _PendingLuaScript{coro, _PendingLuaScript::DELAY, std::get<1>(ret)});
        // Counts as success
        // TODO can an ACS script with a delay use SetResultValue before that to break the switch?
        return true;
    }
    else {
        // TODO check this
        throw sol::error("i am not sure how we got here");
    }
}

void DLuaThinker::Impl::tick()
{
    for (auto& script : pending_scripts)
    {
        // TODO need to remove finished scripts too
        if (not script.ready()) {
            script.tick();
            if (script.ready()) {
                auto result = script.coro();
                // TODO keep it going so a second delay works oops
            }
        }
    }
}



IMPLEMENT_CLASS(DLuaThinker)

DLuaThinker::DLuaThinker()
    : DThinker(STAT_SCRIPTS),
    impl(new DLuaThinker::Impl)
{
    fprintf(stderr, "being created!\n");
    if (DLuaThinker::active_thinker) {
        I_Error("Only one LuaThinker may exist at a time.");
        return;
    }

    DLuaThinker::active_thinker = this;
}

DLuaThinker::~DLuaThinker()
{
    fprintf(stderr, "~ ah good, being destroyed\n");

    if (DLuaThinker::active_thinker == this) {
        DLuaThinker::active_thinker = nullptr;
    }
}

void DLuaThinker::Serialize(FArchive& arc)
{
    // nothin for now
}

void DLuaThinker::Tick()
{
    this->impl->tick();
}

void DLuaThinker_StupidPrivacyHack::compile_lua(FileReader* fr, int len)
{
    if (!DLuaThinker::active_thinker) {
        new DLuaThinker;
    }
    DLuaThinker::active_thinker->impl->compile_lua(fr, len);
}
bool DLuaThinker_StupidPrivacyHack::call_named_script(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags)
{
    if (!DLuaThinker::active_thinker) {
        new DLuaThinker;
    }
    return DLuaThinker::active_thinker->impl->call_named_script(who, where, script, map, args, argcount, flags);
}
