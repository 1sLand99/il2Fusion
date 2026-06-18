#ifndef IL2FUSION_PLUGINS_COCOS_TEXT_DECODER_H
#define IL2FUSION_PLUGINS_COCOS_TEXT_DECODER_H

#include "cocos_text_symbols.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cocos_runtime::internal {

bool DecodeTextArgument(const void* arg, TextArgumentLayout layout, std::string* out);
bool DecodeTextArgumentWithLayout(const void* arg,
                                  TextArgumentLayout layout,
                                  std::string* out,
                                  TextArgumentLayout* decoded_layout);
bool DecodeKnownLibcxxNdkTextArgument(const void* arg, std::string* out);

class TextArgumentReplacement {
public:
    bool Build(TextArgumentLayout layout, const std::string& text);
    const void* argument() const { return argument_; }

private:
    const void* argument_ = nullptr;
    std::string c_string_;
    std::vector<unsigned char> object_;
    std::vector<unsigned char> data_;
};

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_TEXT_DECODER_H
