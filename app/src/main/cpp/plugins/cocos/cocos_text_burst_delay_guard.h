#ifndef IL2FUSION_PLUGINS_COCOS_TEXT_BURST_DELAY_GUARD_H
#define IL2FUSION_PLUGINS_COCOS_TEXT_BURST_DELAY_GUARD_H

#include "cocos_runtime_state.h"

#include <cstdint>
#include <string>

namespace cocos_runtime::internal {

struct TextBurstDelayDecision {
    bool should_sleep = false;
    bool skipped_for_burst = false;
    const char* skip_reason = "none";

    const char* ModeName() const {
        if (skipped_for_burst) {
            return "skip";
        }
        return should_sleep ? "sleep" : "off";
    }

    const char* ReasonName() const {
        return skip_reason;
    }
};

TextBurstDelayDecision ResolveTextBurstDelay(const RuntimeConfig& config,
                                             uintptr_t hook_entry,
                                             int text_arg_index,
                                             const std::string& source_text);

void RecordTextBurstInjectedDelay(const RuntimeConfig& config);

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_TEXT_BURST_DELAY_GUARD_H
