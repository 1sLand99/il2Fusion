#ifndef IL2FUSION_ELF_SYMBOLS_H
#define IL2FUSION_ELF_SYMBOLS_H

#include <cstdint>
#include <string>
#include <vector>

#include "utils.h"

namespace elf_symbols {

struct SymbolInfo {
    std::string name;
    uintptr_t address = 0;
    size_t size = 0;
};

std::vector<SymbolInfo> EnumerateFunctionSymbols(const hookutils::ModuleInfo& module_info);
uintptr_t FindExportAddress(const hookutils::ModuleInfo& module_info, const char* symbol_name);

}  // namespace elf_symbols

#endif  // IL2FUSION_ELF_SYMBOLS_H
