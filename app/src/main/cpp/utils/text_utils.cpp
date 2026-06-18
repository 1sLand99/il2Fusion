#include "text_utils.h"

#include <cstdint>
#include <cstdio>

namespace textutils {
namespace {

bool IsSafeFileChar(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '.' || ch == '_' || ch == '-';
}

bool IsChineseCodePoint(std::uint32_t cp) {
    return (cp >= 0x3400U && cp <= 0x4DBFU) ||
           (cp >= 0x4E00U && cp <= 0x9FFFU) ||
           (cp >= 0xF900U && cp <= 0xFAFFU) ||
           (cp >= 0x20000U && cp <= 0x2A6DFU) ||
           (cp >= 0x2A700U && cp <= 0x2B73FU) ||
           (cp >= 0x2B740U && cp <= 0x2B81FU) ||
           (cp >= 0x2B820U && cp <= 0x2CEAFU) ||
           (cp >= 0x2CEB0U && cp <= 0x2EBEFU) ||
           (cp >= 0x30000U && cp <= 0x3134FU) ||
           (cp >= 0x31350U && cp <= 0x323AFU);
}

bool DecodeNextUtf8CodePoint(const std::string& text, size_t* index, std::uint32_t* out) {
    if (index == nullptr || out == nullptr || *index >= text.size()) {
        return false;
    }

    const unsigned char ch = static_cast<unsigned char>(text[*index]);
    if (ch <= 0x7FU) {
        *out = ch;
        ++(*index);
        return true;
    }

    size_t width = 0;
    std::uint32_t cp = 0;
    if ((ch & 0xE0U) == 0xC0U) {
        width = 2;
        cp = ch & 0x1FU;
    } else if ((ch & 0xF0U) == 0xE0U) {
        width = 3;
        cp = ch & 0x0FU;
    } else if ((ch & 0xF8U) == 0xF0U) {
        width = 4;
        cp = ch & 0x07U;
    } else {
        return false;
    }

    if (*index + width > text.size()) {
        return false;
    }
    for (size_t offset = 1; offset < width; ++offset) {
        const unsigned char cont = static_cast<unsigned char>(text[*index + offset]);
        if ((cont & 0xC0U) != 0x80U) {
            return false;
        }
        cp = (cp << 6U) | (cont & 0x3FU);
    }

    *index += width;
    *out = cp;
    return true;
}

}  // namespace

std::string Trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string Basename(const std::string& value) {
    const size_t slash = value.find_last_of("/\\");
    if (slash == std::string::npos) {
        return value;
    }
    return value.substr(slash + 1);
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

std::string PreviewBytes(const char* data, size_t size, size_t max_bytes) {
    if (data == nullptr || size == 0 || max_bytes == 0) {
        return {};
    }
    const size_t count = size < max_bytes ? size : max_bytes;
    std::string preview;
    preview.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            preview.push_back(' ');
        } else if (ch < 0x20) {
            preview.push_back('.');
        } else {
            preview.push_back(static_cast<char>(ch));
        }
    }
    return preview;
}

std::string SanitizeFileName(const std::string& name, size_t max_length) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char ch : name) {
        out.push_back(IsSafeFileChar(ch) ? static_cast<char>(ch) : '_');
        if (out.size() >= max_length) {
            break;
        }
    }
    if (out.empty()) {
        out = "anonymous";
    }
    return out;
}

bool IsValidUtf8(const std::string& text) {
    size_t index = 0;
    while (index < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (ch == 0x00) {
            return false;
        }
        if (ch <= 0x7F) {
            ++index;
            continue;
        }

        size_t width = 0;
        std::uint32_t code_point = 0;
        if ((ch & 0xE0U) == 0xC0U) {
            width = 2;
            code_point = ch & 0x1FU;
            if (code_point == 0) {
                return false;
            }
        } else if ((ch & 0xF0U) == 0xE0U) {
            width = 3;
            code_point = ch & 0x0FU;
        } else if ((ch & 0xF8U) == 0xF0U) {
            width = 4;
            code_point = ch & 0x07U;
        } else {
            return false;
        }

        if (index + width > text.size()) {
            return false;
        }
        for (size_t offset = 1; offset < width; ++offset) {
            const unsigned char cont = static_cast<unsigned char>(text[index + offset]);
            if ((cont & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (cont & 0x3FU);
        }

        if ((width == 2 && code_point < 0x80U) ||
            (width == 3 && code_point < 0x800U) ||
            (width == 4 && code_point < 0x10000U) ||
            code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU) ||
            code_point == 0xFFFD) {
            return false;
        }
        index += width;
    }
    return true;
}

bool LooksLikeText(const std::string& text, size_t max_bytes) {
    if (text.empty() || text.size() > max_bytes) {
        return false;
    }
    if (!IsValidUtf8(text)) {
        return false;
    }
    size_t controls = 0;
    for (unsigned char ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            continue;
        }
        if (ch < 0x20) {
            ++controls;
        }
    }
    return controls * 4 <= text.size();
}

bool ContainsChinese(const std::string& text) {
    size_t index = 0;
    while (index < text.size()) {
        std::uint32_t cp = 0;
        if (!DecodeNextUtf8CodePoint(text, &index, &cp)) {
            return false;
        }
        if (IsChineseCodePoint(cp)) {
            return true;
        }
    }
    return false;
}

}  // namespace textutils
