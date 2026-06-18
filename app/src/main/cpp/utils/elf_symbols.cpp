#include "elf_symbols.h"

#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <elf.h>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace elf_symbols {
namespace {

using FilePtr = std::unique_ptr<FILE, decltype(&std::fclose)>;

FilePtr OpenFile(const std::string& path) {
    return FilePtr(std::fopen(path.c_str(), "rb"), &std::fclose);
}

std::mutex g_symbol_cache_mutex;
std::unordered_map<std::string, std::unordered_map<std::string, uintptr_t>> g_symbol_cache;

std::string CacheKey(const hookutils::ModuleInfo& module_info) {
    return module_info.path + "@" + std::to_string(module_info.load_bias);
}

template <typename ShdrT, typename SymT>
void CollectSymbolsFromSection(FILE* fp,
                               const std::vector<ShdrT>& sections,
                               const ShdrT& symbol_section,
                               uintptr_t module_base,
                               std::vector<SymbolInfo>* symbols) {
    if (symbols == nullptr || symbol_section.sh_link >= sections.size() ||
        symbol_section.sh_entsize != sizeof(SymT)) {
        return;
    }

    const ShdrT& string_section = sections[symbol_section.sh_link];
    std::vector<char> string_table(string_section.sh_size);
    std::fseek(fp, static_cast<long>(string_section.sh_offset), SEEK_SET);
    if (std::fread(string_table.data(), 1, string_table.size(), fp) != string_table.size()) {
        return;
    }

    const size_t count = symbol_section.sh_size / sizeof(SymT);
    std::vector<SymT> table(count);
    std::fseek(fp, static_cast<long>(symbol_section.sh_offset), SEEK_SET);
    if (std::fread(table.data(), sizeof(SymT), table.size(), fp) != table.size()) {
        return;
    }

    for (const auto& sym : table) {
        if (sym.st_name >= string_table.size() || sym.st_value == 0 ||
            (sym.st_info & 0x0f) != STT_FUNC) {
            continue;
        }
        const char* name = string_table.data() + sym.st_name;
        if (name == nullptr || *name == '\0') {
            continue;
        }
        symbols->push_back(SymbolInfo{
            std::string(name),
            module_base + static_cast<uintptr_t>(sym.st_value),
            static_cast<size_t>(sym.st_size),
        });
    }
}

template <typename EhdrT, typename ShdrT, typename SymT>
std::vector<SymbolInfo> EnumerateTypedSymbols(FILE* fp,
                                              const EhdrT& ehdr,
                                              uintptr_t module_base) {
    std::vector<SymbolInfo> symbols;
    if (ehdr.e_shentsize != sizeof(ShdrT) || ehdr.e_shnum == 0) {
        return symbols;
    }

    std::vector<ShdrT> sections(ehdr.e_shnum);
    std::fseek(fp, static_cast<long>(ehdr.e_shoff), SEEK_SET);
    if (std::fread(sections.data(), sizeof(ShdrT), sections.size(), fp) != sections.size()) {
        return symbols;
    }

    for (const auto& section : sections) {
        if (section.sh_type != SHT_DYNSYM && section.sh_type != SHT_SYMTAB) {
            continue;
        }
        CollectSymbolsFromSection<ShdrT, SymT>(fp, sections, section, module_base, &symbols);
    }

    return symbols;
}

}  // namespace

std::vector<SymbolInfo> EnumerateFunctionSymbols(const hookutils::ModuleInfo& module_info) {
    auto fp = OpenFile(module_info.path);
    if (fp == nullptr) {
        return {};
    }

    Elf64_Ehdr ehdr64{};
    if (std::fread(&ehdr64, 1, sizeof(ehdr64), fp.get()) != sizeof(ehdr64) ||
        std::memcmp(ehdr64.e_ident, ELFMAG, SELFMAG) != 0) {
        return {};
    }

    if (ehdr64.e_ident[EI_CLASS] == ELFCLASS64) {
        return EnumerateTypedSymbols<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(
            fp.get(), ehdr64, module_info.load_bias);
    }

    if (ehdr64.e_ident[EI_CLASS] != ELFCLASS32) {
        return {};
    }

    std::fseek(fp.get(), 0, SEEK_SET);
    Elf32_Ehdr ehdr32{};
    if (std::fread(&ehdr32, 1, sizeof(ehdr32), fp.get()) != sizeof(ehdr32)) {
        return {};
    }
    return EnumerateTypedSymbols<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(
        fp.get(), ehdr32, module_info.load_bias);
}

uintptr_t FindExportAddress(const hookutils::ModuleInfo& module_info, const char* symbol_name) {
    void* handle = dlopen(module_info.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (handle != nullptr) {
        void* sym = dlsym(handle, symbol_name);
        dlclose(handle);
        if (sym != nullptr) {
            return reinterpret_cast<uintptr_t>(sym);
        }
    }

    const std::string cache_key = CacheKey(module_info);
    {
        std::lock_guard<std::mutex> _lk(g_symbol_cache_mutex);
        auto cache_it = g_symbol_cache.find(cache_key);
        if (cache_it != g_symbol_cache.end()) {
            auto symbol_it = cache_it->second.find(symbol_name);
            return symbol_it != cache_it->second.end() ? symbol_it->second : 0;
        }
    }

    std::unordered_map<std::string, uintptr_t> symbols;
    for (const auto& symbol : EnumerateFunctionSymbols(module_info)) {
        symbols.emplace(symbol.name, symbol.address);
    }

    std::lock_guard<std::mutex> _lk(g_symbol_cache_mutex);
    auto [cache_it, _inserted] = g_symbol_cache.emplace(cache_key, std::move(symbols));
    auto symbol_it = cache_it->second.find(symbol_name);
    return symbol_it != cache_it->second.end() ? symbol_it->second : 0;
}

}  // namespace elf_symbols
