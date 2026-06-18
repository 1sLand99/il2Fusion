#ifndef IL2FUSION_PLUGINS_COCOS_LUA_INTERCEPTOR_H
#define IL2FUSION_PLUGINS_COCOS_LUA_INTERCEPTOR_H

#include "cocos_runtime_state.h"

#include "../../utils/utils.h"

#include <string>

namespace cocos_runtime::internal {

bool HasEnabledLuaRules(const RuntimeConfig& config);
void InstallLuaHooksForModule(const std::string& module_name,
                              const hookutils::ModuleInfo& module_info);

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_LUA_INTERCEPTOR_H
