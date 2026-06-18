#ifndef IL2FUSION_DB_H
#define IL2FUSION_DB_H

#include <string>

namespace textdb {

// 初始化数据库，若不存在则创建；log_path 为 true 时打印路径/存在状态。
// reset_on_main_process 为 true 时，目标主进程启动会删除旧库后重新创建。
void Init(const std::string& process_name, bool log_path, bool reset_on_main_process = false);

// 控制目标主进程启动时是否清空 text.db；默认关闭。
void SetResetOnMainProcess(bool enabled);
bool ResetOnMainProcess();

// 将文本写入数据库，已存在则跳过。
void InsertIfNeeded(const std::string& text);

}  // namespace textdb

#endif  // IL2FUSION_DB_H
