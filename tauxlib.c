#include "twoface.h"
#include <lauxlib.h>

static const char* strchr_modified(const char* str, char c) {
  while(*str && *str != c)
    ++str;
  return str;
}

const char* twofaceL_findtable(lua_State *L, int idx, const char *fname, int szhint) {
  const char *e;
  if(!adjust_indicies_1(L, &idx))
    lua_pushvalue(L, idx);
  do {
    e = strchr_modified(fname, '.');
    lua_pushlstring(L, fname, e - fname);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {  /* no such field? */
      lua_pop(L, 1);  /* remove this nil */
      lua_createtable(L, 0, (*e == '.' ? 1 : szhint)); /* new table for field */
      lua_pushlstring(L, fname, e - fname);
      lua_pushvalue(L, -2);
      lua_settable(L, -4);  /* set new table into field */
    }
    else if (!lua_istable(L, -1)) {  /* field has a non-table value? */
      lua_pop(L, 2);  /* remove table and value */
      return fname;  /* return problematic part of the name */
    }
    lua_remove(L, -2);  /* remove previous table */
    fname = e + 1;
  } while (*e == '.');
  return NULL;
}

static int libsize (const struct luaL_Reg *l) {
  int size = 0;
  for (; l->name; l++) size++;
  return size;
}

void twofaceL_openlib(lua_State *L, const char *libname, const struct luaL_Reg *l, int nup) {
  if (libname) {
    int size = libsize(l);
    /* check whether lib already exists */
    twofaceL_findtable(L, LUA51_REGISTRYINDEX, "_LOADED", 1);
    lua_getfield(L, -1, libname);  /* get _LOADED[libname] */
    if (!lua_istable(L, -1)) {  /* not found? */
      lua_pop(L, 1);  /* remove previous result */
      /* try global variable (and create one if it does not exist) */
      if (twofaceL_findtable(L, LUA51_GLOBALSINDEX, libname, size) != NULL)
        luaL_error(L, "name conflict for module " LUA_QS, libname);
      lua_pushvalue(L, -1);
      lua_setfield(L, -3, libname);  /* _LOADED[libname] = new table */
    }
    lua_remove(L, -2);  /* remove _LOADED table */
    lua_insert(L, -(nup+1));  /* move library table to below upvalues */
  }
  for (; l->name; l++) {
    int i;
    for (i=0; i<nup; i++)  /* copy upvalues to the top */
      lua_pushvalue(L, -nup);
    twoface_pushcclosure(L, l->func, nup);
    lua_setfield(L, -(nup+2), l->name);
  }
  lua_pop(L, nup);  /* remove upvalues */
}

void twofaceL_register(lua_State *L, const char *libname, const luaL_Reg *l) {
  twofaceL_openlib(L, libname, l, 0);
}

int twofaceL_typerror(lua_State *L, int narg, const char *tname) {
  int pushed_globals = adjust_indicies_1(L, &narg);
  const char *msg = lua_pushfstring(L, "%s expected, got %s", tname, luaL_typename(L, narg));
  return luaL_argerror(L, narg, msg);
}
