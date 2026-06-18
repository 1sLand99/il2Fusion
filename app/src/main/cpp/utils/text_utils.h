#ifndef IL2FUSION_TEXT_UTILS_H
#define IL2FUSION_TEXT_UTILS_H

#include <cstddef>
#include <string>

namespace textutils {

std::string Trim(const std::string& value);
std::string Basename(const std::string& value);
std::string JsonEscape(const std::string& value);
std::string PreviewBytes(const char* data, size_t size, size_t max_bytes = 240);
std::string SanitizeFileName(const std::string& name, size_t max_length = 96);
bool IsValidUtf8(const std::string& text);
bool LooksLikeText(const std::string& text, size_t max_bytes = 4096);
bool ContainsChinese(const std::string& text);

}  // namespace textutils

#endif  // IL2FUSION_TEXT_UTILS_H
