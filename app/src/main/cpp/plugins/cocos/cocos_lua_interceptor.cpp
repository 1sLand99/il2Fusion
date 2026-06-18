#include "cocos_lua_interceptor.h"

#include "cocos_log.h"

#include "../../utils/elf_symbols.h"
#include "../../utils/hook_backend.h"
#include "../../utils/text_utils.h"

#include <array>
#include <atomic>
#include <cinttypes>
#include <mutex>
#include <utility>

namespace cocos_runtime::internal {
namespace {

enum class LuaHookKind {
    kLoadBuffer,
    kLoadBufferX,
};

using LuaLoadBufferFn = int (*)(void*, const char*, size_t, const char*);
using LuaLoadBufferXFn = int (*)(void*, const char*, size_t, const char*, const char*);

struct LuaHookSlot {
    std::atomic_bool active{false};
    uintptr_t entry = 0;
    LuaHookKind kind = LuaHookKind::kLoadBuffer;
    std::string label;
    std::string module_name;
    std::string symbol_name;
    LuaLoadBufferFn original_loadbuffer = nullptr;
    LuaLoadBufferXFn original_loadbufferx = nullptr;
    hook_backend::Backend backend = hook_backend::Backend::kAnd64InlineHook;
};

std::array<LuaHookSlot, kMaxLuaHookSlots> g_lua_slots{};

bool IsLuaBytecode(const char* data, size_t size) {
    return data != nullptr && size >= 4 &&
           static_cast<unsigned char>(data[0]) == 0x1B &&
           data[1] == 'L' && data[2] == 'u' && data[3] == 'a';
}

bool RuleMatchesScript(const LuaReplacementRule& rule, const std::string& script_name) {
    const std::string needle = textutils::Trim(rule.script_name);
    if (needle.empty()) {
        return false;
    }
    if (script_name == needle || textutils::Basename(script_name) == needle) {
        return true;
    }
    return script_name.find(needle) != std::string::npos;
}

size_t ReplaceAll(std::string* text, const std::string& from, const std::string& to) {
    if (text == nullptr || from.empty()) {
        return 0;
    }
    size_t count = 0;
    size_t cursor = 0;
    while ((cursor = text->find(from, cursor)) != std::string::npos) {
        text->replace(cursor, from.size(), to);
        cursor += to.size();
        ++count;
        if (count >= 64) {
            break;
        }
    }
    return count;
}

bool HasMatchingLuaRule(const RuntimeConfig& config, const std::string& script_name) {
    for (const auto& rule : config.lua_rules) {
        if (rule.enabled && RuleMatchesScript(rule, script_name) &&
            (!rule.replaced_code.empty() || !rule.prepend_code.empty())) {
            return true;
        }
    }
    return false;
}

bool ApplyLuaReplacementRules(const LuaHookSlot& slot,
                              const RuntimeConfig& config,
                              const char* buffer,
                              size_t size,
                              const std::string& script_name,
                              std::string* patched_out) {
    if (patched_out == nullptr || buffer == nullptr || size == 0 ||
        config.lua_rules.empty() || IsLuaBytecode(buffer, size) ||
        !HasMatchingLuaRule(config, script_name)) {
        return false;
    }

    bool changed = false;
    std::string patched(buffer, size);
    for (const auto& rule : config.lua_rules) {
        if (!rule.enabled || !RuleMatchesScript(rule, script_name)) {
            continue;
        }

        size_t replacements = 0;
        if (!rule.replaced_code.empty()) {
            replacements = ReplaceAll(&patched, rule.replaced_code, rule.replace_code);
        }
        bool prepended = false;
        if (!rule.prepend_code.empty()) {
            patched.insert(0, rule.prepend_code);
            prepended = true;
        }

        if (replacements > 0 || prepended) {
            changed = true;
            COCOS_EVENT_LOGI(cocos_log::kLuaReplace,
                             "%s name=%s rule=%s replacements=%zu prepend=%d",
                             slot.label.c_str(),
                             script_name.c_str(),
                             rule.script_name.c_str(),
                             replacements,
                             prepended ? 1 : 0);
        }
    }

    if (!changed) {
        return false;
    }
    *patched_out = std::move(patched);
    return true;
}

bool HandleLuaChunk(const LuaHookSlot& slot,
                    const char* buffer,
                    size_t size,
                    const char* script_name_raw,
                    std::string* patched_out) {
    if (buffer == nullptr || size == 0) {
        return false;
    }

    const RuntimeConfig config = SnapshotConfig();
    const std::string script_name = script_name_raw != nullptr && *script_name_raw != '\0'
        ? std::string(script_name_raw)
        : std::string("<anonymous>");

    return ApplyLuaReplacementRules(slot, config, buffer, size, script_name, patched_out);
}

template <size_t Slot>
int LuaLoadBufferReplacement(void* lua_state, const char* buffer, size_t size, const char* name) {
    LuaHookSlot& slot = g_lua_slots[Slot];
    std::string patched;
    const char* effective_buffer = buffer;
    size_t effective_size = size;
    if (slot.active.load(std::memory_order_acquire)) {
        if (HandleLuaChunk(slot, buffer, size, name, &patched)) {
            effective_buffer = patched.data();
            effective_size = patched.size();
        }
    }
    LuaLoadBufferFn original = slot.original_loadbuffer;
    if (original != nullptr) {
        return original(lua_state, effective_buffer, effective_size, name);
    }
    return 0;
}

template <size_t Slot>
int LuaLoadBufferXReplacement(void* lua_state,
                              const char* buffer,
                              size_t size,
                              const char* name,
                              const char* mode) {
    LuaHookSlot& slot = g_lua_slots[Slot];
    std::string patched;
    const char* effective_buffer = buffer;
    size_t effective_size = size;
    if (slot.active.load(std::memory_order_acquire)) {
        if (HandleLuaChunk(slot, buffer, size, name, &patched)) {
            effective_buffer = patched.data();
            effective_size = patched.size();
        }
    }
    LuaLoadBufferXFn original = slot.original_loadbufferx;
    if (original != nullptr) {
        return original(lua_state, effective_buffer, effective_size, name, mode);
    }
    return 0;
}

template <size_t... Slots>
constexpr std::array<LuaLoadBufferFn, sizeof...(Slots)> MakeLoadBufferReplacements(std::index_sequence<Slots...>) {
    return { &LuaLoadBufferReplacement<Slots>... };
}

template <size_t... Slots>
constexpr std::array<LuaLoadBufferXFn, sizeof...(Slots)> MakeLoadBufferXReplacements(std::index_sequence<Slots...>) {
    return { &LuaLoadBufferXReplacement<Slots>... };
}

const auto kLoadBufferReplacements = MakeLoadBufferReplacements(std::make_index_sequence<kMaxLuaHookSlots>{});
const auto kLoadBufferXReplacements = MakeLoadBufferXReplacements(std::make_index_sequence<kMaxLuaHookSlots>{});

int FindFreeLuaLoadSlotLocked() {
    for (size_t i = 0; i < g_lua_slots.size(); ++i) {
        if (!g_lua_slots[i].active.load(std::memory_order_relaxed) && g_lua_slots[i].entry == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool InstallLuaHook(const std::string& module_name,
                    const hookutils::ModuleInfo& module_info,
                    const char* symbol_name,
                    LuaHookKind kind) {
    const uintptr_t entry = elf_symbols::FindExportAddress(module_info, symbol_name);
    if (entry == 0) {
        COCOS_LOGI("%s not exported by %s", symbol_name, module_name.c_str());
        return false;
    }
    if (!hookutils::IsExecutableAddressInModule(entry, module_name.c_str())) {
        COCOS_LOGW("skip %s in %s: 0x%" PRIxPTR " is not executable",
                   symbol_name,
                   module_name.c_str(),
                   entry);
        return false;
    }

    RuntimeState& state = State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    if (state.installed_entries.find(entry) != state.installed_entries.end()) {
        return false;
    }
    const int slot_index = FindFreeLuaLoadSlotLocked();
    if (slot_index < 0) {
        COCOS_LOGE("no free Lua hook slot for %s", symbol_name);
        return false;
    }

    LuaHookSlot& slot = g_lua_slots[static_cast<size_t>(slot_index)];
    slot.active.store(false, std::memory_order_relaxed);
    slot.entry = entry;
    slot.kind = kind;
    slot.module_name = module_name;
    slot.symbol_name = symbol_name;
    slot.label = module_name + "!" + symbol_name;
    slot.backend = hook_backend::GetPreferredBackend();
    slot.original_loadbuffer = nullptr;
    slot.original_loadbufferx = nullptr;

    void* replacement = nullptr;
    void** original = nullptr;
    if (kind == LuaHookKind::kLoadBuffer) {
        replacement = reinterpret_cast<void*>(kLoadBufferReplacements[static_cast<size_t>(slot_index)]);
        original = reinterpret_cast<void**>(&slot.original_loadbuffer);
    } else {
        replacement = reinterpret_cast<void*>(kLoadBufferXReplacements[static_cast<size_t>(slot_index)]);
        original = reinterpret_cast<void**>(&slot.original_loadbufferx);
    }

    if (!hook_backend::InstallInlineHook(
            reinterpret_cast<void*>(entry),
            replacement,
            original,
            slot.label.c_str(),
            slot.backend)) {
        slot.entry = 0;
        slot.module_name.clear();
        slot.symbol_name.clear();
        slot.label.clear();
        slot.original_loadbuffer = nullptr;
        slot.original_loadbufferx = nullptr;
        return false;
    }

    slot.active.store(true, std::memory_order_release);
    state.installed_entries.insert(entry);
    COCOS_LOGI("hook installed: %s @ 0x%" PRIxPTR " slot=%d backend=%s",
               slot.label.c_str(),
               entry,
               slot_index,
               hook_backend::BackendName(slot.backend));
    return true;
}

}  // namespace

bool HasEnabledLuaRules(const RuntimeConfig& config) {
    for (const auto& rule : config.lua_rules) {
        if (rule.enabled && !textutils::Trim(rule.script_name).empty() &&
            (!rule.replaced_code.empty() || !rule.prepend_code.empty())) {
            return true;
        }
    }
    return false;
}

void InstallLuaHooksForModule(const std::string& module_name,
                              const hookutils::ModuleInfo& module_info) {
    const RuntimeConfig config = SnapshotConfig();
    if (!HasEnabledLuaRules(config)) {
        COCOS_LOGI("lua replacement disabled; skip luaL_loadbuffer hooks for %s", module_name.c_str());
        return;
    }

    InstallLuaHook(module_name, module_info, "luaL_loadbuffer", LuaHookKind::kLoadBuffer);
    InstallLuaHook(module_name, module_info, "luaL_loadbufferx", LuaHookKind::kLoadBufferX);
}

}  // namespace cocos_runtime::internal
