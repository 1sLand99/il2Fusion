#include "cocos_text_symbols.h"

#include <string_view>
#include <unordered_set>
#include <vector>

namespace cocos_runtime::internal {
namespace {

struct ParsedMangledSymbol {
    std::string owner_path;
    std::string method_name;
    std::string params;
    bool is_thunk = false;
};

bool SupportsRuntimeReplacement(TextArgumentLayout layout) {
    return layout == TextArgumentLayout::kCString ||
           layout == TextArgumentLayout::kLibcxxNdk ||
           layout == TextArgumentLayout::kGnustlCow;
}

const std::unordered_set<std::string_view> kCocosSetterMethods = {
    "setString",
    "setStringLazy",
    "setText",
    "setTitleText",
    "setPlaceHolder",
    "setStringValue",
};

const std::unordered_set<std::string_view> kCocosCreationMethods = {
    "createWithTTF",
    "createWithSystemFont",
    "createWithBMFont",
    "initWithString",
    "init",
    "create",
};

const std::unordered_set<std::string_view> kCocosFontFileMethods = {
    "setFntFile",
    "setFontFile",
};

const std::unordered_set<std::string_view> kTextSetterOwners = {
    "cocos2d::Label",
    "cocos2d::LabelLayout",
    "cocos2d::LabelTTF",
    "cocos2d::LabelBMFont",
    "cocos2d::LabelAtlas",
    "cocos2d::CCLabelTTF",
    "cocos2d::CCLabelBMFont",
    "cocos2d::CCLabelAtlas",
    "cocos2d::TextFieldTTF",
    "cocos2d::MenuItemLabel",
    "cocos2d::ui::Label",
    "cocos2d::ui::Text",
    "cocos2d::ui::TextBMFont",
    "cocos2d::ui::TextAtlas",
    "cocos2d::ui::LabelAtlas",
    "cocos2d::ui::LabelBMFont",
    "cocos2d::ui::TextField",
    "cocos2d::ui::Button",
    "cocos2d::ui::TabHeader",
    "cocos2d::ui::RichElementText",
    "cocos2d::ui::RichText",
};

const std::unordered_set<std::string_view> kBmFontOwners = {
    "cocos2d::LabelBMFont",
    "cocos2d::CCLabelBMFont",
    "cocos2d::ui::TextBMFont",
    "cocos2d::ui::LabelBMFont",
};

const std::unordered_set<std::string_view> kTextCreationOwners = {
    "cocos2d::Label",
    "cocos2d::LabelTTF",
    "cocos2d::LabelBMFont",
    "cocos2d::CCLabelBMFont",
    "cocos2d::LabelAtlas",
    "cocos2d::MenuItemFont",
    "cocos2d::MenuItemAtlasFont",
    "cocos2d::ui::Text",
    "cocos2d::ui::TextBMFont",
    "cocos2d::ui::TextAtlas",
    "cocos2d::ui::TextField",
    "cocos2d::ui::RichElementText",
};

struct NativeFontSetterPolicy {
    const char* owner_path;
    const char* method_name;
    FontResourceKind font_kind;
};

// Font setters that can be resolved from native Cocos symbols and invoked as siblings.
constexpr NativeFontSetterPolicy kNativeFontSetterPolicies[] = {
    {"cocos2d::Label", "setSystemFontName", FontResourceKind::kTtf},
    {"cocos2d::LabelTTF", "setFontName", FontResourceKind::kTtf},
    {"cocos2d::CCLabelTTF", "setFontName", FontResourceKind::kTtf},
    {"cocos2d::TextFieldTTF", "setFontName", FontResourceKind::kTtf},
    {"cocos2d::ui::Text", "setFontName", FontResourceKind::kTtf},
    {"cocos2d::ui::TextField", "setFontName", FontResourceKind::kTtf},
    {"cocos2d::ui::Button", "setTitleFontName", FontResourceKind::kTtf},
    {"cocos2d::ui::TabHeader", "setTitleFontName", FontResourceKind::kTtf},
    {"cocos2d::ui::RichText", "setFontFace", FontResourceKind::kTtf},
    {"cocos2d::LabelBMFont", "setFntFile", FontResourceKind::kFnt},
    {"cocos2d::CCLabelBMFont", "setFntFile", FontResourceKind::kFnt},
    {"cocos2d::ui::TextBMFont", "setFntFile", FontResourceKind::kFnt},
    {"cocos2d::ui::LabelBMFont", "setFntFile", FontResourceKind::kFnt},
};

bool IsDigit(char value) {
    return value >= '0' && value <= '9';
}

bool ParseDecimalLength(const std::string& value, size_t start, size_t end, size_t* out) {
    if (out == nullptr || start >= end) {
        return false;
    }

    size_t result = 0;
    for (size_t i = start; i < end; ++i) {
        if (!IsDigit(value[i])) {
            return false;
        }
        const size_t digit = static_cast<size_t>(value[i] - '0');
        if (result > (static_cast<size_t>(-1) - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }

    *out = result;
    return result > 0;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

int FindNestedNameIndex(const std::string& symbol) {
    if (StartsWith(symbol, "_ZN") || StartsWith(symbol, "_ZNK")) {
        return 2;
    }
    if (!StartsWith(symbol, "_Z")) {
        return -1;
    }

    const size_t thunk_nested_index = symbol.find("_N", 2);
    return thunk_nested_index == std::string::npos ? -1 : static_cast<int>(thunk_nested_index + 1);
}

std::optional<ParsedMangledSymbol> ParseMangledNestedSymbol(const std::string& symbol) {
    const int nested_index = FindNestedNameIndex(symbol);
    if (nested_index < 0) {
        return std::nullopt;
    }

    size_t index = static_cast<size_t>(nested_index + 1);
    if (index < symbol.size() && symbol[index] == 'K') {
        ++index;
    }

    std::vector<std::string> components;
    while (index < symbol.size() && symbol[index] != 'E') {
        const size_t length_start = index;
        while (index < symbol.size() && IsDigit(symbol[index])) {
            ++index;
        }
        if (index == length_start) {
            return std::nullopt;
        }

        size_t length = 0;
        if (!ParseDecimalLength(symbol, length_start, index, &length)) {
            return std::nullopt;
        }
        if (length == 0 || index + length > symbol.size()) {
            return std::nullopt;
        }

        components.push_back(symbol.substr(index, length));
        index += length;
    }

    if (components.size() < 2 || index >= symbol.size() || symbol[index] != 'E') {
        return std::nullopt;
    }

    std::string owner_path;
    for (size_t i = 0; i + 1 < components.size(); ++i) {
        if (!owner_path.empty()) {
            owner_path += "::";
        }
        owner_path += components[i];
    }

    return ParsedMangledSymbol{
        owner_path,
        components.back(),
        symbol.substr(index + 1),
        nested_index > 2,
    };
}

bool IsCocosTextSetter(const std::string& owner_path, const std::string& method_name) {
    if (kTextSetterOwners.find(owner_path) != kTextSetterOwners.end()) {
        return kCocosSetterMethods.find(method_name) != kCocosSetterMethods.end();
    }
    return false;
}

bool FirstParameterIsTtfConfig(const std::string& params) {
    return StartsWith(params, "RKNS_10_ttfConfigE") ||
           StartsWith(params, "RKN7cocos2d10_ttfConfigE");
}

bool IsAutoTextSetter(const ParsedMangledSymbol& parsed) {
    return IsCocosTextSetter(parsed.owner_path, parsed.method_name);
}

bool IsAutoTextCreationSymbol(const ParsedMangledSymbol& parsed) {
    if (kCocosCreationMethods.find(parsed.method_name) == kCocosCreationMethods.end()) {
        return false;
    }
    if (parsed.method_name == "init") {
        return parsed.owner_path == "cocos2d::LabelLayout";
    }
    if (parsed.method_name == "initWithString" || parsed.method_name == "create") {
        return kTextCreationOwners.find(parsed.owner_path) != kTextCreationOwners.end();
    }
    return parsed.owner_path == "cocos2d::Label";
}

bool IsAutoFontFileSymbol(const ParsedMangledSymbol& parsed) {
    return kBmFontOwners.find(parsed.owner_path) != kBmFontOwners.end() &&
           kCocosFontFileMethods.find(parsed.method_name) != kCocosFontFileMethods.end();
}

std::optional<TextSymbolCategory> ClassifyAutoTextCategory(const ParsedMangledSymbol& parsed) {
    if (IsAutoTextSetter(parsed)) {
        return TextSymbolCategory::kSetter;
    }
    if (IsAutoTextCreationSymbol(parsed)) {
        return TextSymbolCategory::kCreation;
    }
    if (IsAutoFontFileSymbol(parsed)) {
        return TextSymbolCategory::kFontFile;
    }
    return std::nullopt;
}

std::optional<TextArgumentLayout> InferTextLayout(const std::string& params) {
    if (params.find("St6__ndk1") != std::string::npos) {
        return TextArgumentLayout::kLibcxxNdk;
    }
    if (params.find("St3__1") != std::string::npos) {
        return TextArgumentLayout::kLibcxxAlternate;
    }
    if (params.find("Ss") != std::string::npos) {
        return TextArgumentLayout::kGnustlCow;
    }
    if (params.find("PKc") != std::string::npos) {
        return TextArgumentLayout::kCString;
    }
    return std::nullopt;
}

bool IsAutoTextReplaceable(const ParsedMangledSymbol& parsed, TextSymbolCategory category) {
    (void) parsed;
    if (category == TextSymbolCategory::kSetter ||
        category == TextSymbolCategory::kCreation) {
        return true;
    }
    return false;
}

int InferAutoTextArgIndex(const ParsedMangledSymbol& parsed, TextSymbolCategory category) {
    if (category == TextSymbolCategory::kFontFile) {
        return 1;
    }
    if (category != TextSymbolCategory::kCreation) {
        return 1;
    }
    if (parsed.method_name == "createWithBMFont") {
        return 1;
    }
    if (parsed.method_name == "create" &&
        parsed.owner_path == "cocos2d::ui::RichElementText") {
        return 3;
    }
    if (parsed.method_name == "createWithTTF" &&
        FirstParameterIsTtfConfig(parsed.params)) {
        return 1;
    }
    if (parsed.method_name == "initWithString" || parsed.method_name == "init") {
        return 1;
    }
    return 0;
}

bool InferAutoTextHasThis(const ParsedMangledSymbol& parsed, TextSymbolCategory category) {
    return category != TextSymbolCategory::kCreation ||
           parsed.method_name == "initWithString" ||
           parsed.method_name == "init";
}

struct NativeFontArgSpec {
    int arg_index = -1;
    TextArgumentLayout layout = TextArgumentLayout::kAutoStdString;
    FontResourceKind kind = FontResourceKind::kNone;
};

NativeFontArgSpec InferAutoTextFontSymbol(const ParsedMangledSymbol& parsed,
                                          TextSymbolCategory category,
                                          TextArgumentLayout layout) {
    if (category == TextSymbolCategory::kFontFile) {
        return {1, layout, FontResourceKind::kFnt};
    }
    if (category != TextSymbolCategory::kCreation) {
        return {};
    }
    if (parsed.method_name == "createWithTTF") {
        if (FirstParameterIsTtfConfig(parsed.params)) {
            return {};
        }
        return {1, layout, FontResourceKind::kTtf};
    }
    if (parsed.method_name == "createWithSystemFont") {
        return {1, layout, FontResourceKind::kTtf};
    }
    if (parsed.method_name == "createWithBMFont") {
        return {0, layout, FontResourceKind::kFnt};
    }
    if (parsed.method_name == "initWithString" &&
        parsed.owner_path == "cocos2d::LabelTTF") {
        return {2, layout, FontResourceKind::kTtf};
    }
    if (parsed.method_name == "initWithString" &&
        (parsed.owner_path == "cocos2d::LabelBMFont" ||
         parsed.owner_path == "cocos2d::CCLabelBMFont")) {
        return {2, layout, FontResourceKind::kFnt};
    }
    if (parsed.method_name == "create") {
        if (parsed.owner_path == "cocos2d::ui::Text" ||
            parsed.owner_path == "cocos2d::ui::TextField") {
            return {1, layout, FontResourceKind::kTtf};
        }
        if (parsed.owner_path == "cocos2d::ui::TextBMFont" ||
            parsed.owner_path == "cocos2d::ui::LabelBMFont") {
            return {1, layout, FontResourceKind::kFnt};
        }
        if (parsed.owner_path == "cocos2d::ui::RichElementText") {
            return {4, layout, FontResourceKind::kTtf};
        }
    }
    return {};
}

bool HasLuaBindingCounterpart(const ParsedMangledSymbol& parsed) {
    const std::string& owner = parsed.owner_path;
    const std::string& method = parsed.method_name;

    if (owner == "cocos2d::Label") {
        return method == "createWithTTF" ||
               method == "initWithTTF" ||
               method == "createWithBMFont" ||
               method == "initWithBMFont" ||
               method == "setString" ||
               method == "createWithSystemFont";
    }
    if (owner == "cocos2d::ui::Text") {
        return method == "create" || method == "setString";
    }
    if (owner == "cocos2d::ui::TextBMFont") {
        return method == "create" ||
               method == "setString" ||
               method == "setFntFile";
    }
    if (owner == "cocos2d::LabelBMFont") {
        return method == "setFntFile";
    }
    if (owner == "cocos2d::ui::TextField") {
        return method == "create" ||
               method == "setString" ||
               method == "setPlaceHolder";
    }
    if (owner == "cocos2d::ui::Button") {
        return method == "setTitleText";
    }
    if (owner == "cocos2d::ui::RichElementText") {
        return method == "create" || method == "init";
    }
    if (owner == "cocos2d::ui::RichText") {
        return method == "setString";
    }
    if (owner == "cocos2d::ui::TabHeader") {
        return method == "setTitleText";
    }
    return false;
}

bool FirstParameterIsStdStringByValue(const ParsedMangledSymbol& parsed) {
    return StartsWith(parsed.params, "Ss") ||
           StartsWith(parsed.params, "NSt6__ndk1") ||
           StartsWith(parsed.params, "NSt3__1");
}

bool IsNativeGenericHookSafe(const ParsedMangledSymbol& parsed,
                             TextSymbolCategory category) {
    // The native text wrapper has a fixed integer-register signature. Cocos
    // creation APIs vary widely and often carry floats or stack arguments, so
    // they must be handled through Lua bindings or dedicated typed wrappers.
    if (category == TextSymbolCategory::kCreation) {
        return false;
    }
    // By-value std::string setters are not a stable native replacement point:
    // some Cocos builds copy into a temporary before the call, then the callee
    // only marks layout dirty. Route those through Lua bindings instead.
    if (category == TextSymbolCategory::kSetter && FirstParameterIsStdStringByValue(parsed)) {
        return false;
    }
    return true;
}

std::string FormatOwnerLabel(const std::string& owner_path) {
    constexpr std::string_view kCocosPrefix = "cocos2d::";
    if (StartsWith(owner_path, kCocosPrefix)) {
        return owner_path.substr(kCocosPrefix.size());
    }
    return owner_path;
}

std::optional<TextSymbolSpec> ClassifyAutoTextSymbol(const std::string& symbol_name) {
    const auto parsed = ParseMangledNestedSymbol(symbol_name);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    if (parsed->is_thunk) {
        return std::nullopt;
    }
    const auto category = ClassifyAutoTextCategory(*parsed);
    if (!category.has_value()) {
        return std::nullopt;
    }
    const auto layout = InferTextLayout(parsed->params);
    if (!layout.has_value()) {
        return std::nullopt;
    }

    const int arg_index = InferAutoTextArgIndex(*parsed, *category);
    const bool has_this = InferAutoTextHasThis(*parsed, *category);
    const NativeFontArgSpec font_spec = InferAutoTextFontSymbol(*parsed, *category, *layout);
    const std::string label_prefix = parsed->is_thunk ? "auto thunk " : "auto ";
    return TextSymbolSpec{
        label_prefix + FormatOwnerLabel(parsed->owner_path) + "::" +
            parsed->method_name + "(" + TextArgumentLayoutName(*layout) + ")",
        symbol_name,
        parsed->owner_path,
        parsed->method_name,
        *layout,
        *category,
        TextSymbolFamily::kCocos,
        arg_index,
        has_this,
        SupportsRuntimeReplacement(*layout) && IsAutoTextReplaceable(*parsed, *category),
        font_spec.arg_index,
        font_spec.layout,
        font_spec.kind,
        HasLuaBindingCounterpart(*parsed),
        IsNativeGenericHookSafe(*parsed, *category),
    };
}

std::optional<NativeFontSetterSpec> ClassifyNativeFontSetterSymbol(const std::string& symbol_name) {
    const auto parsed = ParseMangledNestedSymbol(symbol_name);
    if (!parsed.has_value() || parsed->is_thunk) {
        return std::nullopt;
    }

    FontResourceKind font_kind = FontResourceKind::kNone;
    for (const NativeFontSetterPolicy& policy : kNativeFontSetterPolicies) {
        if (parsed->owner_path == policy.owner_path &&
            parsed->method_name == policy.method_name) {
            font_kind = policy.font_kind;
            break;
        }
    }
    if (font_kind == FontResourceKind::kNone) {
        return std::nullopt;
    }

    const auto layout = InferTextLayout(parsed->params);
    if (!layout.has_value()) {
        return std::nullopt;
    }
    return NativeFontSetterSpec{
        "auto " + FormatOwnerLabel(parsed->owner_path) + "::" +
            parsed->method_name + "(" + TextArgumentLayoutName(*layout) + ")",
        symbol_name,
        parsed->owner_path,
        parsed->method_name,
        *layout,
        font_kind,
    };
}

}  // namespace

const char* TextArgumentLayoutName(TextArgumentLayout layout) {
    switch (layout) {
        case TextArgumentLayout::kCString:
            return "c_char";
        case TextArgumentLayout::kAutoStdString:
            return "auto_std_string";
        case TextArgumentLayout::kLibcxxNdk:
            return "libcxx_ndk";
        case TextArgumentLayout::kLibcxxAlternate:
            return "libcxx_alternate";
        case TextArgumentLayout::kGnustlCow:
            return "gnustl_cow";
    }
    return "unknown";
}

const char* TextSymbolCategoryName(TextSymbolCategory category) {
    switch (category) {
        case TextSymbolCategory::kSetter:
            return "setter";
        case TextSymbolCategory::kCreation:
            return "creation";
        case TextSymbolCategory::kFontFile:
            return "font_file";
        case TextSymbolCategory::kRender:
            return "render";
    }
    return "unknown";
}

const char* TextSymbolFamilyName(TextSymbolFamily family) {
    switch (family) {
        case TextSymbolFamily::kCocos:
            return "cocos";
    }
    return "unknown";
}

const char* FontResourceKindName(FontResourceKind kind) {
    switch (kind) {
        case FontResourceKind::kNone:
            return "none";
        case FontResourceKind::kTtf:
            return "ttf";
        case FontResourceKind::kFnt:
            return "fnt";
    }
    return "unknown";
}

std::optional<TextSymbolSpec> ResolveTextSymbolSpec(const std::string& symbol_name) {
    return ClassifyAutoTextSymbol(symbol_name);
}

std::optional<NativeFontSetterSpec> ResolveNativeFontSetterSpec(const std::string& symbol_name) {
    return ClassifyNativeFontSetterSymbol(symbol_name);
}

}  // namespace cocos_runtime::internal
