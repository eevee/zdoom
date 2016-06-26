#include "actor.h"
#include "p_lnspec.h"

void run_temp_lua_code();
void temp_compile_map_lua(FileReader* fr, int len);
bool temp_call_lua_function_as_acs(AActor *who, line_t *where, int script, const char *map, const int *args, int argcount, int flags);
