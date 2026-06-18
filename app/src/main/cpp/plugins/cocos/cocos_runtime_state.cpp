#include "cocos_runtime_state.h"

namespace cocos_runtime::internal {
namespace {

RuntimeState g_state;

}  // namespace

RuntimeState& State() {
    return g_state;
}

RuntimeConfig SnapshotConfig() {
    std::lock_guard<std::mutex> _lk(g_state.mutex);
    return g_state.config;
}

std::string CurrentProcessName() {
    std::lock_guard<std::mutex> _lk(g_state.mutex);
    return g_state.process_name;
}

std::vector<std::string> DefaultLuaLibs() {
    return {
        "libcocos2dlua.so",
        "libcocos2dcpp.so",
        "libgame.so",
        "libnative.so",
    };
}

}  // namespace cocos_runtime::internal
