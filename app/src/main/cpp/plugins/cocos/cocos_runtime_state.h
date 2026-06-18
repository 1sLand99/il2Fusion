#ifndef IL2FUSION_PLUGINS_COCOS_RUNTIME_STATE_H
#define IL2FUSION_PLUGINS_COCOS_RUNTIME_STATE_H

#include "cocos_runtime.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace cocos_runtime::internal {

constexpr size_t kMaxLuaHookSlots = 16;
constexpr size_t kMaxLabelHookSlots = 192;
constexpr size_t kMaxLuaBindingHookSlots = 80;
constexpr size_t kMaxTextBytes = 4096;
constexpr size_t kPreviewBytes = 240;
constexpr int kLuaTypeString = 4;
constexpr int kMaxTextHookArgs = 8;

struct RuntimeConfig {
    bool enabled = false;
    bool runtime_text_capture = false;
    bool label_hooks = false;
    bool lua_binding_hooks = false;
    bool fairygui_hooks = false;
    bool text_replacement_enabled = false;
    bool text_replacement_delay_enabled = false;
    bool text_burst_delay_guard_enabled = false;
    bool text_persistence_enabled = true;
    bool text_persistence_chinese_only = true;
    FontReplacementConfig font_replacement;
    std::string text_replacement_value;
    std::int32_t text_replacement_delay_ms = 300;
    std::int32_t text_burst_hold_ms = 250;
    int lua_binding_max_stack_args = 8;
    std::vector<LuaReplacementRule> lua_rules;
};

struct RuntimeState {
    std::mutex mutex;
    std::string process_name = "unknown";
    RuntimeConfig config;
    std::atomic_bool worker_started{false};
    std::unordered_set<uintptr_t> installed_entries;
};

RuntimeState& State();
RuntimeConfig SnapshotConfig();
std::string CurrentProcessName();
std::vector<std::string> DefaultLuaLibs();

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_RUNTIME_STATE_H
