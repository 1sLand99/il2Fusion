#include "utils.h"

#include <cctype>

namespace textutils {

bool ShouldFilter(const std::string& text) {
    if (text.empty()) {
        return true;
    }

    bool hasDigit = false;
    bool hasLetter = false;
    bool hasPunct = false;
    bool hasSpace = false;

    for (unsigned char ch : text) {
        if (std::isdigit(ch)) {
            hasDigit = true;
        } else if (std::isalpha(ch)) {
            hasLetter = true;
        } else if (std::ispunct(ch)) {
            hasPunct = true;
        } else if (std::isspace(ch)) {
            hasSpace = true;
        } else {
            // 包含非 ASCII 可见字符，不过滤
            return false;
        }
    }

    // 只包含数字/英文/标点/空格的组合（含任意排列）均过滤
    return hasDigit || hasLetter || hasPunct || hasSpace;
}

}  // namespace textutils
