#include "cocos_font_replacement.h"

#include "cocos_log.h"

#include "../../utils/text_utils.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

namespace cocos_runtime::internal {
namespace {

constexpr int kLuaRegistryIndex = -10000;
constexpr int kLuaRefNil = -1;
constexpr int kLuaTypeTable = 5;
constexpr const char* kRichTextFontFaceKey = "fontFace";

thread_local std::string g_native_replacement_font_path;

struct SavedLuaRef {
    int ref = kLuaRefNil;
    bool is_nil = false;
};

std::optional<std::string> SelectFontReplacement(FontResourceKind kind,
                                                 const RuntimeConfig& runtime_config) {
    const FontReplacementConfig& config = runtime_config.font_replacement;
    if (!config.enabled) {
        return std::nullopt;
    }
    if (kind == FontResourceKind::kTtf && !config.ttf_path.empty()) {
        return config.ttf_path;
    }
    if (kind == FontResourceKind::kFnt && !config.bmfont_fnt_path.empty()) {
        return config.bmfont_fnt_path;
    }
    return std::nullopt;
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

bool ShouldReplaceLuaFontValue(FontResourceKind kind, const std::string& original_font) {
    if (kind != FontResourceKind::kFnt) {
        return true;
    }
    return IsLikelyBmFontPath(original_font) && !ShouldSkipBmFontReplacement(original_font);
}

}  // namespace

const std::string* GetReplacementFontString(FontResourceKind kind, const RuntimeConfig& config) {
    const auto font_path = SelectFontReplacement(kind, config);
    if (!font_path.has_value()) {
        return nullptr;
    }
    g_native_replacement_font_path = *font_path;
    return &g_native_replacement_font_path;
}

bool TryReplaceLuaFontBinding(const std::string& label,
                              void* lua_state,
                              int top,
                              int replaced_text_index,
                              const std::array<int, 2>& font_indexes,
                              size_t font_index_count,
                              FontResourceKind font_kind,
                              const LuaBindingApi& api,
                              const RuntimeConfig& config,
                              LuaFontReplacementState* state) {
    if (font_index_count == 0 || font_kind == FontResourceKind::kNone ||
        !config.font_replacement.enabled || state == nullptr) {
        return false;
    }

    bool replaced = false;
    const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
    if (!HasLuaBindingReplacementApi(api)) {
        if (log_probe) {
            COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                             "%s skipped: lua_pushlstring/lua_replace missing",
                             label.c_str());
        }
        return false;
    }

    if (log_probe) {
        COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                         "%s kind=%s top=%d indexes=%zu",
                         label.c_str(),
                         FontResourceKindName(font_kind),
                         top,
                         font_index_count);
    }
    for (size_t i = 0; i < font_index_count; ++i) {
        const int font_index = font_indexes[i];
        if (replaced_text_index > 0 && font_index == replaced_text_index) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d: same as replaced text",
                                 label.c_str(),
                                 font_index);
            }
            continue;
        }
        if (font_index < 1 || font_index > top) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d: top=%d",
                                 label.c_str(),
                                 font_index,
                                 top);
            }
            continue;
        }

        const int value_type = api.type(lua_state, font_index);
        if (value_type != kLuaTypeString) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d: type=%d",
                                 label.c_str(),
                                 font_index,
                                 value_type);
            }
            continue;
        }

        size_t len = 0;
        const char* data = api.tolstring(lua_state, font_index, &len);
        if (data == nullptr || len == 0 || len > kMaxTextBytes) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d: len=%zu",
                                 label.c_str(),
                                 font_index,
                                 len);
            }
            continue;
        }

        std::string original_font(data, len);
        if (!ShouldReplaceLuaFontValue(font_kind, original_font)) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d #%s#: not replaceable %s",
                                 label.c_str(),
                                 font_index,
                                 textutils::PreviewBytes(original_font.c_str(), original_font.size()).c_str(),
                                 FontResourceKindName(font_kind));
            }
            continue;
        }
        const auto font_path = SelectFontReplacement(font_kind, config);
        if (!font_path.has_value()) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d #%s#: no replacement path",
                                 label.c_str(),
                                 font_index,
                                 textutils::PreviewBytes(original_font.c_str(), original_font.size()).c_str());
            }
            continue;
        }
        if (*font_path == original_font) {
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped stack#%d #%s#: already selected font",
                                 label.c_str(),
                                 font_index,
                                 textutils::PreviewBytes(original_font.c_str(), original_font.size()).c_str());
            }
            continue;
        }

        api.pushlstring(lua_state, font_path->data(), font_path->size());
        api.replace(lua_state, font_index);
        replaced = true;
        COCOS_EVENT_LOGI(cocos_log::kLuaFontReplace,
                         "%s kind=%s stack#%d #%s# -> #%s#",
                         label.c_str(),
                         FontResourceKindName(font_kind),
                         font_index,
                         textutils::PreviewBytes(original_font.c_str(), original_font.size()).c_str(),
                         font_path->c_str());
    }
    return replaced;
}

bool TryApplyLuaSiblingFontBinding(const std::string& label,
                                   const std::string& sibling_label,
                                   void* lua_state,
                                   LuaCFunction sibling_font_binding,
                                   bool has_integer_arg,
                                   long long integer_arg,
                                   FontResourceKind font_kind,
                                   const LuaBindingApi& api,
                                   const RuntimeConfig& config,
                                   LuaFontReplacementState* state) {
    if (lua_state == nullptr || sibling_font_binding == nullptr || state == nullptr ||
        api.gettop == nullptr || api.pushlstring == nullptr) {
        return false;
    }
    const auto font_path = SelectFontReplacement(font_kind, config);
    if (!font_path.has_value()) {
        return false;
    }
    if (has_integer_arg && api.pushinteger == nullptr) {
        const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
        if (log_probe) {
            COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                             "%s skipped sibling %s: lua_pushinteger missing",
                             label.c_str(),
                             sibling_label.c_str());
        }
        return false;
    }

    int top = api.gettop(lua_state);
    if (top < 1) {
        const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
        if (log_probe) {
            COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                             "%s skipped sibling %s: top=%d",
                             label.c_str(),
                             sibling_label.c_str(),
                             top);
        }
        return false;
    }
    if (top != 1) {
        if (api.settop == nullptr) {
            const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped sibling %s: top=%d lua_settop missing",
                                 label.c_str(),
                                 sibling_label.c_str(),
                                 top);
            }
            return false;
        }
        api.settop(lua_state, 1);
    }

    api.pushlstring(lua_state, font_path->data(), font_path->size());
    if (has_integer_arg) {
        api.pushinteger(lua_state, integer_arg);
    }
    const int sibling_result = sibling_font_binding(lua_state);
    if (api.settop != nullptr) {
        api.settop(lua_state, 1);
    }
    if (sibling_result < 0) {
        const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
        if (log_probe) {
            COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                             "%s sibling %s returned %d",
                             label.c_str(),
                             sibling_label.c_str(),
                             sibling_result);
        }
        return false;
    }

    COCOS_EVENT_LOGI(cocos_log::kLuaFontReplace,
                     "%s sibling=%s kind=%s path=%s result=%d",
                     label.c_str(),
                     sibling_label.c_str(),
                     FontResourceKindName(font_kind),
                     font_path->c_str(),
                     sibling_result);
    return true;
}

bool TrySetLuaRichTextDefaultFont(const std::string& label,
                                  void* lua_state,
                                  int top,
                                  int defaults_index,
                                  FontResourceKind font_kind,
                                  const LuaBindingApi& api,
                                  const RuntimeConfig& config,
                                  LuaFontReplacementState* state) {
    if (lua_state == nullptr || defaults_index < 1 || defaults_index > top || state == nullptr ||
        api.type == nullptr || api.pushlstring == nullptr || api.setfield == nullptr) {
        return false;
    }
    const auto font_path = SelectFontReplacement(font_kind, config);
    if (!font_path.has_value()) {
        return false;
    }
    const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
    if (api.type(lua_state, defaults_index) != kLuaTypeTable) {
        if (log_probe) {
            COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                             "%s skipped rich text defaults stack#%d: not table",
                             label.c_str(),
                             defaults_index);
        }
        return false;
    }

    api.pushlstring(lua_state, font_path->data(), font_path->size());
    api.setfield(lua_state, defaults_index, kRichTextFontFaceKey);
    COCOS_EVENT_LOGI(cocos_log::kLuaFontReplace,
                     "%s richtext defaults.%s=%s stack#%d",
                     label.c_str(),
                     kRichTextFontFaceKey,
                     font_path->c_str(),
                     defaults_index);
    return true;
}

bool TryApplyLuaSiblingFontBeforeTextBinding(const std::string& label,
                                             const std::string& sibling_label,
                                             void* lua_state,
                                             LuaCFunction sibling_font_binding,
                                             int text_index,
                                             const std::string& restored_text,
                                             FontResourceKind font_kind,
                                             const LuaBindingApi& api,
                                             const RuntimeConfig& config,
                                             LuaFontReplacementState* state) {
    if (lua_state == nullptr || sibling_font_binding == nullptr || state == nullptr ||
        text_index < 2 || api.gettop == nullptr || api.settop == nullptr ||
        api.pushlstring == nullptr || api.replace == nullptr) {
        return false;
    }
    const auto font_path = SelectFontReplacement(font_kind, config);
    if (!font_path.has_value()) {
        return false;
    }

    const int top = api.gettop(lua_state);
    if (top < text_index) {
        const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
        if (log_probe) {
            COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                             "%s skipped sibling %s before text: top=%d text_index=%d",
                             label.c_str(),
                             sibling_label.c_str(),
                             top,
                             text_index);
        }
        return false;
    }

    if (top == text_index && text_index == 2) {
        api.pushlstring(lua_state, font_path->data(), font_path->size());
        api.replace(lua_state, text_index);
        const int sibling_result = sibling_font_binding(lua_state);
        api.settop(lua_state, top);
        api.pushlstring(lua_state, restored_text.data(), restored_text.size());
        api.replace(lua_state, text_index);
        COCOS_EVENT_LOGI(cocos_log::kLuaFontReplace,
                         "%s sibling(before)=%s kind=%s path=%s result=%d",
                         label.c_str(),
                         sibling_label.c_str(),
                         FontResourceKindName(font_kind),
                         font_path->c_str(),
                         sibling_result);
        return true;
    }

    std::vector<SavedLuaRef> saved_refs;
    if (top > 1) {
        if (api.pushvalue == nullptr || api.pushnil == nullptr || api.rawgeti == nullptr ||
            api.lref == nullptr || api.lunref == nullptr) {
            const bool log_probe = state->probe_logs.fetch_add(1, std::memory_order_relaxed) < 8;
            if (log_probe) {
                COCOS_EVENT_LOGI(cocos_log::kLuaFontProbe,
                                 "%s skipped sibling %s before text: lua registry api missing",
                                 label.c_str(),
                                 sibling_label.c_str());
            }
            return false;
        }
        saved_refs.reserve(static_cast<size_t>(top - 1));
        for (int index = 2; index <= top; ++index) {
            api.pushvalue(lua_state, index);
            const int ref = api.lref(lua_state, kLuaRegistryIndex);
            saved_refs.push_back({ref, ref == kLuaRefNil});
        }
        api.settop(lua_state, 1);
    }

    api.pushlstring(lua_state, font_path->data(), font_path->size());
    const int sibling_result = sibling_font_binding(lua_state);
    api.settop(lua_state, 1);
    if (!saved_refs.empty()) {
        for (const SavedLuaRef& saved : saved_refs) {
            if (saved.is_nil) {
                api.pushnil(lua_state);
            } else {
                api.rawgeti(lua_state, kLuaRegistryIndex, saved.ref);
            }
        }
        for (const SavedLuaRef& saved : saved_refs) {
            if (!saved.is_nil) {
                api.lunref(lua_state, kLuaRegistryIndex, saved.ref);
            }
        }
    }
    api.pushlstring(lua_state, restored_text.data(), restored_text.size());
    api.replace(lua_state, text_index);

    COCOS_EVENT_LOGI(cocos_log::kLuaFontReplace,
                     "%s sibling(before)=%s kind=%s path=%s result=%d",
                     label.c_str(),
                     sibling_label.c_str(),
                     FontResourceKindName(font_kind),
                     font_path->c_str(),
                     sibling_result);
    return true;
}

}  // namespace cocos_runtime::internal
