#include <memory>

#include "actor.h"
#include "p_lnspec.h"

void run_temp_lua_code();
void temp_compile_map_lua(FileReader* fr, int len);
bool temp_call_lua_function_as_acs(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags);
void temp_unload_lua();

// Avoid having this file depend on the entirety of the Lua and Sol APIs
class DLuaThinker_StupidPrivacyHack;

class DLuaThinker : public DThinker
{
	DECLARE_CLASS(DLuaThinker, DThinker)
public:
	DLuaThinker();
	~DLuaThinker();

	void Serialize(FArchive &arc);
	void Tick();

	//typedef TMap<int, DLevelScript *> ScriptMap;
	//ScriptMap RunningScripts;	// Array of all synchronous scripts
    // TODO boy i am not happy about this global
	static TObjPtr<DLuaThinker> active_thinker;

	//void DumpScriptStatus();
	//void StopScriptsFor(AActor *actor);

private:
    class Impl;
    std::unique_ptr<Impl> impl;

    friend class DLuaThinker_StupidPrivacyHack;
};

