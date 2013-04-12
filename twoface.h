#ifndef _TWOFACE_H_
#define _TWOFACE_H_

#include <lua.h>
#define LUA51_REGISTRYINDEX	(-10000)
#define LUA51_GLOBALSINDEX	(-10002)

int adjust_indicies_1(lua_State *L, int* idx1);
void twoface_pushcclosure(lua_State *L, lua_CFunction fn, int n);

#endif
