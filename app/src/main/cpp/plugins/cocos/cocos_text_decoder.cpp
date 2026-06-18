#include "cocos_text_decoder.h"

#include "cocos_runtime_state.h"

#include "../../utils/safe_memory.h"
#include "../../utils/text_utils.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace cocos_runtime::internal {
namespace {

bool ReadSizedUtf8(uintptr_t data_ptr, size_t size, std::string* out) {
    if (out == nullptr || size > kMaxTextBytes || (data_ptr == 0 && size != 0)) {
        return false;
    }
    if (size == 0) {
        out->clear();
        return true;
    }
    if (safe_memory::ReadableLimit(data_ptr, size) < size) {
        return false;
    }
    out->assign(reinterpret_cast<const char*>(data_ptr), size);
    return textutils::LooksLikeText(*out);
}

bool TryDecodeLibcxxNdkString(uintptr_t object, std::string* out) {
    unsigned char first = 0;
    if (!safe_memory::ReadBytesAt(object, &first, sizeof(first))) {
        return false;
    }

    const bool is_long = (first & 1U) != 0;
    if (!is_long) {
        const size_t size = first >> 1U;
        const size_t max_short = sizeof(uintptr_t) == 8 ? 22 : 10;
        if (size > max_short) {
            return false;
        }
        return ReadSizedUtf8(object + 1, size, out);
    }

    uintptr_t data_ptr = 0;
    size_t size = 0;
    if (!safe_memory::ReadBytesAt(object + sizeof(uintptr_t), &size, sizeof(size)) ||
        !safe_memory::ReadBytesAt(object + sizeof(uintptr_t) * 2, &data_ptr, sizeof(data_ptr))) {
        return false;
    }
    return ReadSizedUtf8(data_ptr, size, out);
}

bool TryDecodeKnownLibcxxNdkStringFast(uintptr_t object, std::string* out) {
    if (object == 0 || out == nullptr) {
        return false;
    }

    const size_t object_size = sizeof(uintptr_t) == 8 ? 24 : 12;
    if (safe_memory::ReadableLimit(object, object_size) < object_size) {
        return false;
    }

    unsigned char first = 0;
    std::memcpy(&first, reinterpret_cast<const void*>(object), sizeof(first));

    const bool is_long = (first & 1U) != 0;
    if (!is_long) {
        const size_t size = first >> 1U;
        const size_t max_short = sizeof(uintptr_t) == 8 ? 22 : 10;
        if (size > max_short) {
            return false;
        }
        out->assign(reinterpret_cast<const char*>(object + 1), size);
        return size == 0 || textutils::LooksLikeText(*out);
    }

    uintptr_t data_ptr = 0;
    size_t size = 0;
    std::memcpy(&size, reinterpret_cast<const void*>(object + sizeof(uintptr_t)), sizeof(size));
    std::memcpy(&data_ptr, reinterpret_cast<const void*>(object + sizeof(uintptr_t) * 2), sizeof(data_ptr));
    return ReadSizedUtf8(data_ptr, size, out);
}

bool TryDecodeAlternateLibcxxString(uintptr_t object, std::string* out) {
    const size_t size_byte_offset = sizeof(uintptr_t) == 8 ? 23 : 11;
    unsigned char size_byte = 0;
    if (!safe_memory::ReadBytesAt(object + size_byte_offset, &size_byte, sizeof(size_byte))) {
        return false;
    }

    const bool is_long = (size_byte & 0x80U) != 0;
    if (!is_long) {
        const size_t size = size_byte & 0x7fU;
        const size_t max_short = sizeof(uintptr_t) == 8 ? 22 : 10;
        if (size > max_short) {
            return false;
        }
        return ReadSizedUtf8(object, size, out);
    }

    uintptr_t data_ptr = 0;
    size_t size = 0;
    if (!safe_memory::ReadBytesAt(object, &data_ptr, sizeof(data_ptr)) ||
        !safe_memory::ReadBytesAt(object + sizeof(uintptr_t), &size, sizeof(size))) {
        return false;
    }
    return ReadSizedUtf8(data_ptr, size, out);
}

bool TryDecodeGnustlCowString(uintptr_t object, std::string* out) {
    uintptr_t data_ptr = 0;
    if (!safe_memory::ReadBytesAt(object, &data_ptr, sizeof(data_ptr)) || data_ptr == 0) {
        return false;
    }

    const uintptr_t header_size = sizeof(uintptr_t) * 3;
    if (data_ptr < header_size) {
        return false;
    }

    size_t size = 0;
    if (!safe_memory::ReadBytesAt(data_ptr - header_size, &size, sizeof(size))) {
        return false;
    }
    return ReadSizedUtf8(data_ptr, size, out);
}

bool TryDecodeAutoStdString(uintptr_t object, std::string* out) {
    std::string candidate_text;
    bool has_empty = false;
    if (TryDecodeLibcxxNdkString(object, &candidate_text)) {
        if (!candidate_text.empty()) {
            *out = candidate_text;
            return true;
        }
        has_empty = true;
    }
    if (TryDecodeAlternateLibcxxString(object, &candidate_text)) {
        if (!candidate_text.empty()) {
            *out = candidate_text;
            return true;
        }
        has_empty = true;
    }
    if (TryDecodeGnustlCowString(object, &candidate_text)) {
        if (!candidate_text.empty()) {
            *out = candidate_text;
            return true;
        }
        has_empty = true;
    }
    if (has_empty) {
        out->clear();
        return true;
    }
    return false;
}

bool TryDecodeByLayout(uintptr_t object, TextArgumentLayout layout, std::string* out) {
    switch (layout) {
        case TextArgumentLayout::kCString:
            return safe_memory::ReadCStringAt(object, kMaxTextBytes, out) &&
                   textutils::LooksLikeText(*out);
        case TextArgumentLayout::kLibcxxNdk:
            return TryDecodeLibcxxNdkString(object, out);
        case TextArgumentLayout::kLibcxxAlternate:
            return TryDecodeAlternateLibcxxString(object, out);
        case TextArgumentLayout::kGnustlCow:
            return TryDecodeGnustlCowString(object, out);
        case TextArgumentLayout::kAutoStdString:
            return TryDecodeAutoStdString(object, out);
    }
    return false;
}

bool DecodeStdStringWithFallback(uintptr_t object,
                                 TextArgumentLayout preferred_layout,
                                 std::string* out,
                                 TextArgumentLayout* decoded_layout) {
    constexpr TextArgumentLayout kStdLayouts[] = {
        TextArgumentLayout::kLibcxxNdk,
        TextArgumentLayout::kLibcxxAlternate,
        TextArgumentLayout::kGnustlCow,
    };

    bool has_empty = false;
    TextArgumentLayout empty_layout = preferred_layout;
    auto try_candidate = [&](TextArgumentLayout candidate) -> bool {
        std::string candidate_text;
        if (!TryDecodeByLayout(object, candidate, &candidate_text)) {
            return false;
        }
        if (!candidate_text.empty()) {
            *out = std::move(candidate_text);
            if (decoded_layout != nullptr) {
                *decoded_layout = candidate;
            }
            return true;
        }
        if (!has_empty) {
            has_empty = true;
            empty_layout = candidate;
        }
        return false;
    };

    if (preferred_layout != TextArgumentLayout::kAutoStdString &&
        preferred_layout != TextArgumentLayout::kCString &&
        try_candidate(preferred_layout)) {
        if (decoded_layout != nullptr) {
            *decoded_layout = preferred_layout;
        }
        return true;
    }

    for (TextArgumentLayout candidate : kStdLayouts) {
        if (candidate == preferred_layout) {
            continue;
        }
        if (try_candidate(candidate)) {
            return true;
        }
    }
    if (has_empty) {
        out->clear();
        if (decoded_layout != nullptr) {
            *decoded_layout = empty_layout;
        }
        return true;
    }
    return false;
}

void WritePointerSized(unsigned char* target, uintptr_t value) {
    if (target == nullptr) {
        return;
    }
    if constexpr (sizeof(uintptr_t) == 8) {
        const std::uint64_t raw = static_cast<std::uint64_t>(value);
        std::memcpy(target, &raw, sizeof(raw));
    } else {
        const std::uint32_t raw = static_cast<std::uint32_t>(value);
        std::memcpy(target, &raw, sizeof(raw));
    }
}

struct RetainedGnustlCowString {
    std::unique_ptr<unsigned char[]> object;
    std::unique_ptr<unsigned char[]> block;
};

const void* RetainGnustlCowString(std::unique_ptr<unsigned char[]> object,
                                  std::unique_ptr<unsigned char[]> block) {
    static std::mutex mutex;
    static std::vector<RetainedGnustlCowString> retained;
    const void* argument = object.get();
    std::lock_guard<std::mutex> _lk(mutex);
    retained.push_back(RetainedGnustlCowString{std::move(object), std::move(block)});
    return argument;
}

}  // namespace

bool DecodeTextArgument(const void* arg, TextArgumentLayout layout, std::string* out) {
    return DecodeTextArgumentWithLayout(arg, layout, out, nullptr);
}

bool DecodeTextArgumentWithLayout(const void* arg,
                                  TextArgumentLayout layout,
                                  std::string* out,
                                  TextArgumentLayout* decoded_layout) {
    if (arg == nullptr || out == nullptr) {
        return false;
    }

    const uintptr_t address = reinterpret_cast<uintptr_t>(arg);
    if (layout == TextArgumentLayout::kCString) {
        if (TryDecodeByLayout(address, layout, out)) {
            if (decoded_layout != nullptr) {
                *decoded_layout = layout;
            }
            return true;
        }
        return false;
    }
    return DecodeStdStringWithFallback(address, layout, out, decoded_layout);
}

bool DecodeKnownLibcxxNdkTextArgument(const void* arg, std::string* out) {
    if (arg == nullptr || out == nullptr) {
        return false;
    }
    return TryDecodeKnownLibcxxNdkStringFast(reinterpret_cast<uintptr_t>(arg), out);
}

bool TextArgumentReplacement::Build(TextArgumentLayout layout, const std::string& text) {
    argument_ = nullptr;
    c_string_.clear();
    object_.clear();
    data_.clear();

    switch (layout) {
        case TextArgumentLayout::kCString:
            c_string_ = text;
            argument_ = c_string_.c_str();
            return true;
        case TextArgumentLayout::kLibcxxNdk: {
            const size_t object_size = sizeof(uintptr_t) == 8 ? 24 : 12;
            const size_t max_short = sizeof(uintptr_t) == 8 ? 22 : 10;
            object_.assign(object_size, 0);

            if (text.size() <= max_short) {
                object_[0] = static_cast<unsigned char>(text.size() << 1U);
                if (!text.empty()) {
                    std::memcpy(object_.data() + 1, text.data(), text.size());
                }
                argument_ = object_.data();
                return true;
            }

            data_.assign(text.begin(), text.end());
            data_.push_back('\0');
            WritePointerSized(object_.data(), static_cast<uintptr_t>((text.size() + 1U) | 1U));
            WritePointerSized(object_.data() + sizeof(uintptr_t), static_cast<uintptr_t>(text.size()));
            WritePointerSized(
                object_.data() + sizeof(uintptr_t) * 2,
                reinterpret_cast<uintptr_t>(data_.data()));
            argument_ = object_.data();
            return true;
        }
        case TextArgumentLayout::kGnustlCow: {
            const size_t header_size = sizeof(uintptr_t) * 3;
            const size_t block_size = header_size + text.size() + 1;
            auto block = std::make_unique<unsigned char[]>(block_size);
            std::memset(block.get(), 0, block_size);

            WritePointerSized(block.get(), static_cast<uintptr_t>(text.size()));
            WritePointerSized(block.get() + sizeof(uintptr_t), static_cast<uintptr_t>(text.size()));
            WritePointerSized(block.get() + sizeof(uintptr_t) * 2, 0);

            unsigned char* data = block.get() + header_size;
            if (!text.empty()) {
                std::memcpy(data, text.data(), text.size());
            }

            auto object = std::make_unique<unsigned char[]>(sizeof(uintptr_t));
            std::memset(object.get(), 0, sizeof(uintptr_t));
            WritePointerSized(object.get(), reinterpret_cast<uintptr_t>(data));
            argument_ = RetainGnustlCowString(std::move(object), std::move(block));
            return true;
        }
        case TextArgumentLayout::kAutoStdString:
        case TextArgumentLayout::kLibcxxAlternate:
            return false;
    }
    return false;
}

}  // namespace cocos_runtime::internal
