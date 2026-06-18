#include "cocos_runtime.h"

#include "cocos_lua_interceptor.h"
#include "cocos_log.h"
#include "cocos_runtime_state.h"
#include "cocos_text_capture.h"

#include "../../utils/db.h"
#include "../../utils/hook_backend.h"
#include "../../utils/utils.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cocos_runtime {
namespace internal {

constexpr std::int32_t kMaxTextReplacementDelayMs = 5000;
constexpr std::int32_t kMinTextBurstHoldMs = 50;
constexpr std::int32_t kMaxTextBurstHoldMs = 5000;

std::int32_t ClampTextReplacementDelayMs(std::int32_t delay_ms) {
    return std::clamp(delay_ms, static_cast<std::int32_t>(0), kMaxTextReplacementDelayMs);
}

void RefreshTextHookFlagsLocked(RuntimeState& state) {
    const bool text_pipeline_enabled =
        state.config.runtime_text_capture ||
        state.config.text_replacement_enabled;
    const bool ttf_font_pipeline_enabled =
        state.config.font_replacement.enabled &&
        !state.config.font_replacement.ttf_path.empty();
    const bool bmfont_pipeline_enabled =
        state.config.font_replacement.enabled &&
        !state.config.font_replacement.bmfont_fnt_path.empty();
    const bool font_pipeline_enabled = ttf_font_pipeline_enabled || bmfont_pipeline_enabled;

    // Enable both Cocos routes by default. During module scan, FairyGUI symbols
    // switch that module to native setter routing and suppress Lua binding text hooks.
    state.config.label_hooks = text_pipeline_enabled || font_pipeline_enabled;
    state.config.lua_binding_hooks = text_pipeline_enabled || font_pipeline_enabled;
    state.config.fairygui_hooks = text_pipeline_enabled || ttf_font_pipeline_enabled;
    state.config.lua_binding_max_stack_args = 8;
}

void InstallHooksForModule(const std::string& module_name, const hookutils::ModuleInfo& module_info) {
    InstallLuaHooksForModule(module_name, module_info);
    InstallTextCaptureHooksForModule(module_name, module_info);
}

void WorkerMain() {
    const RuntimeConfig config = SnapshotConfig();
    if (!config.enabled) {
        COCOS_LOGI("worker skipped: runtime disabled");
        return;
    }

    const std::vector<std::string> libs = DefaultLuaLibs();
    COCOS_LOGI("worker start libs=%zu lua_rules=%zu text_capture=%d text_persist=%d chinese_only=%d replace_delay=%d replace_delay_ms=%d text_burst_guard=%d text_burst_hold_ms=%d font_replace=%d cocos_native=%d cocos_lua=%d fairygui_detect=%d",
               libs.size(),
               config.lua_rules.size(),
               config.runtime_text_capture ? 1 : 0,
               config.text_persistence_enabled ? 1 : 0,
               config.text_persistence_chinese_only ? 1 : 0,
               config.text_replacement_delay_enabled ? 1 : 0,
               config.text_replacement_delay_ms,
               config.text_burst_delay_guard_enabled ? 1 : 0,
               config.text_burst_hold_ms,
               config.font_replacement.enabled ? 1 : 0,
               config.label_hooks ? 1 : 0,
               config.lua_binding_hooks ? 1 : 0,
               config.fairygui_hooks ? 1 : 0);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::unordered_set<std::string> pending(libs.begin(), libs.end());
    while (!pending.empty() && std::chrono::steady_clock::now() < deadline) {
        std::vector<std::string> resolved;
        for (const auto& lib : pending) {
            hookutils::ModuleInfo info;
            if (!hookutils::GetModuleInfo(lib.c_str(), &info)) {
                continue;
            }
            COCOS_LOGI("%s loaded @ 0x%" PRIxPTR " path=%s",
                       lib.c_str(),
                       info.load_bias,
                       info.path.c_str());
            InstallHooksForModule(lib, info);
            resolved.push_back(lib);
        }
        for (const auto& lib : resolved) {
            pending.erase(lib);
        }
        if (!pending.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    for (const auto& lib : pending) {
        COCOS_LOGI("wait timeout for %s", lib.c_str());
    }
}

void StartWorkerIfNeededLocked(RuntimeState& state) {
    if (!state.config.enabled) {
        return;
    }
    if (state.worker_started.exchange(true)) {
        COCOS_LOGI("worker already started; config changes require target process restart");
        return;
    }
    std::thread(WorkerMain).detach();
}

}  // namespace internal

void Init(const std::string& process_name) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.process_name = process_name.empty() ? "unknown" : process_name;
    internal::StartWorkerIfNeededLocked(state);
}

void SetHookBackend(std::int32_t backend_value) {
    hook_backend::SetPreferredBackend(hook_backend::BackendFromOrdinal(backend_value));
}

void SetEnabled(bool enabled) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.enabled = enabled;
    COCOS_LOGI("set enabled=%d", enabled ? 1 : 0);
}

void SetRuntimeTextCaptureEnabled(bool enabled) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.runtime_text_capture = enabled;
    internal::RefreshTextHookFlagsLocked(state);
    COCOS_LOGI("set text capture=%d cocos_native=%d cocos_lua=%d fairygui_detect=%d",
               enabled ? 1 : 0,
               state.config.label_hooks ? 1 : 0,
               state.config.lua_binding_hooks ? 1 : 0,
               state.config.fairygui_hooks ? 1 : 0);
}

void SetTextPersistence(bool enabled, bool chinese_only) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.text_persistence_enabled = enabled;
    state.config.text_persistence_chinese_only = chinese_only;
    COCOS_LOGI("set text persistence=%d chinese_only=%d",
               enabled ? 1 : 0,
               chinese_only ? 1 : 0);
}

void SetFontReplacement(FontReplacementConfig config) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.font_replacement = std::move(config);
    internal::RefreshTextHookFlagsLocked(state);
    COCOS_LOGI("set font replacement=%d ttf_len=%zu bmfont_fnt_len=%zu cocos_native=%d cocos_lua=%d fairygui_detect=%d",
               state.config.font_replacement.enabled ? 1 : 0,
               state.config.font_replacement.ttf_path.size(),
               state.config.font_replacement.bmfont_fnt_path.size(),
               state.config.label_hooks ? 1 : 0,
               state.config.lua_binding_hooks ? 1 : 0,
               state.config.fairygui_hooks ? 1 : 0);
}

void SetTextReplacement(bool enabled, const std::string& text) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.text_replacement_enabled = enabled;
    state.config.text_replacement_value = text;
    internal::RefreshTextHookFlagsLocked(state);
    COCOS_LOGI("set text replacement=%d length=%zu cocos_lua=%d fairygui_detect=%d",
               enabled ? 1 : 0,
               text.size(),
               state.config.lua_binding_hooks ? 1 : 0,
               state.config.fairygui_hooks ? 1 : 0);
}

void SetTextReplacementDelay(bool enabled, std::int32_t delay_ms) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.text_replacement_delay_enabled = enabled;
    state.config.text_replacement_delay_ms = internal::ClampTextReplacementDelayMs(delay_ms);
    COCOS_LOGI("set text replacement delay=%d ms=%d",
               enabled ? 1 : 0,
               state.config.text_replacement_delay_ms);
}

void SetTextBurstDelayGuard(bool enabled, std::int32_t hold_ms) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.text_burst_delay_guard_enabled = enabled;
    state.config.text_burst_hold_ms = std::clamp(
        hold_ms,
        internal::kMinTextBurstHoldMs,
        internal::kMaxTextBurstHoldMs);
    COCOS_LOGI("set text burst guard=%d hold_ms=%d",
               enabled ? 1 : 0,
               state.config.text_burst_hold_ms);
}

void UpdateLuaReplacementRules(std::vector<LuaReplacementRule> rules) {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    state.config.lua_rules = std::move(rules);
    COCOS_LOGI("set lua replacement rules=%zu", state.config.lua_rules.size());
}

void StartRuntimeIfNeeded() {
    internal::RuntimeState& state = internal::State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    if (state.config.enabled) {
        textdb::Init(state.process_name, true, textdb::ResetOnMainProcess());
    }
    COCOS_LOGI("config ready enabled=%d lua_rules=%zu text_capture=%d text_persist=%d chinese_only=%d replace_delay=%d replace_delay_ms=%d text_burst_guard=%d text_burst_hold_ms=%d font_replace=%d cocos_native=%d cocos_lua=%d fairygui_detect=%d",
               state.config.enabled ? 1 : 0,
               state.config.lua_rules.size(),
               state.config.runtime_text_capture ? 1 : 0,
               state.config.text_persistence_enabled ? 1 : 0,
               state.config.text_persistence_chinese_only ? 1 : 0,
               state.config.text_replacement_delay_enabled ? 1 : 0,
               state.config.text_replacement_delay_ms,
               state.config.text_burst_delay_guard_enabled ? 1 : 0,
               state.config.text_burst_hold_ms,
               state.config.font_replacement.enabled ? 1 : 0,
               state.config.label_hooks ? 1 : 0,
               state.config.lua_binding_hooks ? 1 : 0,
               state.config.fairygui_hooks ? 1 : 0);
    internal::StartWorkerIfNeededLocked(state);
}

}  // namespace cocos_runtime
