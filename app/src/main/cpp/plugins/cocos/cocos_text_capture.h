#ifndef IL2FUSION_PLUGINS_COCOS_TEXT_CAPTURE_H
#define IL2FUSION_PLUGINS_COCOS_TEXT_CAPTURE_H

#include "../../utils/utils.h"

#include <string>

namespace cocos_runtime::internal {

void InstallTextCaptureHooksForModule(const std::string& module_name,
                                      const hookutils::ModuleInfo& module_info);

}  // namespace cocos_runtime::internal

#endif  // IL2FUSION_PLUGINS_COCOS_TEXT_CAPTURE_H
