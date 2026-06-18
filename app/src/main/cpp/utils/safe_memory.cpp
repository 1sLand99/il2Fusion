#include "safe_memory.h"

#include <cinttypes>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace safe_memory {
namespace {

struct ReadableRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
};

using Clock = std::chrono::steady_clock;
constexpr auto kReadableRangesMissRefreshInterval = std::chrono::seconds(1);

std::mutex g_ranges_mutex;
std::vector<ReadableRange> g_readable_ranges;
Clock::time_point g_last_ranges_refresh_attempt{};
bool g_ranges_loaded = false;

uintptr_t UntagAddressForMaps(uintptr_t address) {
#if defined(__aarch64__)
    return address & static_cast<uintptr_t>(0x00FFFFFFFFFFFFFFULL);
#else
    return address;
#endif
}

bool ShouldRefreshRangesLocked(Clock::time_point now) {
    return g_last_ranges_refresh_attempt.time_since_epoch().count() == 0 ||
           now - g_last_ranges_refresh_attempt >= kReadableRangesMissRefreshInterval;
}

bool LoadReadableRangesLocked(Clock::time_point now) {
    g_last_ranges_refresh_attempt = now;
    std::vector<ReadableRange> ranges;
    FILE* fp = std::fopen("/proc/self/maps", "r");
    if (fp == nullptr) {
        g_readable_ranges.clear();
        g_ranges_loaded = false;
        return false;
    }

    char line[1024];
    while (std::fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        char perms[5] = {};
        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, perms) != 3) {
            continue;
        }
        if (start >= end || perms[0] != 'r') {
            continue;
        }
        ranges.push_back({start, end});
    }
    std::fclose(fp);

    std::sort(ranges.begin(), ranges.end(), [](const ReadableRange& left, const ReadableRange& right) {
        return left.start < right.start;
    });
    g_readable_ranges = std::move(ranges);
    g_ranges_loaded = true;
    return true;
}

uintptr_t FindReadableLimitLocked(uintptr_t maps_address, size_t max_size) {
    const auto it = std::upper_bound(
        g_readable_ranges.begin(),
        g_readable_ranges.end(),
        maps_address,
        [](uintptr_t value, const ReadableRange& range) {
            return value < range.start;
        });
    if (it == g_readable_ranges.begin()) {
        return 0;
    }
    const ReadableRange& range = *(it - 1);
    if (maps_address < range.start || maps_address >= range.end) {
        return 0;
    }
    const uintptr_t available = range.end - maps_address;
    return available < max_size ? available : max_size;
}

}  // namespace

uintptr_t ReadableLimit(uintptr_t address, size_t max_size) {
    if (address == 0 || max_size == 0) {
        return 0;
    }
    const uintptr_t maps_address = UntagAddressForMaps(address);
    std::lock_guard<std::mutex> _lk(g_ranges_mutex);
    if (!g_ranges_loaded) {
        const auto now = Clock::now();
        if (!ShouldRefreshRangesLocked(now) || !LoadReadableRangesLocked(now)) {
            return 0;
        }
    }
    uintptr_t limit = FindReadableLimitLocked(maps_address, max_size);
    if (limit != 0) {
        return limit;
    }

    // New mappings can appear after the first cache fill. Refresh misses at a
    // low rate so invalid hot-path candidates do not repeatedly scan maps.
    const auto now = Clock::now();
    if (!ShouldRefreshRangesLocked(now) || !LoadReadableRangesLocked(now)) {
        return 0;
    }
    return FindReadableLimitLocked(maps_address, max_size);
}

bool ReadCStringAt(uintptr_t address, size_t max_size, std::string* out) {
    if (out == nullptr) {
        return false;
    }
    const uintptr_t limit = ReadableLimit(address, max_size);
    if (limit == 0) {
        return false;
    }
    const char* data = reinterpret_cast<const char*>(address);
    size_t len = 0;
    while (len < limit && data[len] != '\0') {
        ++len;
    }
    if (len == 0 || len >= limit) {
        return false;
    }
    out->assign(data, len);
    return true;
}

bool ReadBytesAt(uintptr_t address, void* out, size_t size) {
    if (out == nullptr || size == 0 || ReadableLimit(address, size) < size) {
        return false;
    }
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}

}  // namespace safe_memory
