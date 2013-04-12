#include "twoface.h"
#include <lauxlib.h>
#include <lualib.h>
#include <intrin.h>
#define LUA51_ENVIRONINDEX	(-10001)
#define lua51_upvalueindex(i)	(LUA51_GLOBALSINDEX-(i))
struct lua51_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) `global', `local', `field', `method' */
  const char *what;	/* (S) `Lua', `C', `main', `tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int nups;		/* (u) number of upvalues */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  int i_ci;  /* active function */
};
struct luaL51_Buffer {
  char *p;			/* current position in buffer */
  int lvl;  /* number of strings in the stack (level) */
  lua_State *L;
  char buffer[LUAL_BUFFERSIZE];
};

int adjust_indicies_1(lua_State *L, int* idx1)
{
  if(*idx1 <= LUA51_REGISTRYINDEX)
  {
    switch(*idx1)
    {
    case LUA51_REGISTRYINDEX: *idx1 = LUA_REGISTRYINDEX; break;
    case LUA51_ENVIRONINDEX: *idx1 = lua_upvalueindex(1); break;
    case LUA51_GLOBALSINDEX: lua_pushglobaltable(L); *idx1 = lua_gettop(L); return 1;
    default: *idx1 = lua_upvalueindex(2 + (LUA51_GLOBALSINDEX - *idx1)); break;
    }
  }
  return 0;
}

int adjust_indicies_2(lua_State *L, int* idx1, int* idx2)
{
  if(*idx1 == LUA51_GLOBALSINDEX)
  {
    adjust_indicies_1(L, idx1);
    if(*idx2 == LUA51_GLOBALSINDEX)
    {
      *idx2 = *idx1;
    }
    else
    {
      if(LUA51_REGISTRYINDEX < *idx2 && *idx2 < 0)
        --*idx2;
      else
        adjust_indicies_1(L, idx2);
    }
    return 1;
  }
  else if(*idx2 == LUA51_GLOBALSINDEX)
  {
    return adjust_indicies_2(L, idx2, idx1);
  }
  else
  {
    return adjust_indicies_1(L, idx1), adjust_indicies_1(L, idx2);
  }
}

typedef struct {
  lua_Alloc alloc;
  void* ud;
} twoface_allocator_thunk_t;

static void* twoface_allocator_thunk(void *ud, void *ptr, size_t osize, size_t nsize)
{
  twoface_allocator_thunk_t* thunk = (twoface_allocator_thunk_t*)ud;
  if(ptr == NULL)
    osize = 0;
  return thunk->alloc(thunk->ud, ptr, osize, nsize);
}

static lua_Hook twoface_hook_thunk_get_original(lua_State *L)
{
  lua_Hook result;
  lua_rawgetp(L, LUA_REGISTRYINDEX, (void*)twoface_hook_thunk_get_original);
  result = (lua_Hook)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return result;
}

static void twoface_decode_debug(struct lua_Debug* ar52, const struct lua51_Debug *ar51)
{
  ar52->i_ci = (struct CallInfo*)ar51->i_ci;
#ifdef TWOFACE_64BIT
  *(__int64*)&ar52->i_ci |= (((__int64)(&ar51->event)[1]) << 32);
#endif
}

static void twoface_encode_debug(struct lua51_Debug* ar51, const struct lua_Debug *ar52)
{
  ar51->i_ci = (int)ar52->i_ci;
#ifdef TWOFACE_64BIT
  (&ar51->event)[1] = (int)(((__int64)ar52->i_ci) >> 32);
#endif
}

static void twoface_hook_thunk(lua_State *L, lua_Debug *ar)
{
  struct lua51_Debug ar51;
  ar51.event = ar->event;
  twoface_encode_debug(&ar51, ar);
  twoface_hook_thunk_get_original(L)(L, (lua_Debug*)&ar51);
}

static int twoface_closure_thunk(lua_State *L)
{
  return ((lua_CFunction)lua_touserdata(L, lua_upvalueindex(2)))(L);
}

void twoface_call(lua_State *L, int nargs, int nresults)
{
  lua_callk(L, nargs, nresults, 0, NULL);
}

void twoface_close(lua_State *L)
{
  void* ud;
  lua_Alloc alloc = lua_getallocf(L, &ud);
  lua_close(L);
  if(alloc == twoface_allocator_thunk)
  {
    twoface_allocator_thunk_t* thunk = (twoface_allocator_thunk_t*)ud;
    thunk->alloc(thunk->ud, thunk, sizeof(twoface_allocator_thunk_t), 0);
  }
}

int twoface_cpcall(lua_State *L, lua_CFunction func, void *ud)
{
  if(!lua_checkstack(L, 2))
    return LUA_ERRMEM;
  lua_pushcclosure(L, func, 0);
  lua_pushlightuserdata(L, ud);
  return lua_pcallk(L, 1, 0, 0, 0, NULL);
}

#define COMPARISON_THUNK(fn) \
int twoface_##fn(lua_State *L, int index1, int index2) \
{ \
  int pushed_globals = adjust_indicies_2(L, &index1, &index2); \
  int result = lua_##fn(L, index1, index2); \
  if(pushed_globals) \
    lua_pop(L, 1); \
  return result; \
}

#ifndef lua_equal
#define lua_equal(L, index1, index2) lua_compare((L), (index1), (index2), LUA_OPEQ)
#endif
COMPARISON_THUNK(equal)

lua_Alloc twoface_getallocf(lua_State *L, void **ud)
{
  void* our_ud;
  lua_Alloc result = lua_getallocf(L, &our_ud);
  if(result == twoface_allocator_thunk)
  {
    twoface_allocator_thunk_t* thunk = (twoface_allocator_thunk_t*)our_ud;
    result = thunk->alloc;
    our_ud = thunk->ud;
  }
  if(ud)
    *ud = our_ud;
  return result;
}

static int is_env(const char* str)
{
  return str[0] == '_' && str[1] == 'E' && str[2] == 'N' && str[3] == 'V' && str[4] == 0;
}

void twoface_getfenv(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  switch(lua_type(L, index))
  {
  case LUA_TFUNCTION:
    if(lua_iscfunction(L, index))
    {
      if(lua_getupvalue(L, index, 1) == NULL)
        lua_pushglobaltable(L);
      break;
    }
    else
    {
      int upidx = 0;
      const char* name;
      while((name = lua_getupvalue(L, index, ++upidx)))
      {
        if(*name == 0 || is_env(name))
          break;
        lua_pop(L, 1);
      }
      if(!name)
        lua_pushglobaltable(L);
      break;
    }
  case LUA_TUSERDATA: lua_getuservalue(L, index); break;
  case LUA_TTHREAD: lua_pushglobaltable(L); break;
  default: lua_pushnil(L); break;
  }
  if(pushed_globals)
    lua_replace(L, -2);
}

void twoface_getfield(lua_State *L, int index, const char *k)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  lua_getfield(L, index, k);
  if(pushed_globals)
    lua_replace(L, -2);
}

lua_Hook twoface_gethook(lua_State *L)
{
  lua_Hook result = lua_gethook(L);
  if(result = twoface_hook_thunk)
    result = twoface_hook_thunk_get_original(L);
  return result;
}

int twoface_getinfo(lua_State *L, const char *what, struct lua51_Debug *ar)
{
  int result;
  struct lua_Debug ar52;
  twoface_decode_debug(&ar52, ar);
  result = lua_getinfo(L, what, &ar52);
  ar->name = ar52.name;
  ar->namewhat = ar52.namewhat;
  ar->what = ar52.what;
  ar->source = ar52.source;
  ar->currentline = ar52.currentline;
  ar->nups = ar52.nups; // TODO: Subtract by two for twoface_closure_thunk?
  ar->linedefined = ar52.linedefined;
  ar->lastlinedefined = ar52.lastlinedefined;
  __movsd((unsigned long*)ar->short_src, (const unsigned long*)ar52.short_src, LUA_IDSIZE / sizeof(unsigned long));
  return result;
}

const char *twoface_getlocal(lua_State *L, struct lua51_Debug *ar, int n)
{
  struct lua_Debug ar52;
  twoface_decode_debug(&ar52, ar);
  return lua_getlocal(L, &ar52, n);
}

int twoface_getmetatable(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(lua_getmetatable(L, index))
  {
    if(pushed_globals)
      lua_replace(L, -2);
    return 1;
  }
  else
  {
    if(pushed_globals)
      lua_pop(L, 1);
    return 0;
  }
}

int twoface_getstack(lua_State *L, int level, struct lua51_Debug *ar)
{
  struct lua_Debug ar52;
  int result = lua_getstack(L, level, &ar52);
  twoface_encode_debug(ar, &ar52);
  return result;
}

void twoface_gettable(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -2);
    lua_gettable(L, -2);
    lua_replace(L, -2);
  }
  else
  {
    lua_gettable(L, index);
  }
}

const char *twoface_getupvalue(lua_State *L, int funcindex, int n)
{
  int pushed_globals = adjust_indicies_1(L, &funcindex);
  if(pushed_globals)
  {
    lua_pop(L, 1);
    return NULL;
  }
  if(lua_tocfunction(L, funcindex) == twoface_closure_thunk)
    n += 2;
  return lua_getupvalue(L, funcindex, n);
}

#define INSPECTION_THUNK(typ, fn) \
typ twoface##fn(lua_State *L, int index) \
{ \
  int pushed_globals = adjust_indicies_1(L, &index); \
  typ result = lua##fn(L, index); \
  if(pushed_globals) \
    lua_pop(L, 1); \
  return result; \
}

#define INSPECTION_THUNK_1(typ, fn, t1) \
typ twoface##fn(lua_State *L, int index, t1 a1) \
{ \
  int pushed_globals = adjust_indicies_1(L, &index); \
  typ result = lua##fn(L, index, a1); \
  if(pushed_globals) \
    lua_pop(L, 1); \
  return result; \
}

INSPECTION_THUNK(int, _iscfunction)
INSPECTION_THUNK(int, _isnumber)
INSPECTION_THUNK(int, _isstring)
INSPECTION_THUNK(int, _isuserdata)

#ifndef lua_lessthan
#define lua_lessthan(L, index1, index2) lua_compare((L), (index1), (index2), LUA_OPLT)
#endif
COMPARISON_THUNK(lessthan)

int twoface_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname)
{
  return lua_load(L, reader, data, chunkname, NULL);
}

lua_State *twoface_newstate(lua_Alloc f, void *ud)
{
  twoface_allocator_thunk_t* thunk = (twoface_allocator_thunk_t*)f(ud, NULL, 0, sizeof(twoface_allocator_thunk_t));
  if(thunk == NULL)
    return NULL;
  thunk->alloc = f;
  thunk->ud = ud;
  return lua_newstate(twoface_allocator_thunk, thunk);
}

int twoface_next(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    int result;
    lua_insert(L, -2);
    result = lua_next(L, -2);
    lua_remove(L, result ? -3 : -1);
    return result;
  }
  else
  {
    return lua_next(L, index);
  }
}

int twoface_pcall(lua_State *L, int nargs, int nresults, int errfunc)
{
  return lua_pcallk(L, nargs, nresults, errfunc, 0, NULL);
}

void twoface_pushcclosure(lua_State *L, lua_CFunction fn, int n)
{
  luaL_checkstack(L, 2, NULL);
  lua_pushglobaltable(L);
  lua_insert(L, -1 - n);
  lua_pushlightuserdata(L, (void*)fn);
  lua_insert(L, -1 - n);
  lua_pushcclosure(L, twoface_closure_thunk, n + 2);
}

void twoface_pushvalue(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(!pushed_globals)
    lua_pushvalue(L, index);
}

COMPARISON_THUNK(rawequal)

void twoface_rawget(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -2);
    lua_rawget(L, -2);
    lua_replace(L, -2);
  }
  else
  {
    lua_rawget(L, index);
  }
}

void twoface_rawgeti(lua_State *L, int index, int n)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -2);
    lua_rawgeti(L, -2, n);
    lua_replace(L, -2);
  }
  else
  {
    lua_rawgeti(L, index, n);
  }
}

void twoface_rawset(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -3);
    lua_rawset(L, -3);
    lua_pop(L, 1);
  }
  else
  {
    lua_rawset(L, index);
  }
}

void twoface_rawseti(lua_State *L, int index, int n)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -2);
    lua_rawseti(L, -2, n);
    lua_pop(L, 1);
  }
  else
  {
    lua_rawseti(L, index, n);
  }
}

void twoface_replace(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_pop(L, 1);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  }
  else
  {
    lua_replace(L, index);
  }
}

void twoface_setallocf(lua_State *L, lua_Alloc f, void *ud)
{
  void* existing_ud;
  if(lua_getallocf(L, &existing_ud) == twoface_allocator_thunk)
  {
    twoface_allocator_thunk_t* thunk = (twoface_allocator_thunk_t*)existing_ud;
    thunk->alloc = f;
    thunk->ud = ud;
  }
  else
  {
    twoface_allocator_thunk_t* thunk = (twoface_allocator_thunk_t*)f(ud, NULL, 0, sizeof(twoface_allocator_thunk_t));
    if(thunk != NULL)
    {
      thunk->alloc = f;
      thunk->ud = ud;
      lua_setallocf(L, twoface_allocator_thunk, thunk);
    }
  }
}

int twoface_setfenv(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_pop(L, 2);
    return 0;
  }
  else
  {
    switch(lua_type(L, index))
    {
    case LUA_TTHREAD: lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS); return 1;
    case LUA_TUSERDATA: lua_setuservalue(L, index); return 1;
    case LUA_TFUNCTION:
      if(lua_iscfunction(L, index))
      {
        if(lua_tocfunction(L, index) == twoface_closure_thunk)
        {
          lua_setupvalue(L, index, 1);
          return 1;
        }
      }
      else
      {
        int upidx = 0;
        const char* name;
        while((name = lua_getupvalue(L, index, ++upidx)))
        {
          if(*name == 0 || is_env(name))
          {
            lua_setupvalue(L, index, upidx);
            return 1;
          }
        }
      }
      /* fall-through */
    default: lua_pop(L, 1); return 0;
    }
  }
}

void twoface_setfield(lua_State *L, int index, const char *k)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -2);
    lua_setfield(L, -2, k);
    lua_pop(L, 1);
  }
  else
  {
    lua_setfield(L, index, k);
  }
}

int twoface_sethook(lua_State *L, lua_Hook f, int mask, int count)
{
  lua_pushlightuserdata(L, (void*)f);
  lua_rawsetp(L, LUA_REGISTRYINDEX, (void*)twoface_hook_thunk_get_original);
  return lua_sethook(L, twoface_hook_thunk, mask, count);
}

const char *twoface_setlocal(lua_State *L, struct lua51_Debug *ar, int n)
{
  struct lua_Debug ar52;
  twoface_decode_debug(&ar52, ar);
  return lua_setlocal(L, &ar52, n);
}

int twoface_setmetatable(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -2);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
  }
  else
  {
    lua_setmetatable(L, index);
  }
  return 1;
}

void twoface_settable(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  if(pushed_globals)
  {
    lua_insert(L, -3);
    lua_settable(L, -3);
    lua_pop(L, 1);
  }
  else
  {
    lua_settable(L, index);
  }
}

const char *twoface_setupvalue(lua_State *L, int funcindex, int n)
{
  int pushed_globals = adjust_indicies_1(L, &funcindex);
  if(pushed_globals)
  {
    lua_pop(L, 1);
    return NULL;
  }
  if(lua_tocfunction(L, funcindex) == twoface_closure_thunk)
    n += 2;
  return lua_setupvalue(L, funcindex, n);
}

INSPECTION_THUNK(int, _toboolean)

lua_CFunction twoface_tocfunction(lua_State *L, int index)
{
  int pushed_globals = adjust_indicies_1(L, &index);
  lua_CFunction result = lua_tocfunction(L, index);
  if(result == twoface_closure_thunk)
  {
    lua_getupvalue(L, index, 2);
    result = (lua_CFunction)lua_touserdata(L, -1);
    lua_pop(L, 1);
  }
  if(pushed_globals)
    lua_pop(L, 1);
  return result;
}

INSPECTION_THUNK(lua_Integer, _tointeger)
INSPECTION_THUNK_1(const char*, _tolstring, size_t*)
INSPECTION_THUNK(lua_Number, _tonumber)
INSPECTION_THUNK(const void*, _topointer)
INSPECTION_THUNK(lua_State*, _tothread)
INSPECTION_THUNK(void*, _touserdata)
INSPECTION_THUNK(int, _type)

int twoface_yield(lua_State *L, int nresults)
{
  return lua_yieldk(L, nresults, 0, NULL);
}

INSPECTION_THUNK_1(int, L_argerror, const char*)

int twofaceL_callmeta(lua_State *L, int obj, const char *e)
{
  int pushed_globals = adjust_indicies_1(L, &obj);
  int result = luaL_callmeta(L, obj, e);
  if(pushed_globals)
    lua_remove(L, result ? -2 : -1);
  return result;
}

void twofaceL_checkany(lua_State *L, int arg)
{
  int pushed_globals = adjust_indicies_1(L, &arg);
  luaL_checkany(L, arg);
  if(pushed_globals)
    lua_pop(L, 1);
}

INSPECTION_THUNK(lua_Integer, L_checkinteger)
INSPECTION_THUNK_1(const char*, L_checklstring, size_t*)
INSPECTION_THUNK(lua_Number, L_checknumber)

int twofaceL_checkoption(lua_State *L, int narg, const char *def, const char *const lst[])
{
  int pushed_globals = adjust_indicies_1(L, &narg);
  int result = luaL_checkoption(L, narg, def, lst);
  if(pushed_globals)
    lua_pop(L, 1);
  return result;
}

void twofaceL_checktype(lua_State *L, int narg, int t)
{
  int pushed_globals = adjust_indicies_1(L, &narg);
  luaL_checktype(L, narg, t);
  if(pushed_globals)
    lua_pop(L, 1);
}

INSPECTION_THUNK_1(void*, L_checkudata, const char*)

int twofaceL_getmetafield(lua_State *L, int obj, const char *e)
{
  int pushed_globals = adjust_indicies_1(L, &obj);
  int result = luaL_getmetafield(L, obj, e);
  if(pushed_globals)
    lua_remove(L, result ? -2 : -1);
  return result;
}

int twofaceL_loadbuffer(lua_State *L, const char *buff, size_t sz, const char *name)
{
  return luaL_loadbufferx(L, buff, sz, name, NULL);
}

int twofaceL_loadfile(lua_State *L, const char *filename)
{
  return luaL_loadfilex(L, filename, NULL);
}

INSPECTION_THUNK_1(lua_Integer, L_optinteger, lua_Integer)

const char *twofaceL_optlstring(lua_State *L, int narg, const char *d, size_t *l)
{
  int pushed_globals = adjust_indicies_1(L, &narg);
  const char *result = luaL_optlstring(L, narg, d, l);
  if(pushed_globals)
    lua_pop(L, 1);
  return result;
}

INSPECTION_THUNK_1(lua_Number, L_optnumber, lua_Number)

int twofaceL_ref(lua_State *L, int t)
{
  int pushed_globals = adjust_indicies_1(L, &t);
  if(pushed_globals)
  {
    int result;
    lua_insert(L, -2);
    result = luaL_ref(L, -2);
    lua_pop(L, 1);
    return result;
  }
  else
  {
    return luaL_ref(L, t);
  }
}

void twofaceL_unref(lua_State *L, int t, int ref)
{
  int pushed_globals = adjust_indicies_1(L, &t);
  luaL_unref(L, t, ref);
  if(pushed_globals)
    lua_pop(L, 1);
}
