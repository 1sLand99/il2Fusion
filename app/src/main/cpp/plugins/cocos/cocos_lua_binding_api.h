#ifndef IL2FUSION_PLUGINS_COCOS_LUA_BINDING_API_H
#define IL2FUSION_PLUGINS_COCOS_LUA_BINDING_API_H

#include <cstddef>

namespace cocos_runtime::internal {

using LuaCFunction = int (*)(void*);
using LuaGetTopFn = int (*)(void*);
using LuaSetTopFn = void (*)(void*, int);
using LuaTypeFn = int (*)(void*, int);
using LuaToLStringFn = const char* (*)(void*, int, size_t*);
using LuaToIntegerFn = long long (*)(void*, int);
using LuaPushLStringFn = const char* (*)(void*, const char*, size_t);
using LuaPushIntegerFn = void (*)(void*, long long);
using LuaPushNilFn = void (*)(void*);
using LuaReplaceFn = void (*)(void*, int);
using LuaSetFieldFn = void (*)(void*, int, const char*);
using LuaPushValueFn = void (*)(void*, int);
using LuaRawGetIFn = void (*)(void*, int, int);
using LuaLRefFn = int (*)(void*, int);
using LuaLUnrefFn = void (*)(void*, int, int);

struct LuaBindingApi {
    LuaGetTopFn gettop = nullptr;
    LuaSetTopFn settop = nullptr;
    LuaTypeFn type = nullptr;
    LuaToLStringFn tolstring = nullptr;
    LuaToIntegerFn tointeger = nullptr;
    LuaPushLStringFn pushlstring = nullptr;
    LuaPushIntegerFn pushinteger = nullptr;
    LuaPushNilFn pushnil = nullptr;
    LuaReplaceFn replace = nullptr;
    LuaSetFieldFn setfield = nullptr;
    LuaPushValueFn pushvalue = nullptr;
    LuaRawGetIFn rawgeti = nullptr;
    LuaLRefFn lref = nullptr;
    LuaLUnrefFn lunref = nullptr;
};

inline bool HasLuaBindingReadApi(const LuaBindingApi& api) {
    return api.gettop != nullptr && api.type != nullptr && api.tolstring != nullptr;
}

inline bool HasLuaBindingReplacementApi(const LuaBindingApi& api) {
    return api.pushlstring != nullptr && api.replace != nullptr;
}

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_LUA_BINDING_API_H
