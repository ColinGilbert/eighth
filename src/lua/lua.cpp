
#include "eight/lua/lua.h"
#include "eight/lua/bindlua.h"
#include "eight/core/test.h"
#include "eight/core/debug.h"
#include <stdio.h>

using namespace eight;
using namespace lua;

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

LuaState::LuaState(PackageLoader* loader)
{
	L = luaL_newstate(); //todo - pass allocator
	eiCheckLuaStack(L);
	luaL_openlibs(L);
	RegisterEngine(L);
	if( loader )
		SetPackageLoader(L, &DefaultPackageLoaderCallback, loader);
}
LuaState::~LuaState()
{
	lua_close(L);
}

//------------------------------------------------------------------------------

#ifndef eiBUILD_NO_LUA_TYPESAFETY
const char* const eight::lua::internal::LuaPointer::s_metatable = "eight.ptr";
#endif
const char* const eight::lua::internal::LuaBoxBase::s_metatable = "eight.box";

void lua::internal::PushMetatable(lua_State *L, void* name)
{
	lua_pushlightuserdata(L, name);
	lua_rawget(L, LUA_REGISTRYINDEX);
	eiASSERT( !lua_isnil(L, -1) );
}
static void PushNewMetatable(lua_State *L, void* name)
{
	lua_pushlightuserdata(L, name);
	lua_rawget(L, LUA_REGISTRYINDEX);  /* get registry.name */
	if (!lua_isnil(L, -1))  /* name already in use? */
		return;  /* leave previous value on top */
	lua_pop(L, 1);
	lua_newtable(L);  /* create metatable */
	lua_pushlightuserdata(L, name);
	lua_pushvalue(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);  /* registry.name = metatable */
}
static void RegisterMetatable(lua_State *L, const char* name)
{
	eiCheckLuaStack(L);
	PushNewMetatable(L, (void*)name);
	lua_pushlightuserdata(L, (void*)name);
	lua_pushvalue(L, -1);
	lua_rawset(L, -3); // metatable[(void*)name] = (void*)name
#if eiDEBUG_LEVEL > 0
	lua_pushstring(L, "name");
	lua_pushstring(L, name);
	lua_rawset(L, -3); // metatable["name"] = name
#endif
	lua_pop(L, 1);//pop metatable
}

void lua::SetPackageLoader(lua_State* L, lua_CFunction f, PackageLoader* data)
{
	eiCheckLuaStack(L);
	/*
	// replace the package loaders table
	lua_getglobal(L, "package");
	lua_createtable(L, 1, 0);
	lua_pushstring(L, "loaders");
	lua_pushvalue(L, -2);
	lua_settable(L, -4);//package.loaders = {}
	lua_pushinteger(L, 0);
	lua_pushlightuserdata(L, data);
    lua_pushcclosure(L, f, 1);
	lua_settable(L, -3);//package.loaders[0] = closure(f, data)
	lua_pop(L, 2);*/
	
	lua_getglobal(L, "package");
	lua_pushstring(L, "loaders");
	lua_gettable(L, -2);
	lua_pushlightuserdata(L, data);
    lua_pushcclosure(L, f, 1);
	lua_rawseti (L, -2, lua_objlen(L, -2)+1);//package.loaders[package.loaders.length+1] = closure(f, data)
	lua_pop(L, 2);
}

int lua::DefaultPackageLoaderCallback(lua_State* L)
{
	const char* name = lua_tostring(L, 1);
	PackageLoader* loader = (PackageLoader*)lua_topointer(L, lua_upvalueindex(1));
	LoadedPackage data;
	if( !loader || !loader->pfn )
	{
		lua_pushstring(L, "package loader initialization error");
		return 1;
	}
	else
	{
		data = loader->pfn(loader->userdata, name);
		if( data.bytes != NULL )
		{
			eiASSERT( data.numBytes && data.filename );
			eiDEBUG( int ok = ) luaL_loadbuffer(L, data.bytes, data.numBytes, data.filename);
			eiASSERT( ok == 0 );
			lua_pushstring(L, data.filename);
			return 2;
		}
	}
	lua_pushstring(L, "file not found");
	return 1;
}

void lua::RegisterEngine(lua_State* L)
{
#ifndef eiBUILD_NO_LUA_TYPESAFETY
	RegisterMetatable(L, internal::LuaPointer::s_metatable);
#endif
	RegisterMetatable(L, internal::LuaBoxBase::s_metatable);
}

static void PushNewGlobalTable(lua_State *L, const char* name, int nameLen)
{
	lua_pushlstring(L, name, nameLen);
	lua_pushvalue(L, -1);
	lua_rawget(L, LUA_GLOBALSINDEX);  /* get name */
	if (!lua_isnil(L, -1))  // name already in use?
	{
		lua_remove(L, -2); //remove name
		return;  // leave previous value on top
	}
	lua_pop(L, 1); // pop the nil
	lua_newtable(L);//todo - size hint?
	lua_pushvalue(L, -2);//push name
	lua_pushvalue(L, -2);//push table
	lua_rawset(L, LUA_GLOBALSINDEX);  // globals.name = table
	lua_remove(L, -2); //remove name
}

void lua::RegisterType(lua_State* L, const TypeBinding& type)
{
	int nameLen = strlen(type.name);
	const char pushSuffix[] = "_Push"; eiSTATIC_ASSERT(sizeof(pushSuffix) == 6);//includes terminator
	char pushTableName[128] = {0};
	eiASSERT( nameLen+sizeof(pushSuffix) <= 128 );
	memcpy(pushTableName, type.name, nameLen);
	memcpy(pushTableName+nameLen, pushSuffix, sizeof(pushSuffix));
	PushNewGlobalTable(L, type.name, nameLen);
	PushNewGlobalTable(L, pushTableName, nameLen+sizeof(pushSuffix)-1);
	for( uint i=0, end=type.methodCount; i!=end; ++i )
	{
		const MethodBinding& method = type.method[i];
		if( method.luaCall )
		{
			lua_pushstring(L, method.name);
			lua_pushcfunction(L, method.luaCall);
			lua_rawset(L, -4);//table[name] = luaCall
		}
		if( method.luaPush )
		{
			lua_pushstring(L, method.name);
			lua_pushcfunction(L, method.luaCall);
			lua_rawset(L, -3);
		}
	}
	lua_pop(L, 2); // pop the tables
}

void lua::internal::LuaTypeError(lua_State* L, int i, const char* expected, const char* received)
{
	char buffer[256];
	_snprintf(buffer, 256, "Expected %s, received %s", expected, received);
	buffer[255] = 0;
	luaL_argcheck(L, false, i, buffer);
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

namespace { 
	struct Stuff { int value; };
	class Test
	{
	public:
		eiBind(Test);
		Stuff DoStuff(int i, Test*, const char*) { Stuff s = {i}; printf("DoStuff"); return s; }
		void PrintStuff1(Stuff  s) { printf("1: %d\n", s.value); }
		Stuff* PrintStuff2(Stuff* s) { printf("2: %d\n", s->value); return s; }
		void PrintStuff3(Stuff& s) { printf("3: %d\n", s.value); }
		void FooBar(int*, const float*) { }
	private:
		int m_stuff;
	};
}

eiBindClass( Test )
	eiLuaBind( LuaCall )
	eiBeginMethods()
		eiBindMethod( DoStuff )
		eiBindMethod( PrintStuff1 )
		eiBindMethod( PrintStuff2 )
		eiBindMethod( PrintStuff3 )
	eiEndMethods()
eiEndBind();

static LoadedPackage TestLoader(void* userdata, const char* name)
{
	eiASSERT( userdata == (void*)0x1337 );
	eiASSERT( 0 == strcmp(name, "testModule") );
	const char* code =
		"LuaTest = LuaTest or {}\n"
		"LuaTest.__index = LuaTest\n"
		"function LuaTest:DoStuff()\n"
		"	Test.DoStuff(self.member, 42, self.member, 'member')\n"
		"end\n"
		"function NewLuaTest(x)\n"
		"	local object = { member = x }\n"
		"	setmetatable(object, LuaTest)\n"
		"	object.member = x\n"
		"	return object\n"
		"end\n"
		;
	LoadedPackage data = {"testModule.txt", code, strlen(code) };
	return data;
}

eiTEST( Lua )
{
	char buffer[1024];
	StackAlloc stack(buffer, 1024);
	Scope a(stack, "main");
	Tilde* tilde = Tilde::New(a);
	PackageLoader testLoader = { (void*)0x1337, &TestLoader };
	LuaState lua( &testLoader );
	tilde->Register( lua );
	RegisterType(lua, ReflectTest());
	const char* code = 
		"print('code loaded')\n"
		"local module = require 'testModule'\n"
		"function init(test)\n"
		" print('called init')\n"
		" local stuff = Test.DoStuff(test, 42, nil, 'text')\n"
		" Test.PrintStuff1(test, stuff)\n"
		" local s2 = Test.PrintStuff2(test, stuff)\n"
		" Test.PrintStuff3(test, s2)\n"
		" local object = NewLuaTest(test)\n"
		" object:DoStuff()\n"
		" return test\r\n"
		"end\n"
		;
	int ok = luaL_loadbuffer(lua, code, strlen(code), "builtin.txt" );
//	int ok = luaL_loadstring(lua, code);
	eiASSERT( ok == 0 );
	ok = lua_pcall(lua, 0, LUA_MULTRET, 0);
	if( ok == LUA_ERRRUN )
	{
		const char* error = lua_tostring(lua, -1);
		printf(error);
		lua_pop(lua, 1);
	}
	eiASSERT( ok == 0 );
	Test test;
	if( IsDebuggerAttached() )
		tilde->WaitForDebuggerConnection();
	do
	{
		tilde->Poll();
		Test* result = PCall<Test*>(lua, "init", &test);
	} while( IsDebuggerAttached() );
}
