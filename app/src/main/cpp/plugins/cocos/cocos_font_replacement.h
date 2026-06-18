#ifndef IL2FUSION_PLUGINS_COCOS_FONT_REPLACEMENT_H
#define IL2FUSION_PLUGINS_COCOS_FONT_REPLACEMENT_H

#include "cocos_lua_binding_api.h"
#include "cocos_runtime_state.h"
#include "cocos_text_symbols.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace cocos_runtime::internal {

struct LuaFontReplacementState {
    std::atomic_uint32_t probe_logs{0};

    void Reset() {
        probe_logs.store(0, std::memory_order_relaxed);
    }
};

const std::string* GetReplacementFontString(FontResourceKind kind, const RuntimeConfig& config);

bool TryReplaceLuaFontBinding(const std::string& label,
                              void* lua_state,
                              int top,
                              int replaced_text_index,
                              const std::array<int, 2>& font_indexes,
                              size_t font_index_count,
                              FontResourceKind font_kind,
                              const LuaBindingApi& api,
                              const RuntimeConfig& config,
                              LuaFontReplacementState* state);

bool TryApplyLuaSiblingFontBinding(const std::string& label,
                                   const std::string& sibling_label,
                                   void* lua_state,
                                   LuaCFunction sibling_font_binding,
                                   bool has_integer_arg,
                                   long long integer_arg,
                                   FontResourceKind font_kind,
                                   const LuaBindingApi& api,
                                   const RuntimeConfig& config,
                                   LuaFontReplacementState* state);

bool TrySetLuaRichTextDefaultFont(const std::string& label,
                                  void* lua_state,
                                  int top,
                                  int defaults_index,
                                  FontResourceKind font_kind,
                                  const LuaBindingApi& api,
                                  const RuntimeConfig& config,
                                  LuaFontReplacementState* state);

bool TryApplyLuaSiblingFontBeforeTextBinding(const std::string& label,
                                             const std::string& sibling_label,
                                             void* lua_state,
                                             LuaCFunction sibling_font_binding,
                                             int text_index,
                                             const std::string& restored_text,
                                             FontResourceKind font_kind,
                                             const LuaBindingApi& api,
                                             const RuntimeConfig& config,
                                             LuaFontReplacementState* state);

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_FONT_REPLACEMENT_H
