#ifndef IL2FUSION_PLUGINS_COCOS_TEXT_SYMBOLS_H
#define IL2FUSION_PLUGINS_COCOS_TEXT_SYMBOLS_H

#include <optional>
#include <string>

namespace cocos_runtime::internal {

enum class TextArgumentLayout {
    kCString,
    kAutoStdString,
    kLibcxxNdk,
    kLibcxxAlternate,
    kGnustlCow,
};

enum class TextSymbolCategory {
    kSetter,
    kCreation,
    kFontFile,
    kRender,
};

enum class TextSymbolFamily {
    kCocos,
};

enum class FontResourceKind {
    kNone,
    kTtf,
    kFnt,
};

struct TextSymbolSpec {
    std::string label;
    std::string symbol;
    std::string owner_path;
    std::string method_name;
    TextArgumentLayout layout = TextArgumentLayout::kAutoStdString;
    TextSymbolCategory category = TextSymbolCategory::kSetter;
    TextSymbolFamily family = TextSymbolFamily::kCocos;
    int arg_index = 1;
    bool has_this = true;
    bool replacement_supported = false;
    int font_arg_index = -1;
    TextArgumentLayout font_layout = TextArgumentLayout::kAutoStdString;
    FontResourceKind font_kind = FontResourceKind::kNone;
    bool lua_binding_overlap = false;
    bool native_hook_safe = true;
};

struct NativeFontSetterSpec {
    std::string label;
    std::string symbol;
    std::string owner_path;
    std::string method_name;
    TextArgumentLayout layout = TextArgumentLayout::kAutoStdString;
    FontResourceKind font_kind = FontResourceKind::kNone;
};

std::optional<TextSymbolSpec> ResolveTextSymbolSpec(const std::string& symbol_name);
std::optional<NativeFontSetterSpec> ResolveNativeFontSetterSpec(const std::string& symbol_name);
const char* TextArgumentLayoutName(TextArgumentLayout layout);
const char* TextSymbolCategoryName(TextSymbolCategory category);
const char* TextSymbolFamilyName(TextSymbolFamily family);
const char* FontResourceKindName(FontResourceKind kind);

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_TEXT_SYMBOLS_H
