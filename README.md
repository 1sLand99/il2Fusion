il2Fusion
================

<p align="center">
  <img src="doc/imgs/il2FusionIcon.png" alt="il2Fusion icon" width="233" />
</p>

il2Fusion 是一个运行在 Android 侧、围绕 LSPosed、JNI 与 Native Hook 后端构建的 Unity 游戏逆向工程工具集。它将 Il2Cpp dump 生成、可配置文本拦截、跨进程配置同步以及设备端逆向辅助能力整合到同一套工作流中。

英文版：见 [README_EN](doc/README_EN.md)。

## 核心亮点
- 双 Native Hook 后端，可在运行期切换：默认使用 `And64InlineHook`，并提供 `Dobby` 作为替代方案。
- Dump 优先工作流，可生成 `dump.cs` 并导出到 `/sdcard/Download/<pkg>.cs`。
- Unity 文本拦截链路，优先使用 JSON/RVA 目标，缺失时回退到反射方式。
- 从应用进程到 LSPosed 注入进程的跨进程配置同步能力。

## 架构总览

```mermaid
flowchart LR
    A["Compose 应用壳层<br/>app/"] --> B["业务 Feature ViewModel<br/>feature/*"]
    B --> C["共享配置仓库<br/>config/"]
    B --> D["国际化模块<br/>core/i18n"]
    B --> E["更新模块<br/>core/update"]
    E --> F["网络层<br/>core/network"]
    F --> G["GitHub Releases API"]
    C --> H["ContentProvider + SharedPreferences"]
    I["LSPosed 入口<br/>com.tools.module.MainHook"] --> H
    I --> J["NativeBridge JNI"]
    J --> K["native_hook.cpp"]
    K --> L["Dump 插件"]
    K --> M["文本提取 / SQLite"]
```

## 工作原理

```mermaid
sequenceDiagram
    participant U as 用户
    participant App as Compose 应用
    participant Repo as HookConfigRepository
    participant Provider as ConfigContentProvider
    participant Hook as LSPosed MainHook
    participant JNI as NativeBridge
    participant Native as native_hook.cpp
    participant Il2Cpp as libil2cpp.so

    U->>App: 配置模式 / 后端 / 目标方法
    App->>Repo: 保存配置
    Repo->>Provider: 持久化共享配置
    Hook->>Provider: 在目标进程中读取配置
    Hook->>JNI: 下发模式 / 目标 / JSON / 后端
    JNI->>Native: 初始化运行时
    Native->>Il2Cpp: 等待 libil2cpp.so
    alt Dump 模式
        Native->>Native: 执行 Il2CppDumper 流程
    else 文本 Hook 模式
        Native->>Native: 安装 Hook 后端
        Native->>Native: 记录或替换 Unity 文本
    end
```

## 功能集合
- **Dump 工作流：** 触发 Il2Cpp dump 生成，并将结果导出到设备下载目录。
- **文本拦截：** 对 Unity 文本 setter 安装 Native Hook，并将捕获结果写入 SQLite。
- **解析流程：** 从 `dump.cs` 中提取 `set_text` 目标，并以方法名和 JSON 元数据形式持久化。

## 环境要求
- 已 Root 的设备，并具备 Magisk + LSPosed 环境。
- Android 9+（`minSdk 28`、`targetSdk 35`、`compileSdk 36`）。
- 默认 ABI：`arm64-v8a`。
  如需支持更多 ABI，请补充 `app/src/main/cpp/libs/<abi>/libdobby.a` 并更新 `ndk.abiFilters`。
- 已验证设备：Google Pixel 3 XL，Android 12（`SP1A.210812.016.C2 / 8618562`）。

## 快速开始
1. 构建模块：`./gradlew :app:assembleDebug`
2. 安装 APK，并在 LSPosed 中只勾选一个目标应用。
3. 打开 il2Fusion 应用并选择一种工作流。
   文本 Hook 模式：
   保持 Dump 关闭，解析 `dump.cs`，并保存目标 setter 列表。
   Dump 模式：
   开启 Dump，启动目标应用，并等待 `dump.cs` 导出。
4. 在目标应用中验证运行结果。
   文本 Hook 模式：
   等待 `libil2cpp.so`，确认 Hook 安装完成，并检查 `/data/data/<pkg>/text.db`。
   Dump 模式：
   等待 Dump 流程完成，并检查 Download 目录中的输出文件。

## 项目结构
- `app/src/main/java/com/tools/il2fusion/app/`：应用壳层、导航、启动更新检查和全局弹窗。
- `app/src/main/java/com/tools/il2fusion/feature/`：基于 MVVM 的页面级业务 Feature，包括 overview、mode、parse、settings。
- `app/src/main/java/com/tools/il2fusion/core/`：共享模块，包括设计组件、国际化、网络层和更新流程。
- `app/src/main/java/com/tools/il2fusion/config/`：供应用进程与 Hook 进程共同使用的 Provider 配置层。
- `app/src/main/java/com/tools/module/`：LSPosed 入口与 JNI 对接的 Android 桥接层。
- `app/src/main/cpp/`：Native Hook 运行时、Il2CppDumper 集成、文本提取插件和 SQLite 支持。
- `app/src/main/assets/xposed/`：LSPosed 模块描述与入口声明。

## 致谢
- [Rprop - And64InlineHook](https://github.com/Rprop/And64InlineHook)：ARM64 Inline Hook 实现。
- [jmpews - Dobby](https://github.com/jmpews/Dobby)：轻量级跨平台 Hook 框架。
- [Perfare - Zygisk-Il2CppDumper](https://github.com/Perfare/Zygisk-Il2CppDumper)：Il2CppDumper 实现参考。

## 贡献
- 欢迎提交 issue 与功能建议。建议附上目标应用、Android 版本、LSPosed 环境、期望行为和日志。
- 欢迎提交改进 Hook 稳定性、解析准确度、ABI 覆盖、文档质量或 UI/UX 的 PR。

## 免责声明
- 本项目仅供学习、研究与安全测试之用，不得用于任何违法、侵权或商业牟利场景。
- 使用者需自行确保遵守所在地法律法规，并对由此产生的全部后果自行承担。
