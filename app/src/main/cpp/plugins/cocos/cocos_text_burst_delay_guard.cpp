#include "cocos_text_burst_delay_guard.h"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cocos_runtime::internal {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kBurstWindow = std::chrono::milliseconds(50);
constexpr int kBurstMinEvents = 3;
constexpr int kGlobalBurstMinEvents = 2;
constexpr auto kGlobalBurstMinHold = std::chrono::milliseconds(1000);
constexpr auto kDynamicTemplateWindow = std::chrono::milliseconds(1500);
constexpr auto kDynamicTemplateMinHold = std::chrono::milliseconds(1500);
constexpr int kDynamicTemplateMinEvents = 2;
constexpr size_t kMaxDynamicTemplateWindows = 512;

size_t MixHash(size_t seed, size_t value) {
    seed ^= value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
}

struct BurstKey {
    uintptr_t hook_entry = 0;
    int text_arg_index = 0;

    bool operator==(const BurstKey& other) const {
        return hook_entry == other.hook_entry &&
               text_arg_index == other.text_arg_index;
    }
};

struct BurstKeyHash {
    size_t operator()(const BurstKey& key) const {
        size_t result = std::hash<uintptr_t>{}(key.hook_entry);
        return MixHash(result, std::hash<int>{}(key.text_arg_index));
    }
};

struct BurstWindow {
    Clock::time_point window_start_at{};
    Clock::time_point burst_until{};
    int event_count = 0;
};

struct GlobalBurstWindow {
    Clock::time_point window_start_at{};
    Clock::time_point burst_until{};
    int event_count = 0;
};

struct DynamicTemplateKey {
    uintptr_t hook_entry = 0;
    int text_arg_index = 0;
    std::string normalized_text;

    bool operator==(const DynamicTemplateKey& other) const {
        return hook_entry == other.hook_entry &&
               text_arg_index == other.text_arg_index &&
               normalized_text == other.normalized_text;
    }
};

struct DynamicTemplateKeyHash {
    size_t operator()(const DynamicTemplateKey& key) const {
        size_t result = std::hash<uintptr_t>{}(key.hook_entry);
        result = MixHash(result, std::hash<int>{}(key.text_arg_index));
        return MixHash(result, std::hash<std::string>{}(key.normalized_text));
    }
};

struct DynamicTemplateWindow {
    Clock::time_point last_seen_at{};
    Clock::time_point burst_until{};
    int event_count = 0;
};

std::mutex g_mutex;
GlobalBurstWindow g_global_window;
std::unordered_map<BurstKey, BurstWindow, BurstKeyHash> g_windows;
std::unordered_map<DynamicTemplateKey, DynamicTemplateWindow, DynamicTemplateKeyHash> g_template_windows;
Clock::duration g_injected_delay{};

std::string NormalizeDynamicTemplate(const std::string& text, bool* has_placeholder) {
    if (has_placeholder != nullptr) {
        *has_placeholder = false;
    }
    std::string normalized;
    normalized.reserve(text.size());
    bool previous_space = false;
    bool previous_placeholder = false;
    for (size_t index = 0; index < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (std::isdigit(ch) != 0) {
            if (!previous_placeholder) {
                normalized.push_back('#');
                previous_placeholder = true;
            }
            previous_space = false;
            if (has_placeholder != nullptr) {
                *has_placeholder = true;
            }
            while (index < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[index])) != 0) {
                ++index;
            }
            continue;
        }
        previous_placeholder = false;
        if (std::isspace(ch) != 0) {
            if (!previous_space && !normalized.empty()) {
                normalized.push_back(' ');
                previous_space = true;
            }
            ++index;
            continue;
        }
        previous_space = false;
        normalized.push_back(static_cast<char>(ch));
        ++index;
    }
    if (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

bool UpdateBurstWindowLocked(BurstWindow* window,
                             Clock::time_point logical_now,
                             std::chrono::milliseconds burst_hold) {
    if (window == nullptr) {
        return false;
    }
    const bool has_window = window->window_start_at != Clock::time_point{};
    if (!has_window || logical_now - window->window_start_at > kBurstWindow) {
        window->window_start_at = logical_now;
        window->event_count = 0;
    }

    ++window->event_count;
    if (window->event_count >= kBurstMinEvents) {
        window->burst_until = logical_now + burst_hold;
        return true;
    }
    return window->burst_until != Clock::time_point{} && logical_now <= window->burst_until;
}

bool UpdateGlobalBurstWindowLocked(Clock::time_point logical_now,
                                   std::chrono::milliseconds configured_hold) {
    const auto burst_hold = configured_hold > kGlobalBurstMinHold
                                ? configured_hold
                                : kGlobalBurstMinHold;
    const bool has_window = g_global_window.window_start_at != Clock::time_point{};
    if (!has_window || logical_now - g_global_window.window_start_at > kBurstWindow) {
        g_global_window.window_start_at = logical_now;
        g_global_window.event_count = 0;
    }

    ++g_global_window.event_count;
    if (g_global_window.event_count >= kGlobalBurstMinEvents) {
        g_global_window.burst_until = logical_now + burst_hold;
        return true;
    }
    const bool is_active = g_global_window.burst_until != Clock::time_point{} &&
                           logical_now <= g_global_window.burst_until;
    if (is_active) {
        g_global_window.burst_until = logical_now + burst_hold;
    }
    return is_active;
}

bool UpdateDynamicTemplateWindowLocked(DynamicTemplateWindow* window,
                                       Clock::time_point logical_now,
                                       std::chrono::milliseconds burst_hold) {
    if (window == nullptr) {
        return false;
    }
    const bool has_window = window->last_seen_at != Clock::time_point{};
    if (!has_window || logical_now - window->last_seen_at > kDynamicTemplateWindow) {
        window->event_count = 0;
        window->burst_until = Clock::time_point{};
    }

    window->last_seen_at = logical_now;
    ++window->event_count;
    if (window->event_count >= kDynamicTemplateMinEvents) {
        window->burst_until = logical_now + burst_hold;
        return true;
    }
    return window->burst_until != Clock::time_point{} && logical_now <= window->burst_until;
}

bool ShouldSkipForDynamicTemplateLocked(const std::string& source_text,
                                        uintptr_t hook_entry,
                                        int text_arg_index,
                                        Clock::time_point logical_now,
                                        std::chrono::milliseconds configured_hold) {
    bool has_placeholder = false;
    const std::string normalized = NormalizeDynamicTemplate(source_text, &has_placeholder);
    if (!has_placeholder || normalized.empty()) {
        return false;
    }
    if (g_template_windows.size() > kMaxDynamicTemplateWindows) {
        g_template_windows.clear();
    }
    const auto dynamic_hold = configured_hold > kDynamicTemplateMinHold
                                  ? configured_hold
                                  : kDynamicTemplateMinHold;
    DynamicTemplateWindow& window =
        g_template_windows[DynamicTemplateKey{hook_entry, text_arg_index, normalized}];
    return UpdateDynamicTemplateWindowLocked(&window, logical_now, dynamic_hold);
}

}  // namespace

TextBurstDelayDecision ResolveTextBurstDelay(const RuntimeConfig& config,
                                             uintptr_t hook_entry,
                                             int text_arg_index,
                                             const std::string& source_text) {
    TextBurstDelayDecision decision;
    if (!config.text_replacement_delay_enabled || config.text_replacement_delay_ms <= 0) {
        return decision;
    }
    decision.should_sleep = true;
    if (!config.text_burst_delay_guard_enabled || hook_entry == 0) {
        return decision;
    }

    std::lock_guard<std::mutex> _lk(g_mutex);
    const Clock::time_point logical_now = Clock::now() - g_injected_delay;
    const auto burst_hold = std::chrono::milliseconds(config.text_burst_hold_ms);
    const bool skip_for_global_burst = UpdateGlobalBurstWindowLocked(logical_now, burst_hold);
    BurstWindow& window = g_windows[BurstKey{hook_entry, text_arg_index}];
    const bool skip_for_same_hook_burst = UpdateBurstWindowLocked(&window, logical_now, burst_hold);
    const bool skip_for_dynamic_template = ShouldSkipForDynamicTemplateLocked(
        source_text,
        hook_entry,
        text_arg_index,
        logical_now,
        burst_hold);
    if (skip_for_global_burst || skip_for_same_hook_burst || skip_for_dynamic_template) {
        decision.should_sleep = false;
        decision.skipped_for_burst = true;
        if (skip_for_global_burst) {
            decision.skip_reason = "global-burst";
        } else if (skip_for_same_hook_burst) {
            decision.skip_reason = "same-hook-burst";
        } else {
            decision.skip_reason = "dynamic-template";
        }
    }
    return decision;
}

void RecordTextBurstInjectedDelay(const RuntimeConfig& config) {
    if (!config.text_burst_delay_guard_enabled ||
        !config.text_replacement_delay_enabled ||
        config.text_replacement_delay_ms <= 0) {
        return;
    }
    std::lock_guard<std::mutex> _lk(g_mutex);
    g_injected_delay += std::chrono::milliseconds(config.text_replacement_delay_ms);
}

}  // namespace cocos_runtime::internal
