#ifndef IL2FUSION_PLUGINS_TEXT_EXTRACTOR_H
#define IL2FUSION_PLUGINS_TEXT_EXTRACTOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace text_extractor {

// 设置进程名并异步启动文本拦截初始化（幂等）。
void Init(const std::string& process_name);

// 设置当前 hook backend；默认 And64InlineHook，1 表示 Dobby。
void SetHookBackend(std::int32_t backend_value);

// 开启后将捕获到的 Unity set_text 入参替换为固定文本。
void SetTextReplacement(bool enabled, const std::string& text);

// 更新 JSON 配置；优先用于 Unity 的 RVA/translation 配置。
void UpdateTargetsJson(const std::string& json);

// 更新方法全名列表；若 libil2cpp 已就绪会立即重新安装 hook。
void UpdateTargets(const std::vector<std::string>& new_targets);

}  // namespace text_extractor

#endif  // IL2FUSION_PLUGINS_TEXT_EXTRACTOR_H
