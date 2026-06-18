#include "textExtractor.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <utility>
#include <type_traits>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../il2CppDumper/il2cpp-class.h"
#include "../../il2CppDumper/il2cpp-tabledefs.h"
#include "../../il2CppDumper/xdl/include/xdl.h"
#include "../../utils/db.h"
#include "../../utils/hook_backend.h"
#include "../../utils/log.h"
#include "../../utils/utils.h"
#include "unity_translation_store.h"

namespace text_extractor {
namespace {

constexpr const char* kLibIl2cpp = "libil2cpp.so";
constexpr size_t kMaxHookSlots = 128;

using SetterFn = void (*)(void*, textutils::Il2CppString*);

struct HookedMethod {
    std::string full;
    uintptr_t entry = 0;
    SetterFn original = nullptr;
    bool active = false;
    hook_backend::Backend backend = hook_backend::Backend::kAnd64InlineHook;
};

std::mutex g_hook_mutex;
std::vector<il2cpputils::TargetSpec> g_targets;
std::vector<void*> g_installed_targets;
std::array<HookedMethod, kMaxHookSlots> g_hook_slots{};
std::string g_process_name = "unknown";
std::atomic_bool g_worker_started{false};
std::atomic_bool g_il2cpp_ready{false};
std::atomic_bool g_api_initialized{false};
std::atomic_bool g_dobby_mode_configured{false};
std::atomic<uintptr_t> g_il2cpp_base{0};
il2cpputils::Il2CppApi g_api{};
std::mutex g_text_replacement_mutex;
bool g_text_replacement_enabled = false;
std::string g_text_replacement_value;

bool EnsureIl2cppApi() {
    if (g_api_initialized.load()) {
        return true;
    }

    hookutils::ModuleInfo module_info;
    const bool has_module_info = hookutils::GetModuleInfo(kLibIl2cpp, &module_info);
    void* handle = dlopen(kLibIl2cpp, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
    if (handle == nullptr) {
        if (has_module_info && !module_info.path.empty()) {
            handle = dlopen(module_info.path.c_str(), RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
        }
    }
    if (handle == nullptr) {
        handle = xdl_open(kLibIl2cpp, 0);
    }
    if (handle == nullptr) {
        if (has_module_info && !module_info.path.empty()) {
            handle = xdl_open(module_info.path.c_str(), 0);
        }
    }
    if (handle == nullptr) {
        // 最后尝试直接加载一次
        handle = dlopen(kLibIl2cpp, RTLD_NOW | RTLD_GLOBAL);
    }
    if (handle == nullptr) {
        LOGE("il2cpp dlopen 失败，无法初始化 API");
        return false;
    }

    if (!il2cpputils::ResolveIl2cppApi(handle, g_api)) {
        return false;
    }

    g_api_initialized.store(true);
    return true;
}

bool ResolveMethod(const il2cpputils::TargetSpec& spec,
                   il2cpputils::ResolvedMethod& out,
                   std::string* out_reason = nullptr) {
    if (!EnsureIl2cppApi()) {
        if (out_reason != nullptr) {
            *out_reason = "il2cpp api unavailable";
        }
        return false;
    }

    if (!il2cpputils::EnsureVmReadyAndAttach(g_api, 50, std::chrono::milliseconds(100))) {
        if (out_reason != nullptr) {
            *out_reason = "vm not ready";
        }
        return false;
    }

    std::string reason;
    if (!il2cpputils::FindMethodInAssemblies(g_api, spec, out, &reason)) {
        if (out_reason != nullptr) {
            *out_reason = reason;
        }
        LOGE("解析方法失败：%s (%s)", spec.full.c_str(), reason.c_str());
        return false;
    }
    return true;
}

bool IsInvalidManagedText(const std::string& text) {
    if (text == "<null>" || text.rfind("<length=", 0) == 0 || text.empty()) {
        return true;
    }
    return false;
}

void NormalizeUnityLookupText(std::string* text) {
    if (text == nullptr || text->empty()) {
        return;
    }

    constexpr const char* kNbspUtf8 = "\xC2\xA0";
    size_t offset = 0;
    while ((offset = text->find(kNbspUtf8, offset)) != std::string::npos) {
        text->replace(offset, 2, " ");
        ++offset;
    }
}

std::string ExtractManagedText(textutils::Il2CppString* value) {
    if (value == nullptr) {
        return {};
    }
    std::string original = textutils::DescribeIl2CppString(value);
    NormalizeUnityLookupText(&original);
    return original;
}

void RecordCapturedText(const HookedMethod& method, const std::string& original) {
    if (IsInvalidManagedText(original)) {
        return;
    }

    const bool filtered = textutils::ShouldFilter(original);
    if (filtered) {
        LOGI("[Setter] %s 过滤：#%s#", method.full.c_str(), original.c_str());
        return;
    }

    LOGI("[Setter] %s %s", method.full.c_str(), original.c_str());
    textdb::InsertIfNeeded(original);
}

bool TryApplyTranslation(const HookedMethod& method,
                         const std::string& original,
                         textutils::Il2CppString** value) {
    if (value == nullptr || *value == nullptr || IsInvalidManagedText(original)) {
        return false;
    }
    if (textutils::ShouldFilter(original)) {
        return false;
    }
    if (g_api.string_new == nullptr) {
        return false;
    }

    unity_translation_store::TranslationEntry translated;
    if (!unity_translation_store::LookupTranslation(original, &translated) ||
        translated.translation.empty()) {
        return false;
    }

    auto* replacement = reinterpret_cast<textutils::Il2CppString*>(
        g_api.string_new(translated.translation.c_str()));
    if (replacement == nullptr) {
        LOGE("构造翻译字符串失败：%s", method.full.c_str());
        return false;
    }

    LOGI("[Setter] %s 命中翻译：#%s# -> #%s#",
         method.full.c_str(),
         original.c_str(),
         translated.translation.c_str());
    *value = replacement;
    return true;
}

bool GetForcedReplacement(std::string* out) {
    if (out == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> _lk(g_text_replacement_mutex);
    if (!g_text_replacement_enabled || g_text_replacement_value.empty()) {
        return false;
    }
    *out = g_text_replacement_value;
    return true;
}

bool TryApplyForcedReplacement(const HookedMethod& method,
                               const std::string& original,
                               textutils::Il2CppString** value) {
    if (value == nullptr || *value == nullptr || IsInvalidManagedText(original)) {
        return false;
    }
    if (g_api.string_new == nullptr) {
        return false;
    }
    if (textutils::ShouldFilter(original)) {
        return false;
    }
    std::string replacement_text;
    if (!GetForcedReplacement(&replacement_text)) {
        return false;
    }
    auto* replacement = reinterpret_cast<textutils::Il2CppString*>(
        g_api.string_new(replacement_text.c_str()));
    if (replacement == nullptr) {
        LOGE("构造测试替换字符串失败：%s", method.full.c_str());
        return false;
    }
    LOGI("[Setter] %s 测试替换：#%s# -> #%s#",
         method.full.c_str(),
         original.c_str(),
         replacement_text.c_str());
    *value = replacement;
    return true;
}

template <size_t Slot>
void SetterReplacement(void* instance, textutils::Il2CppString* value) {
    HookedMethod& method = g_hook_slots[Slot];
    if (method.active) {
        std::string original = ExtractManagedText(value);
        if (!TryApplyForcedReplacement(method, original, &value)) {
            TryApplyTranslation(method, original, &value);
        }
        RecordCapturedText(method, original);
    }

    SetterFn original = method.original;
    if (original != nullptr) {
        original(instance, value);
    }
}

template <size_t... Slots>
constexpr std::array<SetterFn, sizeof...(Slots)> MakeSetterReplacements(std::index_sequence<Slots...>) {
    return { &SetterReplacement<Slots>... };
}

const auto kSetterReplacements = MakeSetterReplacements(std::make_index_sequence<kMaxHookSlots>{});

void ResetHookSlotsLocked() {
    for (auto& slot : g_hook_slots) {
        slot.active = false;
        slot.original = nullptr;
        slot.entry = 0;
        slot.full.clear();
        slot.backend = hook_backend::Backend::kAnd64InlineHook;
    }
}

int FindFreeHookSlotLocked() {
    for (size_t i = 0; i < g_hook_slots.size(); ++i) {
        if (!g_hook_slots[i].active && g_hook_slots[i].original == nullptr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool clear_hooks_locked() {
    for (const auto& slot : g_hook_slots) {
        if (slot.active && !hook_backend::SupportsDestroy(slot.backend)) {
            LOGW("检测到已安装的 %s hook 无法安全卸载，保留现有 hook，仅允许增量安装。若要完整切换请重启目标进程。",
                 hook_backend::BackendName(slot.backend));
            return false;
        }
    }

    for (const auto& slot : g_hook_slots) {
        if (!slot.active || slot.entry == 0) {
            continue;
        }
        if (!hook_backend::DestroyInlineHook(reinterpret_cast<void*>(slot.entry), slot.backend)) {
            LOGE("销毁 hook @%p 失败", reinterpret_cast<void*>(slot.entry));
        }
    }
    g_installed_targets.clear();
    ResetHookSlotsLocked();
    return true;
}

bool TryResolveMethodFromJson(const il2cpputils::TargetSpec& spec, uintptr_t* out_entry) {
    if (out_entry == nullptr) {
        return false;
    }

    std::uint64_t rva = 0;
    const uintptr_t base = g_il2cpp_base.load();
    if (base == 0 || !unity_translation_store::ResolveTargetAddress(spec.full, &rva) || rva == 0) {
        return false;
    }

    *out_entry = base + static_cast<uintptr_t>(rva);
    return true;
}

bool ResolveMethodForInstall(const il2cpputils::TargetSpec& spec,
                             uintptr_t* out_entry,
                             bool* out_verified_json,
                             il2cpputils::ResolvedMethod* out_reflection) {
    if (out_entry == nullptr) {
        return false;
    }

    uintptr_t json_entry = 0;
    const bool has_json = TryResolveMethodFromJson(spec, &json_entry);

    il2cpputils::ResolvedMethod reflection{};
    std::string reflection_reason;
    const bool has_reflection = ResolveMethod(spec, reflection, &reflection_reason);
    if (out_reflection != nullptr && has_reflection) {
        *out_reflection = reflection;
    }

    if (out_verified_json != nullptr) {
        *out_verified_json = false;
    }

    if (has_json && has_reflection) {
        if (json_entry == reflection.entry) {
            *out_entry = json_entry;
            if (out_verified_json != nullptr) {
                *out_verified_json = true;
            }
            return true;
        }

        LOGW("JSON 目标与运行时反射不一致：%s json=0x%" PRIxPTR " reflection=0x%" PRIxPTR "，回退到反射入口",
             spec.full.c_str(),
             json_entry,
             reflection.entry);
        *out_entry = reflection.entry;
        return true;
    }

    if (has_reflection) {
        *out_entry = reflection.entry;
        return true;
    }

    if (has_json) {
        LOGW("拒绝仅凭 JSON 安装 hook：%s json=0x%" PRIxPTR "，运行时反射校验失败（%s）",
             spec.full.c_str(),
             json_entry,
             reflection_reason.empty() ? "unknown" : reflection_reason.c_str());
    }
    return false;
}

void install_hooks_locked() {
#if !defined(__arm__) && !defined(__aarch64__) && !defined(__x86_64__) && !defined(__i386__)
    LOGE("当前架构不支持文本拦截");
    return;
#endif

    if (!g_il2cpp_ready.load()) {
        LOGE("il2cpp 未准备好，跳过安装 hook");
        return;
    }

    if (g_targets.empty()) {
        LOGI("未配置方法，跳过安装 hook");
        return;
    }

    if (!EnsureIl2cppApi()) {
        return;
    }

    const auto backend = hook_backend::GetPreferredBackend();
    const bool cleared_all = clear_hooks_locked();
    std::unordered_map<uintptr_t, std::string> seen_entries;
    if (!cleared_all) {
        for (const auto& slot : g_hook_slots) {
            if (slot.active && slot.entry != 0) {
                seen_entries.emplace(slot.entry, slot.full);
            }
        }
    }
    size_t success_count = 0;
    size_t skipped_count = 0;
    size_t duplicate_count = 0;

    if (backend == hook_backend::Backend::kDobby && !g_dobby_mode_configured.exchange(true)) {
        dobby_enable_near_branch_trampoline();
    }

    for (const auto& spec : g_targets) {
        uintptr_t resolved_entry = 0;
        il2cpputils::ResolvedMethod resolved{};
        bool resolved_from_verified_json = false;
        if (!ResolveMethodForInstall(spec, &resolved_entry, &resolved_from_verified_json, &resolved)) {
            ++skipped_count;
            continue;
        }

        if (!hookutils::IsExecutableAddressInModule(resolved_entry, kLibIl2cpp)) {
            LOGE("跳过 hook：%s -> 0x%" PRIxPTR " 不在 %s 可执行段内",
                 spec.full.c_str(), resolved_entry, kLibIl2cpp);
            ++skipped_count;
            continue;
        }

        auto existing = seen_entries.find(resolved_entry);
        if (existing != seen_entries.end()) {
            LOGI("跳过重复入口：%s 与 %s 共用 0x%" PRIxPTR,
                 spec.full.c_str(), existing->second.c_str(), resolved_entry);
            ++duplicate_count;
            continue;
        }

        const int slot = FindFreeHookSlotLocked();
        if (slot < 0 || static_cast<size_t>(slot) >= kSetterReplacements.size()) {
            LOGE("跳过 hook：%s 没有可用 hook 槽位（上限=%zu）", spec.full.c_str(), kMaxHookSlots);
            ++skipped_count;
            continue;
        }

        void* target = reinterpret_cast<void*>(resolved_entry);
        HookedMethod& hook_slot = g_hook_slots[static_cast<size_t>(slot)];
        hook_slot.full = spec.full;
        hook_slot.entry = resolved_entry;
        hook_slot.original = nullptr;
        hook_slot.active = false;
        hook_slot.backend = backend;

        if (hook_backend::InstallInlineHook(
                target,
                reinterpret_cast<void*>(kSetterReplacements[static_cast<size_t>(slot)]),
                reinterpret_cast<void**>(&hook_slot.original),
                spec.full.c_str(),
                backend)) {
            g_installed_targets.push_back(target);
            hook_slot.active = true;
            seen_entries.emplace(resolved_entry, spec.full);
            ++success_count;
            if (resolved_from_verified_json) {
                LOGI("Hook 成功: %s @ %p (slot=%d, source=json_rva, backend=%s)",
                     spec.full.c_str(),
                     target,
                     slot,
                     hook_backend::BackendName(backend));
            } else {
                LOGI("Hook 成功: %s @ %p (slot=%d, source=reflection, backend=%s, instance=%d, params=%u, argType=%d, retType=%d)",
                 spec.full.c_str(),
                 target,
                 slot,
                 hook_backend::BackendName(backend),
                 resolved.is_instance ? 1 : 0,
                 resolved.param_count,
                 resolved.first_param_type,
                 resolved.return_type);
            }
        } else {
            LOGE("Hook 失败 %s, backend=%s", spec.full.c_str(), hook_backend::BackendName(backend));
            hook_slot = HookedMethod{};
            ++skipped_count;
        }
    }
    LOGI("文本 hook 安装完成：backend=%s 成功 %zu，跳过 %zu，重复 %zu",
         hook_backend::BackendName(backend),
         success_count,
         skipped_count,
         duplicate_count);
}

void update_targets_internal(const std::vector<std::string>& new_targets) {
    std::lock_guard<std::mutex> _lk(g_hook_mutex);
    g_targets.clear();
    for (const auto& full : new_targets) {
        il2cpputils::TargetSpec spec;
        if (il2cpputils::ParseTarget(full, spec)) {
            g_targets.push_back(std::move(spec));
        } else {
            LOGE("非法方法名：%s", full.c_str());
        }
    }
    LOGI("更新方法列表，共 %zu 个", g_targets.size());

    if (g_il2cpp_ready.load()) {
        install_hooks_locked();
    }
}

void init_worker() {
    textdb::Init(g_process_name, true, textdb::ResetOnMainProcess());

    const uintptr_t base = hookutils::WaitForModule(kLibIl2cpp, std::chrono::seconds(10));
    if (base == 0) {
        LOGI("[%s] 等待 %s 载入超时，当前进程可能未使用 Unity/il2cpp", g_process_name.c_str(), kLibIl2cpp);
        return;
    }

    LOGI("[%s] %s loaded @ 0x%" PRIxPTR, g_process_name.c_str(), kLibIl2cpp, base);
    g_il2cpp_base.store(base);
    if (!EnsureIl2cppApi()) {
        return;
    }

    g_il2cpp_ready.store(true);
    std::lock_guard<std::mutex> _lk(g_hook_mutex);
    install_hooks_locked();
}

}  // namespace

void Init(const std::string& process_name) {
    g_process_name = process_name.empty() ? "unknown" : process_name;
    if (g_worker_started.exchange(true)) {
        return;
    }
    LOGI("[%s] Text extractor init", g_process_name.c_str());
    std::thread(init_worker).detach();
}

void SetHookBackend(std::int32_t backend_value) {
    const auto backend = hook_backend::BackendFromOrdinal(backend_value);
    {
        std::lock_guard<std::mutex> _lk(g_hook_mutex);
        if (!g_installed_targets.empty() && hook_backend::GetPreferredBackend() != backend) {
            LOGW("检测到运行期切换 backend 到 %s。已安装 hook 不会被热迁移，完整生效需要重启目标进程。",
                 hook_backend::BackendName(backend));
        }
        hook_backend::SetPreferredBackend(backend);
    }
}

void SetTextReplacement(bool enabled, const std::string& text) {
    std::lock_guard<std::mutex> _lk(g_text_replacement_mutex);
    g_text_replacement_enabled = enabled;
    g_text_replacement_value = text;
    LOGI("Unity text replacement enabled=%d length=%zu", enabled ? 1 : 0, text.size());
}

void UpdateTargetsJson(const std::string& json) {
    unity_translation_store::UpdateConfigJson(json);
    LOGI("更新 targets_json，length=%zu", json.size());
    if (g_il2cpp_ready.load()) {
        std::lock_guard<std::mutex> _lk(g_hook_mutex);
        install_hooks_locked();
    }
}

void UpdateTargets(const std::vector<std::string>& new_targets) {
    update_targets_internal(new_targets);
}

}  // namespace text_extractor
