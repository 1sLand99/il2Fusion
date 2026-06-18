#ifndef IL2FUSION_SAFE_MEMORY_H
#define IL2FUSION_SAFE_MEMORY_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace safe_memory {

uintptr_t ReadableLimit(uintptr_t address, size_t max_size);
bool ReadCStringAt(uintptr_t address, size_t max_size, std::string* out);
bool ReadBytesAt(uintptr_t address, void* out, size_t size);

}  // namespace safe_memory

#endif  // IL2FUSION_SAFE_MEMORY_H
