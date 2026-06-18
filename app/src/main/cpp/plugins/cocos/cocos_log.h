#ifndef IL2FUSION_PLUGINS_COCOS_LOG_H
#define IL2FUSION_PLUGINS_COCOS_LOG_H

#include "../../utils/log.h"

namespace cocos_runtime::internal::cocos_log {

inline constexpr const char* kLuaBinding = "LuaBinding";
inline constexpr const char* kLuaFontProbe = "LuaFontProbe";
inline constexpr const char* kLuaFontReplace = "LuaFontReplace";
inline constexpr const char* kNativeFontReplace = "NativeFontReplace";
inline constexpr const char* kLuaReplace = "LuaReplace";
inline constexpr const char* kLuaTextReplace = "LuaTextReplace";
inline constexpr const char* kText = "Text";
inline constexpr const char* kTextReplace = "TextReplace";

}  // namespace cocos_runtime::internal::cocos_log

#define COCOS_LOGI(fmt, ...) LOGI("[Cocos] " fmt, ##__VA_ARGS__)
#define COCOS_LOGW(fmt, ...) LOGW("[Cocos] " fmt, ##__VA_ARGS__)
#define COCOS_LOGE(fmt, ...) LOGE("[Cocos] " fmt, ##__VA_ARGS__)

#define COCOS_EVENT_LOGI(event, fmt, ...) LOGI("[Cocos][%s] " fmt, event, ##__VA_ARGS__)
#define COCOS_EVENT_LOGW(event, fmt, ...) LOGW("[Cocos][%s] " fmt, event, ##__VA_ARGS__)
#define COCOS_EVENT_LOGE(event, fmt, ...) LOGE("[Cocos][%s] " fmt, event, ##__VA_ARGS__)

#endif  // IL2FUSION_PLUGINS_COCOS_LOG_H
