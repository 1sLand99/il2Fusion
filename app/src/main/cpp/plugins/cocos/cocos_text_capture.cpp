#include "cocos_text_capture.h"

#include "cocos_font_replacement.h"
#include "cocos_log.h"
#include "cocos_runtime_state.h"
#include "cocos_text_burst_delay_guard.h"
#include "cocos_text_decoder.h"
#include "cocos_text_symbols.h"

#include "../../utils/db.h"
#include "../../utils/elf_symbols.h"
#include "../../utils/hook_backend.h"
#include "../../utils/text_utils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace cocos_runtime::internal {
namespace {

using TextSymbolFn = void* (*)(void*, void*, void*, void*, void*, void*, void*, void*);
using NativeFontSetterFn = void (*)(void*, void*);
constexpr int kLuaTypeNumber = 3;
constexpr size_t kMinInlineHookSymbolBytes = 16;
constexpr bool kEnableFairyguiNativeLabelSetStringHook = true;

struct NativeFontSetterTarget {
    uintptr_t entry = 0;
    std::string label;
    std::string owner_path;
    std::string method_name;
    TextArgumentLayout layout = TextArgumentLayout::kAutoStdString;
    FontResourceKind kind = FontResourceKind::kNone;
    NativeFontSetterFn function = nullptr;
};

struct TextHookSlot {
    std::atomic_bool active{false};
    uintptr_t entry = 0;
    TextSymbolSpec spec;
    TextSymbolFn original = nullptr;
    NativeFontSetterTarget sibling_font_setter;
    hook_backend::Backend backend = hook_backend::Backend::kAnd64InlineHook;
};

struct LuaBindingHookSlot {
    std::atomic_bool active{false};
    uintptr_t entry = 0;
    std::string label;
    std::string symbol_name;
    LuaBindingApi api{};
    LuaCFunction original = nullptr;
    LuaCFunction sibling_font_binding = nullptr;
    std::string sibling_font_label;
    bool font_replacement_enabled = false;
    LuaFontReplacementState font_replacement;
    hook_backend::Backend backend = hook_backend::Backend::kAnd64InlineHook;
};

std::array<TextHookSlot, kMaxLabelHookSlots> g_text_slots{};
std::array<LuaBindingHookSlot, kMaxLuaBindingHookSlots> g_lua_binding_slots{};

bool IsReplacementEchoText(const RuntimeConfig& config, const std::string& text) {
    if (!config.text_replacement_enabled || config.text_replacement_value.empty()) {
        return false;
    }
    const std::string& replacement = config.text_replacement_value;
    if (text == replacement) {
        return true;
    }
    return text.size() < replacement.size() &&
           !text.empty() &&
           replacement.find(text) != std::string::npos;
}

void RecordCocosText(const std::string& label, const std::string& text) {
    if (!textutils::LooksLikeText(text) || textutils::ShouldFilter(text)) {
        return;
    }
    const RuntimeConfig config = SnapshotConfig();
    if (IsReplacementEchoText(config, text)) {
        return;
    }
    COCOS_EVENT_LOGI(cocos_log::kText, "%s %s", label.c_str(), text.c_str());
    if (!config.text_persistence_enabled) {
        return;
    }
    if (config.text_persistence_chinese_only && !textutils::ContainsChinese(text)) {
        return;
    }
    textdb::InsertIfNeeded(text);
}

bool GetTextReplacementValue(std::string* out) {
    if (out == nullptr) {
        return false;
    }
    RuntimeState& state = State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    if (!state.config.text_replacement_enabled || state.config.text_replacement_value.empty()) {
        return false;
    }
    *out = state.config.text_replacement_value;
    return true;
}

TextBurstDelayDecision SleepBeforeTextReplacement(const RuntimeConfig& config,
                                                  uintptr_t hook_entry,
                                                  int text_arg_index,
                                                  const std::string& source_text) {
    const TextBurstDelayDecision decision = ResolveTextBurstDelay(
        config,
        hook_entry,
        text_arg_index,
        source_text);
    if (!decision.should_sleep) {
        return decision;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(config.text_replacement_delay_ms));
    RecordTextBurstInjectedDelay(config);
    return decision;
}

bool BuildReplacementArgument(TextArgumentLayout layout,
                              const std::string& text,
                              TextArgumentReplacement* replacement,
                              void** out_arg) {
    if (replacement == nullptr || out_arg == nullptr) {
        return false;
    }
    if (!replacement->Build(layout, text) || replacement->argument() == nullptr) {
        return false;
    }
    *out_arg = const_cast<void*>(replacement->argument());
    return true;
}

bool IsValidHookArgIndex(int index) {
    return index >= 0 && index < kMaxTextHookArgs;
}

bool IsTextRecordCategory(TextSymbolCategory category) {
    return category == TextSymbolCategory::kSetter ||
           category == TextSymbolCategory::kCreation;
}

bool DecodeKnownHookArgument(TextArgumentLayout layout,
                             void* arg,
                             std::string* out,
                             TextArgumentLayout* decoded_layout) {
    if (layout == TextArgumentLayout::kLibcxxNdk &&
        DecodeKnownLibcxxNdkTextArgument(arg, out)) {
        if (decoded_layout != nullptr) {
            *decoded_layout = TextArgumentLayout::kLibcxxNdk;
        }
        return true;
    }
    return DecodeTextArgumentWithLayout(arg, layout, out, decoded_layout);
}

bool DecodeHookTextArgument(const TextSymbolSpec& spec,
                            void* arg,
                            std::string* out,
                            TextArgumentLayout* decoded_layout) {
    return DecodeKnownHookArgument(spec.layout, arg, out, decoded_layout);
}

bool DecodeHookFontArgument(const TextSymbolSpec& spec,
                            void* arg,
                            std::string* out,
                            TextArgumentLayout* decoded_layout) {
    return DecodeKnownHookArgument(spec.font_layout, arg, out, decoded_layout);
}

bool IsSymbolLongEnoughForInlineHook(const elf_symbols::SymbolInfo& symbol, const char* label) {
    if (symbol.size == 0 || symbol.size >= kMinInlineHookSymbolBytes) {
        return true;
    }
    COCOS_LOGW("skip short hook symbol: %s size=%zu min=%zu",
               label != nullptr ? label : symbol.name.c_str(),
               symbol.size,
               kMinInlineHookSymbolBytes);
    return false;
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsLikelyBmFontPath(const std::string& value) {
    const std::string lower = ToLowerAscii(value);
    return lower.find(".fnt") != std::string::npos;
}

bool ShouldSkipBmFontReplacement(const std::string& value) {
    const std::string basename = ToLowerAscii(textutils::Basename(value));
    return basename.find("fntnum") != std::string::npos ||
           basename.find("number") != std::string::npos ||
           basename.find("digit") != std::string::npos;
}

bool ShouldReplaceNativeFontPath(FontResourceKind kind,
                                 const RuntimeConfig& config,
                                 const std::string& original_font) {
    if (kind == FontResourceKind::kTtf) {
        return true;
    }
    if (kind != FontResourceKind::kFnt) {
        return false;
    }
    if (!config.text_replacement_enabled || config.text_replacement_value.empty()) {
        return false;
    }
    if (!IsLikelyBmFontPath(original_font)) {
        return false;
    }
    return !ShouldSkipBmFontReplacement(original_font);
}

bool TryReplaceNativeFontArgument(const TextSymbolSpec& spec,
                                  const RuntimeConfig& config,
                                  void** args,
                                  TextArgumentReplacement* font_replacement) {
    if (spec.family != TextSymbolFamily::kCocos ||
        spec.font_kind == FontResourceKind::kNone ||
        !IsValidHookArgIndex(spec.font_arg_index) ||
        font_replacement == nullptr) {
        return false;
    }

    const std::string* font_path = GetReplacementFontString(spec.font_kind, config);
    std::string original_font;
    TextArgumentLayout decoded_font_layout = spec.font_layout;
    if (font_path == nullptr ||
        !DecodeHookFontArgument(
            spec,
            args[spec.font_arg_index],
            &original_font,
            &decoded_font_layout) ||
        original_font == *font_path ||
        !ShouldReplaceNativeFontPath(spec.font_kind, config, original_font)) {
        return false;
    }

    const bool built_font_replacement = BuildReplacementArgument(
        decoded_font_layout,
        *font_path,
        font_replacement,
        &args[spec.font_arg_index]);
    if (!built_font_replacement) {
        COCOS_EVENT_LOGI(cocos_log::kNativeFontReplace,
                         "%s skip font replacement unsupported arg=%d layout=%s decoded=%s",
                         spec.label.c_str(),
                         spec.font_arg_index,
                         TextArgumentLayoutName(spec.font_layout),
                         TextArgumentLayoutName(decoded_font_layout));
        return false;
    }

    COCOS_EVENT_LOGI(cocos_log::kNativeFontReplace,
                     "%s kind=%s arg=%d layout=%s decoded=%s #%s# -> #%s#",
                     spec.label.c_str(),
                     FontResourceKindName(spec.font_kind),
                     spec.font_arg_index,
                     TextArgumentLayoutName(spec.font_layout),
                     TextArgumentLayoutName(decoded_font_layout),
                     textutils::PreviewBytes(original_font.c_str(), original_font.size()).c_str(),
                     font_path->c_str());
    return true;
}

bool TryApplyNativeSiblingFontSetter(const TextHookSlot& slot,
                                     void* target,
                                     const RuntimeConfig& config) {
    const NativeFontSetterTarget& setter = slot.sibling_font_setter;
    if (target == nullptr ||
        setter.function == nullptr ||
        setter.kind == FontResourceKind::kNone ||
        !config.font_replacement.enabled) {
        return false;
    }

    const std::string* font_path = GetReplacementFontString(setter.kind, config);
    if (font_path == nullptr) {
        return false;
    }

    TextArgumentReplacement font_replacement;
    void* font_arg = nullptr;
    if (!BuildReplacementArgument(setter.layout, *font_path, &font_replacement, &font_arg)) {
        COCOS_EVENT_LOGI(cocos_log::kNativeFontReplace,
                         "%s sibling=%s skip font replacement unsupported layout=%s",
                         slot.spec.label.c_str(),
                         setter.label.c_str(),
                         TextArgumentLayoutName(setter.layout));
        return false;
    }

    setter.function(target, font_arg);
    COCOS_EVENT_LOGI(cocos_log::kNativeFontReplace,
                     "%s sibling=%s kind=%s path=%s",
                     slot.spec.label.c_str(),
                     setter.label.c_str(),
                     FontResourceKindName(setter.kind),
                     font_path->c_str());
    return true;
}

bool IsNativeLabelSetStringSymbol(const TextSymbolSpec& spec);

template <size_t Slot>
void* TextSymbolReplacement(void* a0,
                            void* a1,
                            void* a2,
                            void* a3,
                            void* a4,
                            void* a5,
                            void* a6,
                            void* a7) {
    TextHookSlot& slot = g_text_slots[Slot];
    void* args[kMaxTextHookArgs] = {a0, a1, a2, a3, a4, a5, a6, a7};
    TextArgumentReplacement text_replacement;
    TextArgumentReplacement font_replacement;
    bool replaced_text = false;
    bool apply_native_sibling_font = false;
    bool has_config = false;
    RuntimeConfig config;

    if (slot.active.load(std::memory_order_acquire)) {
        const TextSymbolSpec& spec = slot.spec;
        config = SnapshotConfig();
        has_config = true;
        std::string decoded_text;
        TextArgumentLayout decoded_text_layout = spec.layout;
        const bool has_text_arg = IsValidHookArgIndex(spec.arg_index) &&
            DecodeHookTextArgument(
                spec,
                args[spec.arg_index],
                &decoded_text,
                &decoded_text_layout);

        if (has_text_arg &&
            config.runtime_text_capture &&
            IsTextRecordCategory(spec.category)) {
            RecordCocosText(spec.label, decoded_text);
        } else if (!has_text_arg && IsTextRecordCategory(spec.category)) {
            COCOS_EVENT_LOGI(cocos_log::kText,
                             "%s decode skipped category=%s layout=%s arg_index=%d arg=%p",
                             spec.label.c_str(),
                             TextSymbolCategoryName(spec.category),
                             TextArgumentLayoutName(spec.layout),
                             spec.arg_index,
                             IsValidHookArgIndex(spec.arg_index) ? args[spec.arg_index] : nullptr);
        }

        std::string replacement_text;
        const bool should_replace_text = spec.replacement_supported &&
            IsTextRecordCategory(spec.category) &&
            has_text_arg &&
            textutils::LooksLikeText(decoded_text) &&
            !textutils::ShouldFilter(decoded_text) &&
            GetTextReplacementValue(&replacement_text) &&
            !IsReplacementEchoText(config, decoded_text) &&
            decoded_text != replacement_text;
        if (should_replace_text) {
            const bool built_text_replacement = BuildReplacementArgument(
                decoded_text_layout,
                replacement_text,
                &text_replacement,
                &args[spec.arg_index]);
            if (!built_text_replacement) {
                COCOS_EVENT_LOGI(cocos_log::kTextReplace,
                                 "%s skip replacement unsupported layout=%s decoded=%s",
                                 spec.label.c_str(),
                                 TextArgumentLayoutName(spec.layout),
                                 TextArgumentLayoutName(decoded_text_layout));
            } else {
                const TextBurstDelayDecision delay = SleepBeforeTextReplacement(
                    config,
                    slot.entry,
                    spec.arg_index,
                    decoded_text);
                replaced_text = true;
                COCOS_EVENT_LOGI(cocos_log::kTextReplace,
                                 "%s family=%s category=%s layout=%s decoded=%s delay=%s reason=%s #%s# -> #%s#",
                                 spec.label.c_str(),
                                 TextSymbolFamilyName(spec.family),
                                 TextSymbolCategoryName(spec.category),
                                 TextArgumentLayoutName(spec.layout),
                                 TextArgumentLayoutName(decoded_text_layout),
                                 delay.ModeName(),
                                 delay.ReasonName(),
                                 textutils::PreviewBytes(decoded_text.c_str(), decoded_text.size()).c_str(),
                                 replacement_text.c_str());

                if (spec.family == TextSymbolFamily::kCocos &&
                    spec.font_kind != FontResourceKind::kNone &&
                    IsValidHookArgIndex(spec.font_arg_index) &&
                    spec.font_arg_index != spec.arg_index) {
                    TryReplaceNativeFontArgument(spec, config, args, &font_replacement);
                }
                apply_native_sibling_font =
                    slot.sibling_font_setter.function != nullptr;
            }
        }

        if (!replaced_text && spec.font_kind == FontResourceKind::kFnt) {
            TryReplaceNativeFontArgument(spec, config, args, &font_replacement);
        }
    }

    TextSymbolFn original = slot.original;
    if (original != nullptr) {
        void* result = original(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        if (apply_native_sibling_font && has_config) {
            TryApplyNativeSiblingFontSetter(slot, args[0], config);
        }
        return result;
    }
    return nullptr;
}

template <size_t... Slots>
constexpr std::array<TextSymbolFn, sizeof...(Slots)> MakeTextReplacements(std::index_sequence<Slots...>) {
    return { &TextSymbolReplacement<Slots>... };
}

const auto kTextReplacements = MakeTextReplacements(std::make_index_sequence<kMaxLabelHookSlots>{});

struct LuaTextBindingRule {
    const char* marker;
    std::array<int, 3> indexes;
    size_t index_count;
    std::array<int, 2> font_indexes;
    size_t font_index_count;
    FontResourceKind font_kind;
    const char* sibling_font_marker;
    FontResourceKind sibling_font_kind;
    int sibling_integer_index;
    int rich_text_defaults_index = 0;
    bool apply_sibling_font_before_original = false;
    bool apply_sibling_font_after_original = true;
    bool replace_font_without_text = false;
};

constexpr LuaTextBindingRule MakeLuaTextBindingRule(const char* marker,
                                                     std::array<int, 3> indexes,
                                                     size_t index_count,
                                                     std::array<int, 2> font_indexes,
                                                     size_t font_index_count,
                                                     FontResourceKind font_kind,
                                                     const char* sibling_font_marker = nullptr,
                                                     FontResourceKind sibling_font_kind = FontResourceKind::kNone,
                                                     int sibling_integer_index = 0,
                                                     int rich_text_defaults_index = 0,
                                                     bool apply_sibling_font_before_original = false,
                                                     bool apply_sibling_font_after_original = true,
                                                     bool replace_font_without_text = false) {
    return {
        marker,
        indexes,
        index_count,
        font_indexes,
        font_index_count,
        font_kind,
        sibling_font_marker,
        sibling_font_kind,
        sibling_integer_index,
        rich_text_defaults_index,
        apply_sibling_font_before_original,
        apply_sibling_font_after_original,
        replace_font_without_text,
    };
}

constexpr LuaTextBindingRule TextAndFont(const char* marker,
                                         int text_index,
                                         int font_index,
                                         FontResourceKind font_kind) {
    return MakeLuaTextBindingRule(
        marker, {text_index, 0, 0}, 1, {font_index, 0}, 1, font_kind);
}

constexpr LuaTextBindingRule TextAt2WithSiblingFont(const char* marker,
                                                    const char* sibling_font_marker,
                                                    FontResourceKind sibling_font_kind,
                                                    int sibling_integer_index = 0,
                                                    bool apply_before_original = false,
                                                    bool apply_after_original = true) {
    return MakeLuaTextBindingRule(
        marker,
        {2, 0, 0},
        1,
        {0, 0},
        0,
        FontResourceKind::kNone,
        sibling_font_marker,
        sibling_font_kind,
        sibling_integer_index,
        0,
        apply_before_original,
        apply_after_original);
}

constexpr LuaTextBindingRule FontOnly(const char* marker,
                                      int font_index,
                                      FontResourceKind font_kind) {
    return MakeLuaTextBindingRule(
        marker,
        {0, 0, 0},
        0,
        {font_index, 0},
        1,
        font_kind,
        nullptr,
        FontResourceKind::kNone,
        0,
        0,
        false,
        false,
        true);
}

constexpr LuaTextBindingRule RichXml(const char* marker,
                                     int defaults_index,
                                     bool apply_font_before_original) {
    return MakeLuaTextBindingRule(
        marker,
        {2, 0, 0},
        1,
        {0, 0},
        0,
        FontResourceKind::kNone,
        "_ui_RichText_setFontFaceP9lua_State",
        FontResourceKind::kTtf,
        0,
        defaults_index,
        apply_font_before_original,
        false);
}

constexpr LuaTextBindingRule kLuaTextBindingRules[] = {
    TextAndFont("_Label_createWithTTFP9lua_State", 2, 3, FontResourceKind::kTtf),
    TextAndFont("_Label_initWithTTFP9lua_State", 2, 3, FontResourceKind::kTtf),
    TextAndFont("_Label_createWithBMFontP9lua_State", 3, 2, FontResourceKind::kFnt),
    TextAndFont("_Label_initWithBMFontP9lua_State", 3, 2, FontResourceKind::kFnt),
    TextAt2WithSiblingFont("_Label_setStringP9lua_State", "_Label_setSystemFontNameP9lua_State", FontResourceKind::kTtf),
    TextAndFont("_createWithSystemFontP9lua_State", 2, 3, FontResourceKind::kTtf),
    TextAndFont("_ui_Text_createP9lua_State", 2, 3, FontResourceKind::kTtf),
    TextAt2WithSiblingFont("_ui_Text_setStringP9lua_State", "_ui_Text_setFontNameP9lua_State", FontResourceKind::kTtf),
    TextAndFont("_ui_TextBMFont_createP9lua_State", 2, 3, FontResourceKind::kFnt),
    TextAt2WithSiblingFont("_ui_TextBMFont_setStringP9lua_State", "_ui_TextBMFont_setFntFileP9lua_State", FontResourceKind::kFnt),
    FontOnly("_ui_TextBMFont_setFntFileP9lua_State", 2, FontResourceKind::kFnt),
    FontOnly("_LabelBMFont_setFntFileP9lua_State", 2, FontResourceKind::kFnt),
    TextAndFont("_ui_TextField_createP9lua_State", 2, 3, FontResourceKind::kTtf),
    TextAt2WithSiblingFont("_ui_TextField_setStringP9lua_State", "_ui_TextField_setFontNameP9lua_State", FontResourceKind::kTtf),
    TextAt2WithSiblingFont("_ui_TextField_setPlaceHolderP9lua_State", "_ui_TextField_setFontNameP9lua_State", FontResourceKind::kTtf),
    TextAt2WithSiblingFont("_ui_Button_setTitleTextP9lua_State", "_ui_Button_setTitleFontNameP9lua_State", FontResourceKind::kTtf),
    TextAndFont("_ui_RichElementText_createP9lua_State", 5, 6, FontResourceKind::kTtf),
    TextAndFont("_ui_RichElementText_initP9lua_State", 5, 6, FontResourceKind::kTtf),
    RichXml("_ui_RichText_createWithXMLP9lua_State", 3, false),
    RichXml("_ui_RichText_initWithXMLP9lua_State", 3, true),
    TextAt2WithSiblingFont("_ui_RichText_setStringP9lua_State", "_ui_RichText_setFontFaceP9lua_State", FontResourceKind::kTtf, 0, true, false),
    FontOnly("_ui_RichText_setDefaultFontNameP9lua_State", 2, FontResourceKind::kTtf),
    FontOnly("_ui_RichText_setCalcFontNameP9lua_State", 2, FontResourceKind::kTtf),
    TextAt2WithSiblingFont("_ui_TabHeader_setTitleTextP9lua_State", "_ui_TabHeader_setTitleFontNameP9lua_State", FontResourceKind::kTtf),
    TextAndFont("_extension_ControlButton_initWithTitleAndFontNameAndFontSizeP9lua_State", 2, 3, FontResourceKind::kTtf),
    TextAt2WithSiblingFont("_extension_ControlButton_setTitleForStateP9lua_State", "_extension_ControlButton_setTitleTTFForStateP9lua_State", FontResourceKind::kTtf, 3),
};

bool LuaTextBindingRuleMatches(const LuaTextBindingRule& rule, const std::string& name) {
    return name.find(rule.marker) != std::string::npos;
}

const LuaTextBindingRule* FindLuaTextBindingRule(const std::string& name) {
    for (const auto& rule : kLuaTextBindingRules) {
        if (LuaTextBindingRuleMatches(rule, name)) {
            return &rule;
        }
    }
    return nullptr;
}

template <size_t Slot>
int LuaBindingReplacement(void* lua_state) {
    LuaBindingHookSlot& slot = g_lua_binding_slots[Slot];
    const LuaTextBindingRule* rule = nullptr;
    RuntimeConfig config;
    bool has_config = false;
    bool replaced = false;
    bool has_sibling_integer_arg = false;
    bool sibling_integer_required = false;
    long long sibling_integer_arg = 0;
    std::string replaced_text;
    if (slot.active.load(std::memory_order_acquire) && lua_state != nullptr &&
        slot.api.gettop != nullptr && slot.api.type != nullptr && slot.api.tolstring != nullptr) {
        int top = slot.api.gettop(lua_state);
        if (top < 0) {
            top = 0;
        }
        config = SnapshotConfig();
        has_config = true;
        if (top > config.lua_binding_max_stack_args) {
            top = config.lua_binding_max_stack_args;
        }

        rule = FindLuaTextBindingRule(slot.symbol_name);
        int replaced_text_index = -1;
        if (rule != nullptr) {
            sibling_integer_required = rule->sibling_integer_index > 0;
            if (sibling_integer_required &&
                slot.api.tointeger != nullptr &&
                rule->sibling_integer_index <= top &&
                slot.api.type(lua_state, rule->sibling_integer_index) == kLuaTypeNumber) {
                sibling_integer_arg = slot.api.tointeger(lua_state, rule->sibling_integer_index);
                has_sibling_integer_arg = true;
            }
            for (size_t i = 0; i < rule->index_count; ++i) {
                const int text_index = rule->indexes[i];
                if (text_index < 1 || text_index > top ||
                    slot.api.type(lua_state, text_index) != kLuaTypeString) {
                    continue;
                }

                size_t len = 0;
                const char* data = slot.api.tolstring(lua_state, text_index, &len);
                if (data == nullptr || len == 0 || len > kMaxTextBytes) {
                    continue;
                }

                std::string text(data, len);
                if (!textutils::LooksLikeText(text) || textutils::ShouldFilter(text)) {
                    continue;
                }
                if (IsReplacementEchoText(config, text)) {
                    continue;
                }

                if (config.runtime_text_capture) {
                    COCOS_EVENT_LOGI(cocos_log::kLuaBinding,
                                     "%s stack#%d %s",
                                     slot.label.c_str(),
                                     text_index,
                                     textutils::PreviewBytes(text.c_str(), text.size()).c_str());
                    RecordCocosText(slot.label, text);
                }
                if (!replaced &&
                    config.text_replacement_enabled &&
                    !config.text_replacement_value.empty() &&
                    text != config.text_replacement_value &&
                    HasLuaBindingReplacementApi(slot.api)) {
                    std::string lua_replacement_text = config.text_replacement_value;
                    slot.api.pushlstring(
                        lua_state,
                        lua_replacement_text.data(),
                        lua_replacement_text.size());
                    slot.api.replace(lua_state, text_index);
                    replaced = true;
                    replaced_text_index = text_index;
                    replaced_text = lua_replacement_text;
                    const TextBurstDelayDecision delay = SleepBeforeTextReplacement(
                        config,
                        slot.entry,
                        text_index,
                        text);
                    COCOS_EVENT_LOGI(cocos_log::kLuaTextReplace,
                                     "%s stack#%d delay=%s reason=%s #%s# -> #%s#",
                                     slot.label.c_str(),
                                     text_index,
                                     delay.ModeName(),
                                     delay.ReasonName(),
                                     textutils::PreviewBytes(text.c_str(), text.size()).c_str(),
                                     lua_replacement_text.c_str());
                } else if (!replaced && config.text_replacement_enabled && !HasLuaBindingReplacementApi(slot.api)) {
                    COCOS_EVENT_LOGI(cocos_log::kLuaTextReplace,
                                     "%s skipped: lua_pushlstring/lua_replace missing",
                                     slot.label.c_str());
                }
            }
        }
        if (slot.font_replacement_enabled &&
            rule != nullptr &&
            rule->replace_font_without_text) {
            TryReplaceLuaFontBinding(
                slot.label,
                lua_state,
                top,
                -1,
                rule->font_indexes,
                rule->font_index_count,
                rule->font_kind,
                slot.api,
                config,
                &slot.font_replacement);
        }
        if (slot.font_replacement_enabled && replaced && rule != nullptr) {
            TryReplaceLuaFontBinding(
                slot.label,
                lua_state,
                top,
                replaced_text_index,
                rule->font_indexes,
                rule->font_index_count,
                rule->font_kind,
                slot.api,
                config,
                &slot.font_replacement);
            TrySetLuaRichTextDefaultFont(
                slot.label,
                lua_state,
                top,
                rule->rich_text_defaults_index,
                rule->sibling_font_kind,
                slot.api,
                config,
                &slot.font_replacement);
            if (rule->apply_sibling_font_before_original &&
                slot.sibling_font_binding != nullptr &&
                (!sibling_integer_required || has_sibling_integer_arg)) {
                TryApplyLuaSiblingFontBeforeTextBinding(
                    slot.label,
                    slot.sibling_font_label,
                    lua_state,
                    slot.sibling_font_binding,
                    replaced_text_index,
                    replaced_text,
                    rule->sibling_font_kind,
                    slot.api,
                    config,
                    &slot.font_replacement);
            }
        }
    }
    LuaCFunction original = slot.original;
    int original_result = 0;
    if (original != nullptr) {
        original_result = original(lua_state);
    }
    // Setter bindings often return 0 because they do not push Lua results.
    // Still apply the sibling font setter after the original text setter ran.
    if (slot.font_replacement_enabled &&
        replaced && has_config && rule != nullptr && rule->apply_sibling_font_after_original &&
        original != nullptr && slot.sibling_font_binding != nullptr &&
        (!sibling_integer_required || has_sibling_integer_arg)) {
        TryApplyLuaSiblingFontBinding(
            slot.label,
            slot.sibling_font_label,
            lua_state,
            slot.sibling_font_binding,
            has_sibling_integer_arg,
            sibling_integer_arg,
            rule->sibling_font_kind,
            slot.api,
            config,
            &slot.font_replacement);
    }
    return original_result;
}

template <size_t... Slots>
constexpr std::array<LuaCFunction, sizeof...(Slots)> MakeLuaBindingReplacements(std::index_sequence<Slots...>) {
    return { &LuaBindingReplacement<Slots>... };
}

const auto kLuaBindingReplacements = MakeLuaBindingReplacements(std::make_index_sequence<kMaxLuaBindingHookSlots>{});

int FindFreeTextSlotLocked() {
    for (size_t i = 0; i < g_text_slots.size(); ++i) {
        if (!g_text_slots[i].active.load(std::memory_order_relaxed) && g_text_slots[i].entry == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FindFreeLuaBindingSlotLocked() {
    for (size_t i = 0; i < g_lua_binding_slots.size(); ++i) {
        if (!g_lua_binding_slots[i].active.load(std::memory_order_relaxed) &&
            g_lua_binding_slots[i].entry == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool IsLuaTextBindingSymbol(const std::string& name) {
    if (FindLuaTextBindingRule(name) == nullptr) {
        return false;
    }
    return name.find("lua_cocos2dx") != std::string::npos;
}

bool HasFairyguiSymbols(const std::vector<elf_symbols::SymbolInfo>& symbols) {
    for (const auto& symbol : symbols) {
        if (symbol.name.find("fairygui") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasFontReplacementForKind(const RuntimeConfig& config, FontResourceKind kind) {
    if (!config.font_replacement.enabled) {
        return false;
    }
    if (kind == FontResourceKind::kTtf) {
        return !config.font_replacement.ttf_path.empty();
    }
    if (kind == FontResourceKind::kFnt) {
        return !config.font_replacement.bmfont_fnt_path.empty();
    }
    return false;
}

bool HasAnyFontReplacement(const RuntimeConfig& config) {
    return HasFontReplacementForKind(config, FontResourceKind::kTtf) ||
           HasFontReplacementForKind(config, FontResourceKind::kFnt);
}

uintptr_t FindLuaBindingAddressByMarker(const std::vector<elf_symbols::SymbolInfo>& symbols,
                                        const char* marker) {
    if (marker == nullptr || *marker == '\0') {
        return 0;
    }
    for (const auto& symbol : symbols) {
        if (symbol.address != 0 && symbol.name.find(marker) != std::string::npos) {
            return symbol.address;
        }
    }
    return 0;
}

std::vector<NativeFontSetterTarget> FindNativeFontSetters(
    const std::string& module_name,
    const std::vector<elf_symbols::SymbolInfo>& symbols,
    const RuntimeConfig& config) {
    std::vector<NativeFontSetterTarget> targets;
    if (!HasAnyFontReplacement(config)) {
        return targets;
    }
    for (const auto& symbol : symbols) {
        const auto spec = ResolveNativeFontSetterSpec(symbol.name);
        if (!spec.has_value() || symbol.address == 0) {
            continue;
        }
        if (!HasFontReplacementForKind(config, spec->font_kind)) {
            continue;
        }
        if (!hookutils::IsExecutableAddressInModule(symbol.address, module_name.c_str())) {
            continue;
        }
        targets.push_back(NativeFontSetterTarget{
            symbol.address,
            module_name + "!" + spec->label,
            spec->owner_path,
            spec->method_name,
            spec->layout,
            spec->font_kind,
            reinterpret_cast<NativeFontSetterFn>(symbol.address),
        });
    }
    return targets;
}

struct NativeSiblingFontRequest {
    const char* owner_path;
    const char* method_name;
    FontResourceKind kind;
};

struct NativeTextSetterPolicy {
    const char* text_owner_path;
    const char* text_method_name;
    const char* font_owner_path;
    const char* font_method_name;
    FontResourceKind font_kind;
    bool allow_alongside_lua_binding;
};

// Policy for native setters that are useful even when LuaBinding hooks are active.
constexpr NativeTextSetterPolicy kNativeTextSetterPolicies[] = {
    {"cocos2d::Label", "setString", "cocos2d::Label", "setSystemFontName", FontResourceKind::kTtf, true},
    {"cocos2d::Label", "setStringLazy", "cocos2d::Label", "setSystemFontName", FontResourceKind::kTtf, true},
    {"cocos2d::LabelTTF", "setString", "cocos2d::LabelTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::LabelTTF", "setText", "cocos2d::LabelTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::CCLabelTTF", "setString", "cocos2d::CCLabelTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::CCLabelTTF", "setText", "cocos2d::CCLabelTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::TextFieldTTF", "setString", "cocos2d::TextFieldTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::TextFieldTTF", "setText", "cocos2d::TextFieldTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::TextFieldTTF", "setPlaceHolder", "cocos2d::TextFieldTTF", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::Text", "setString", "cocos2d::ui::Text", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::Text", "setText", "cocos2d::ui::Text", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::TextField", "setString", "cocos2d::ui::TextField", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::TextField", "setText", "cocos2d::ui::TextField", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::TextField", "setPlaceHolder", "cocos2d::ui::TextField", "setFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::Button", "setTitleText", "cocos2d::ui::Button", "setTitleFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::TabHeader", "setTitleText", "cocos2d::ui::TabHeader", "setTitleFontName", FontResourceKind::kTtf, true},
    {"cocos2d::ui::RichText", "setString", "cocos2d::ui::RichText", "setFontFace", FontResourceKind::kTtf, true},
    {"cocos2d::LabelBMFont", "setString", "cocos2d::LabelBMFont", "setFntFile", FontResourceKind::kFnt, true},
    {"cocos2d::CCLabelBMFont", "setString", "cocos2d::CCLabelBMFont", "setFntFile", FontResourceKind::kFnt, true},
    {"cocos2d::ui::TextBMFont", "setString", "cocos2d::ui::TextBMFont", "setFntFile", FontResourceKind::kFnt, true},
    {"cocos2d::ui::LabelBMFont", "setString", "cocos2d::ui::LabelBMFont", "setFntFile", FontResourceKind::kFnt, true},
};

const NativeTextSetterPolicy* FindNativeTextSetterPolicy(const TextSymbolSpec& spec) {
    if (spec.family != TextSymbolFamily::kCocos ||
        spec.category != TextSymbolCategory::kSetter) {
        return nullptr;
    }
    for (const NativeTextSetterPolicy& policy : kNativeTextSetterPolicies) {
        if (spec.owner_path == policy.text_owner_path &&
            spec.method_name == policy.text_method_name) {
            return &policy;
        }
    }
    return nullptr;
}

std::optional<NativeSiblingFontRequest> ResolveNativeSiblingFontRequest(
    const TextSymbolSpec& spec) {
    const NativeTextSetterPolicy* policy = FindNativeTextSetterPolicy(spec);
    if (policy == nullptr) {
        return std::nullopt;
    }
    return NativeSiblingFontRequest{
        policy->font_owner_path,
        policy->font_method_name,
        policy->font_kind,
    };
}

std::optional<NativeFontSetterTarget> FindNativeSiblingFontSetter(
    const TextSymbolSpec& spec,
    const std::vector<NativeFontSetterTarget>& native_font_setters,
    const RuntimeConfig& config) {
    const auto request = ResolveNativeSiblingFontRequest(spec);
    if (!request.has_value() ||
        !HasFontReplacementForKind(config, request->kind)) {
        return std::nullopt;
    }
    for (const NativeFontSetterTarget& setter : native_font_setters) {
        if (setter.kind == request->kind &&
            setter.owner_path == request->owner_path &&
            setter.method_name == request->method_name &&
            setter.function != nullptr) {
            return setter;
        }
    }
    return std::nullopt;
}

bool ShouldInstallLuaBindingSymbol(const RuntimeConfig& config,
                                   const std::string& name,
                                   const LuaTextBindingRule& rule,
                                   bool lua_binding_text_hooks_enabled,
                                   bool lua_binding_font_hooks_enabled) {
    const bool need_text_binding =
        lua_binding_text_hooks_enabled &&
        (config.runtime_text_capture || config.text_replacement_enabled);
    const bool need_font_binding =
        lua_binding_font_hooks_enabled &&
        rule.replace_font_without_text &&
        HasFontReplacementForKind(config, rule.font_kind);
    return (need_text_binding || need_font_binding) && IsLuaTextBindingSymbol(name);
}

void ResetTextSlot(TextHookSlot& slot) {
    slot.active.store(false, std::memory_order_relaxed);
    slot.entry = 0;
    slot.spec = TextSymbolSpec{};
    slot.original = nullptr;
    slot.sibling_font_setter = NativeFontSetterTarget{};
    slot.backend = hook_backend::Backend::kAnd64InlineHook;
}

void ResetLuaBindingSlot(LuaBindingHookSlot& slot) {
    slot.active.store(false, std::memory_order_relaxed);
    slot.entry = 0;
    slot.label.clear();
    slot.symbol_name.clear();
    slot.api = LuaBindingApi{};
    slot.original = nullptr;
    slot.sibling_font_binding = nullptr;
    slot.sibling_font_label.clear();
    slot.font_replacement_enabled = false;
    slot.font_replacement.Reset();
    slot.backend = hook_backend::Backend::kAnd64InlineHook;
}

bool IsNativeLabelSetStringSymbol(const TextSymbolSpec& spec) {
    return spec.family == TextSymbolFamily::kCocos &&
           spec.category == TextSymbolCategory::kSetter &&
           spec.owner_path == "cocos2d::Label" &&
           spec.method_name == "setString";
}

bool IsFairyguiNativeTextSymbolAllowed(const TextSymbolSpec& spec) {
    if (!kEnableFairyguiNativeLabelSetStringHook) {
        return false;
    }
    return IsNativeLabelSetStringSymbol(spec);
}

bool ShouldAllowNativeHookAlongsideLuaBinding(const TextSymbolSpec& spec) {
    const NativeTextSetterPolicy* policy = FindNativeTextSetterPolicy(spec);
    return policy != nullptr && policy->allow_alongside_lua_binding;
}

bool ShouldInstallTextSymbolHook(const RuntimeConfig& config,
                                 const TextSymbolSpec& spec,
                                 bool lua_binding_text_hooks_enabled,
                                 bool fairygui_native_mode) {
    if (!spec.native_hook_safe) {
        return false;
    }
    if (fairygui_native_mode && !IsFairyguiNativeTextSymbolAllowed(spec)) {
        return false;
    }
    if (lua_binding_text_hooks_enabled &&
        spec.lua_binding_overlap &&
        !ShouldAllowNativeHookAlongsideLuaBinding(spec)) {
        return false;
    }
    const bool text_hook_needed = config.runtime_text_capture || config.text_replacement_enabled;
    const bool font_hook_needed = HasFontReplacementForKind(config, spec.font_kind);
    if (!text_hook_needed && !font_hook_needed) {
        return false;
    }
    if (!config.label_hooks) {
        return false;
    }
    if (spec.category == TextSymbolCategory::kSetter) {
        return text_hook_needed && spec.has_this && IsValidHookArgIndex(spec.arg_index);
    }
    if (spec.category == TextSymbolCategory::kCreation) {
        return (text_hook_needed || font_hook_needed) && IsValidHookArgIndex(spec.arg_index);
    }
    if (spec.category == TextSymbolCategory::kFontFile) {
        return font_hook_needed && spec.has_this && IsValidHookArgIndex(spec.font_arg_index);
    }
    if (spec.category == TextSymbolCategory::kRender) {
        return false;
    }
    return false;
}

bool InstallTextSymbolHook(const std::string& module_name,
                           const elf_symbols::SymbolInfo& symbol,
                           const TextSymbolSpec& spec,
                           bool lua_binding_text_hooks_enabled,
                           bool fairygui_native_mode,
                           const std::vector<NativeFontSetterTarget>& native_font_setters) {
    if (symbol.address == 0 || !hookutils::IsExecutableAddressInModule(symbol.address, module_name.c_str())) {
        return false;
    }
    if (!IsSymbolLongEnoughForInlineHook(symbol, spec.label.c_str())) {
        return false;
    }

    RuntimeState& state = State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    if (state.installed_entries.find(symbol.address) != state.installed_entries.end()) {
        return false;
    }
    if (!ShouldInstallTextSymbolHook(state.config, spec, lua_binding_text_hooks_enabled, fairygui_native_mode)) {
        return false;
    }

    const int slot_index = FindFreeTextSlotLocked();
    if (slot_index < 0) {
        COCOS_LOGE("no free text hook slot for %s", symbol.name.c_str());
        return false;
    }

    TextHookSlot& slot = g_text_slots[static_cast<size_t>(slot_index)];
    ResetTextSlot(slot);
    slot.entry = symbol.address;
    slot.spec = spec;
    slot.spec.label = module_name + "!" + spec.label;
    slot.spec.symbol = symbol.name;
    const auto native_sibling_font_setter =
        FindNativeSiblingFontSetter(slot.spec, native_font_setters, state.config);
    if (native_sibling_font_setter.has_value()) {
        slot.sibling_font_setter = *native_sibling_font_setter;
    }
    slot.backend = hook_backend::GetPreferredBackend();

    if (!hook_backend::InstallInlineHook(
            reinterpret_cast<void*>(symbol.address),
            reinterpret_cast<void*>(kTextReplacements[static_cast<size_t>(slot_index)]),
            reinterpret_cast<void**>(&slot.original),
            slot.spec.label.c_str(),
            slot.backend)) {
        ResetTextSlot(slot);
        return false;
    }

    slot.active.store(true, std::memory_order_release);
    state.installed_entries.insert(symbol.address);
    COCOS_LOGI("text hook installed: %s @ 0x%" PRIxPTR " slot=%d family=%s category=%s arg=%d layout=%s replace=%d sibling_font=%s backend=%s",
               slot.spec.label.c_str(),
               symbol.address,
               slot_index,
               TextSymbolFamilyName(slot.spec.family),
               TextSymbolCategoryName(slot.spec.category),
               slot.spec.arg_index,
               TextArgumentLayoutName(slot.spec.layout),
               slot.spec.replacement_supported ? 1 : 0,
               slot.sibling_font_setter.label.empty() ? "none" : slot.sibling_font_setter.label.c_str(),
               hook_backend::BackendName(slot.backend));
    return true;
}

LuaBindingApi ResolveLuaApi(const hookutils::ModuleInfo& module_info) {
    LuaBindingApi api{};
    api.gettop = reinterpret_cast<LuaGetTopFn>(elf_symbols::FindExportAddress(module_info, "lua_gettop"));
    api.settop = reinterpret_cast<LuaSetTopFn>(elf_symbols::FindExportAddress(module_info, "lua_settop"));
    api.type = reinterpret_cast<LuaTypeFn>(elf_symbols::FindExportAddress(module_info, "lua_type"));
    api.tolstring = reinterpret_cast<LuaToLStringFn>(elf_symbols::FindExportAddress(module_info, "lua_tolstring"));
    api.tointeger = reinterpret_cast<LuaToIntegerFn>(elf_symbols::FindExportAddress(module_info, "lua_tointeger"));
    api.pushlstring = reinterpret_cast<LuaPushLStringFn>(elf_symbols::FindExportAddress(module_info, "lua_pushlstring"));
    api.pushinteger = reinterpret_cast<LuaPushIntegerFn>(elf_symbols::FindExportAddress(module_info, "lua_pushinteger"));
    api.pushnil = reinterpret_cast<LuaPushNilFn>(elf_symbols::FindExportAddress(module_info, "lua_pushnil"));
    api.replace = reinterpret_cast<LuaReplaceFn>(elf_symbols::FindExportAddress(module_info, "lua_replace"));
    api.setfield = reinterpret_cast<LuaSetFieldFn>(elf_symbols::FindExportAddress(module_info, "lua_setfield"));
    api.pushvalue = reinterpret_cast<LuaPushValueFn>(elf_symbols::FindExportAddress(module_info, "lua_pushvalue"));
    api.rawgeti = reinterpret_cast<LuaRawGetIFn>(elf_symbols::FindExportAddress(module_info, "lua_rawgeti"));
    api.lref = reinterpret_cast<LuaLRefFn>(elf_symbols::FindExportAddress(module_info, "luaL_ref"));
    api.lunref = reinterpret_cast<LuaLUnrefFn>(elf_symbols::FindExportAddress(module_info, "luaL_unref"));
    return api;
}

bool InstallLuaBindingHook(const std::string& module_name,
                           const elf_symbols::SymbolInfo& symbol,
                           const LuaBindingApi& api,
                           const LuaTextBindingRule& rule,
                           uintptr_t sibling_font_address,
                           bool enable_font_replacement) {
    if (!HasLuaBindingReadApi(api) || symbol.address == 0 ||
        !hookutils::IsExecutableAddressInModule(symbol.address, module_name.c_str())) {
        return false;
    }
    if (!IsSymbolLongEnoughForInlineHook(symbol, symbol.name.c_str())) {
        return false;
    }

    RuntimeState& state = State();
    std::lock_guard<std::mutex> _lk(state.mutex);
    if (state.installed_entries.find(symbol.address) != state.installed_entries.end()) {
        return false;
    }
    const int slot_index = FindFreeLuaBindingSlotLocked();
    if (slot_index < 0) {
        COCOS_LOGE("no free Lua binding hook slot for %s", symbol.name.c_str());
        return false;
    }

    LuaBindingHookSlot& slot = g_lua_binding_slots[static_cast<size_t>(slot_index)];
    ResetLuaBindingSlot(slot);
    slot.entry = symbol.address;
    slot.symbol_name = symbol.name;
    slot.label = module_name + "!" + symbol.name;
    slot.api = api;
    slot.font_replacement_enabled = enable_font_replacement;
    if (sibling_font_address != 0 && rule.sibling_font_marker != nullptr) {
        slot.sibling_font_binding = reinterpret_cast<LuaCFunction>(sibling_font_address);
        slot.sibling_font_label = module_name + "!" + rule.sibling_font_marker;
    }
    slot.backend = hook_backend::GetPreferredBackend();

    if (!hook_backend::InstallInlineHook(
            reinterpret_cast<void*>(symbol.address),
            reinterpret_cast<void*>(kLuaBindingReplacements[static_cast<size_t>(slot_index)]),
            reinterpret_cast<void**>(&slot.original),
            slot.label.c_str(),
            slot.backend)) {
        ResetLuaBindingSlot(slot);
        return false;
    }

    slot.active.store(true, std::memory_order_release);
    state.installed_entries.insert(symbol.address);
    COCOS_LOGI("lua binding hook installed: %s @ 0x%" PRIxPTR " slot=%d font=%d sibling=%s sibling_addr=0x%" PRIxPTR " backend=%s",
               slot.label.c_str(),
               symbol.address,
               slot_index,
               slot.font_replacement_enabled ? 1 : 0,
               slot.sibling_font_label.empty() ? "none" : slot.sibling_font_label.c_str(),
               sibling_font_address,
               hook_backend::BackendName(slot.backend));
    return true;
}

}  // namespace

void InstallTextCaptureHooksForModule(const std::string& module_name,
                                      const hookutils::ModuleInfo& module_info) {
    const RuntimeConfig config = SnapshotConfig();
    if (!config.label_hooks && !config.lua_binding_hooks && !config.fairygui_hooks) {
        return;
    }

    size_t cocos_native_text_count = 0;
    size_t cocos_lua_binding_count = 0;
    std::vector<elf_symbols::SymbolInfo> symbols = elf_symbols::EnumerateFunctionSymbols(module_info);
    const bool fairygui_native_mode = config.fairygui_hooks && HasFairyguiSymbols(symbols);
    const bool font_replacement_enabled = HasAnyFontReplacement(config);
    const bool lua_binding_text_hooks_enabled = config.lua_binding_hooks && !fairygui_native_mode;
    const bool lua_binding_font_hooks_enabled =
        config.lua_binding_hooks &&
        font_replacement_enabled &&
        !fairygui_native_mode;
    const bool lua_binding_route_enabled =
        lua_binding_text_hooks_enabled || lua_binding_font_hooks_enabled;
    const char* text_route = !fairygui_native_mode
        ? "lua_binding"
        : (kEnableFairyguiNativeLabelSetStringHook
            ? "fairygui_native_label_only"
            : "fairygui_native_label_disabled");
    const char* font_route = !font_replacement_enabled
        ? "off"
        : (fairygui_native_mode ? "fairygui_native_disabled" : "lua_binding/native_sibling");

    LuaBindingApi lua_api{};
    if (lua_binding_route_enabled) {
        lua_api = ResolveLuaApi(module_info);
        if (!HasLuaBindingReadApi(lua_api)) {
            COCOS_LOGI("lua binding scan skipped for %s: lua_gettop/lua_type/lua_tolstring missing",
                       module_name.c_str());
        } else if (config.text_replacement_enabled && !HasLuaBindingReplacementApi(lua_api)) {
            COCOS_LOGI("lua binding replacement degraded for %s: lua_pushlstring/lua_replace missing",
                       module_name.c_str());
        }
    }
    COCOS_LOGI("route mode %s: text=%s font=%s",
               module_name.c_str(),
               text_route,
               font_route);

    const std::vector<NativeFontSetterTarget> native_font_setters =
        FindNativeFontSetters(module_name, symbols, config);
    if (!native_font_setters.empty()) {
        for (const NativeFontSetterTarget& setter : native_font_setters) {
            COCOS_LOGI("native font setter registered: %s @ 0x%" PRIxPTR " kind=%s layout=%s",
                       setter.label.c_str(),
                       setter.entry,
                       FontResourceKindName(setter.kind),
                       TextArgumentLayoutName(setter.layout));
        }
    } else if (font_replacement_enabled) {
        COCOS_LOGI("native font setter missing for %s", module_name.c_str());
    }

    for (const auto& symbol : symbols) {
        bool native_text_installed = false;
        const auto text_spec = config.label_hooks
            ? ResolveTextSymbolSpec(symbol.name)
            : std::optional<TextSymbolSpec>{};
        if (text_spec.has_value() &&
            ShouldInstallTextSymbolHook(config, *text_spec, lua_binding_text_hooks_enabled, fairygui_native_mode)) {
            if (InstallTextSymbolHook(
                    module_name,
                    symbol,
                    *text_spec,
                    lua_binding_text_hooks_enabled,
                    fairygui_native_mode,
                    native_font_setters)) {
                native_text_installed = true;
                ++cocos_native_text_count;
            }
        }
        if (native_text_installed) {
            continue;
        }

        const LuaTextBindingRule* lua_rule = FindLuaTextBindingRule(symbol.name);
        if (lua_binding_route_enabled &&
            HasLuaBindingReadApi(lua_api) &&
            lua_rule != nullptr &&
            ShouldInstallLuaBindingSymbol(
                config,
                symbol.name,
                *lua_rule,
                lua_binding_text_hooks_enabled,
                lua_binding_font_hooks_enabled)) {
            const uintptr_t sibling_font_address =
                FindLuaBindingAddressByMarker(symbols, lua_rule->sibling_font_marker);
            if (lua_binding_font_hooks_enabled &&
                lua_rule->sibling_font_marker != nullptr &&
                sibling_font_address == 0) {
                COCOS_LOGI("lua binding sibling font missing: %s sibling=%s",
                           symbol.name.c_str(),
                           lua_rule->sibling_font_marker);
            }
            if (InstallLuaBindingHook(
                    module_name,
                    symbol,
                    lua_api,
                    *lua_rule,
                    sibling_font_address,
                    lua_binding_font_hooks_enabled)) {
                ++cocos_lua_binding_count;
            }
        }
    }

    COCOS_LOGI("symbol scan %s: symbols=%zu text_route=%s font_route=%s cocos_native_text_hooks=%zu cocos_lua_binding_hooks=%zu",
               module_name.c_str(),
               symbols.size(),
               text_route,
               font_route,
               cocos_native_text_count,
               cocos_lua_binding_count);
}

}  // namespace cocos_runtime::internal
