#ifndef IL2FUSION_PLUGINS_COCOS_RUNTIME_H
#define IL2FUSION_PLUGINS_COCOS_RUNTIME_H

#include <cstdint>
#include <string>
#include <vector>

namespace cocos_runtime {

struct LuaReplacementRule {
    bool enabled = true;
    std::string script_name;
    std::string replaced_code;
    std::string replace_code;
    std::string prepend_code;
};

struct FontReplacementConfig {
    bool enabled = false;
    std::string ttf_path;
    std::string bmfont_fnt_path;
};

void Init(const std::string& process_name);
void SetHookBackend(std::int32_t backend_value);
void SetEnabled(bool enabled);
void SetTextReplacement(bool enabled, const std::string& text);
void SetTextReplacementDelay(bool enabled, std::int32_t delay_ms);
void SetTextBurstDelayGuard(bool enabled, std::int32_t hold_ms);
void SetRuntimeTextCaptureEnabled(bool enabled);
void SetTextPersistence(bool enabled, bool chinese_only);
void SetFontReplacement(FontReplacementConfig config);
void UpdateLuaReplacementRules(std::vector<LuaReplacementRule> rules);
void StartRuntimeIfNeeded();

}  // namespace cocos_runtime

#endif  // IL2FUSION_PLUGINS_COCOS_RUNTIME_H
