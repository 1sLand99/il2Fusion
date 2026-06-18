package com.tools.module

import android.content.Context
import com.tools.il2fusion.config.CocosBundledFont
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.config.HookFramework
import com.tools.il2fusion.config.HookConfigStore
import com.tools.il2fusion.config.RuntimeHookConfig
import de.robv.android.xposed.IXposedHookLoadPackage
import de.robv.android.xposed.XC_MethodHook
import de.robv.android.xposed.XposedBridge
import de.robv.android.xposed.XposedHelpers
import de.robv.android.xposed.callbacks.XC_LoadPackage

/**
 * <pre>
 *     author: PenguinAndy
 *     time  : 2025/11/28 17:41
 *     desc  :
 * </pre>
 */
class MainHook: IXposedHookLoadPackage {

    private companion object {
        const val TAG = "[il2Fusion]"
        const val NATIVE_LIB = "native_hook"
        const val MODULE_PKG = "com.tools.il2fusion"

        private fun resolveCocosFontId(config: RuntimeHookConfig): String {
            if (config.engine != GameEngine.Cocos2dxLua) {
                return ""
            }
            if (config.cocosFontId.isNotBlank()) {
                return config.cocosFontId
            }
            return if (
                config.textReplacementEnabled &&
                containsThaiCodePoint(config.textReplacementValue)
            ) {
                CocosBundledFont.NotoSansThai.id
            } else {
                ""
            }
        }

        private fun containsThaiCodePoint(value: String): Boolean {
            return value.any { it.code in 0x0E00..0x0E7F }
        }

        private fun isMainProcess(packageName: String, processName: String): Boolean {
            return processName.isBlank() || processName == packageName
        }
    }

    override fun handleLoadPackage(lpparam: XC_LoadPackage.LoadPackageParam) {
        // ignore self
        if (lpparam.packageName == MODULE_PKG) return

        XposedBridge.log("$TAG Inject to: ${lpparam.packageName}")

        val cl = lpparam.classLoader

        // Hook Application.attach(Context)
        XposedHelpers.findAndHookMethod(
            "android.app.Application",
            cl,
            "attach",
            Context::class.java,
            object : XC_MethodHook() {
                override fun afterHookedMethod(param: MethodHookParam) {
                    // Keep context for future use (e.g. resource access)
                    val ctx = param.args[0] as? Context
                    if (ctx == null) {
                        XposedBridge.log("$TAG Application.attach -> ctx is null, skip init")
                        return
                    }
                    NativeBridge.setContext(ctx)
                    XposedBridge.log("$TAG Application.attach -> ctx=$ctx")

                    // Load native so
                    try {
                        System.loadLibrary(NATIVE_LIB)
                        XposedBridge.log("$TAG $NATIVE_LIB loaded")
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG loadLibrary failed: $e")
                    }

                    // native initialize
                    try {
                        NativeBridge.init(lpparam.processName)
                        XposedBridge.log("$TAG Native init() call, process=${lpparam.processName}")
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG Native init() failed: $e")
                    }

                    try {
                        TextDbExportBridge.start(ctx, lpparam.packageName, lpparam.processName)
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG TextDbExportBridge start failed: $e")
                    }

                    try {
                        val hookFramework = HookConfigStore.loadHookFrameworkForHook(ctx)
                        val frameworkValue = when (hookFramework) {
                            HookFramework.And64InlineHook -> 0
                            HookFramework.Dobby -> 1
                        }
                        NativeBridge.setHookFramework(frameworkValue)
                        XposedBridge.log("$TAG setHookFramework -> ${hookFramework.storageValue}")
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG setHookFramework failed: $e")
                    }

                    val runtimeConfig = try {
                        val config = HookConfigStore.loadRuntimeConfigForHook(ctx)
                        if (config.engine == GameEngine.Cocos2dxLua &&
                            !isMainProcess(lpparam.packageName, lpparam.processName)
                        ) {
                            NativeBridge.setGameEngine(config.engine.nativeValue)
                            XposedBridge.log(
                                "$TAG Cocos runtime skipped for non-main process " +
                                    "${lpparam.packageName}/${lpparam.processName}"
                            )
                            return
                        }
                        NativeBridge.setGameEngine(config.engine.nativeValue)
                        NativeBridge.setTextReplacement(config.textReplacementEnabled, config.textReplacementValue)
                        NativeBridge.setTextDbResetOnStart(config.textDbResetOnStart)
                        NativeBridge.setCocosTextCaptureEnabled(config.cocosTextCaptureEnabled)
                        NativeBridge.setCocosTextPersistence(
                            config.cocosTextPersistEnabled,
                            config.cocosTextPersistChineseOnly
                        )
                        NativeBridge.setCocosTextReplacementDelay(
                            config.cocosTextReplacementDelayEnabled,
                            config.cocosTextReplacementDelayMs
                        )
                        NativeBridge.setCocosBurstDelayGuard(
                            config.cocosTypewriterOptimizationEnabled,
                            config.cocosTypewriterIdleFinalizeMs
                        )
                        val resolvedCocosFontId = resolveCocosFontId(config)
                        val resolvedCocosFont = if (resolvedCocosFontId.isNotBlank()) {
                            CocosFontAssetInstaller.installSelectedFont(
                                ctx,
                                resolvedCocosFontId
                            )
                        } else {
                            CocosFontAssetInstaller.InstalledFont()
                        }
                        NativeBridge.setCocosFontReplacement(
                            resolvedCocosFont.ttfPath.isNotBlank() ||
                                resolvedCocosFont.bmfontFntPath.isNotBlank(),
                            resolvedCocosFont.ttfPath,
                            resolvedCocosFont.bmfontFntPath
                        )
                        val rules = config.cocosLuaReplacementRules
                        val cocosFontSource = when {
                            resolvedCocosFontId.isBlank() -> "none"
                            config.cocosFontId.isBlank() -> "auto"
                            else -> "selected"
                        }
                        NativeBridge.setCocosLuaReplacementRules(
                            BooleanArray(rules.size) { index -> rules[index].enabled },
                            Array(rules.size) { index -> rules[index].scriptName },
                            Array(rules.size) { index -> rules[index].replacedCode },
                            Array(rules.size) { index -> rules[index].replaceCode },
                            Array(rules.size) { index -> rules[index].prependCode }
                        )
                        NativeBridge.startCocosRuntimeIfNeeded()
                        XposedBridge.log(
                            "$TAG runtime config -> engine=${config.engine.storageValue}, " +
                                "replace=${config.textReplacementEnabled}, " +
                                "replaceLength=${config.textReplacementValue.length}, " +
                                "textDbReset=${config.textDbResetOnStart}, " +
                                "cocosText=${config.cocosTextCaptureEnabled}, " +
                                "cocosTextPersist=${config.cocosTextPersistEnabled}, " +
                                "cocosTextChineseOnly=${config.cocosTextPersistChineseOnly}, " +
                                "cocosReplaceDelay=${config.cocosTextReplacementDelayEnabled}, " +
                                "cocosReplaceDelayMs=${config.cocosTextReplacementDelayMs}, " +
                                "cocosBurstDelayGuard=${config.cocosTypewriterOptimizationEnabled}, " +
                                "cocosBurstHoldMs=${config.cocosTypewriterIdleFinalizeMs}, " +
                                "cocosFontReplace=${resolvedCocosFont.ttfPath.isNotBlank() || resolvedCocosFont.bmfontFntPath.isNotBlank()}, " +
                                "cocosFontTtfLength=${resolvedCocosFont.ttfPath.length}, " +
                                "cocosFontBmFntLength=${resolvedCocosFont.bmfontFntPath.length}, " +
                                "cocosFont=${resolvedCocosFontId.ifBlank { "none" }}, " +
                                "cocosFontSource=$cocosFontSource, " +
                                "luaRules=${config.cocosLuaReplacementRules.size}"
                        )
                        TargetSessionBridge.start(
                            ctx = ctx,
                            packageName = lpparam.packageName,
                            processName = lpparam.processName.ifBlank { lpparam.packageName },
                            engine = config.engine
                        )
                        config
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG apply runtime config failed: $e")
                        return
                    }

                    if (runtimeConfig.engine != GameEngine.UnityIl2Cpp) {
                        XposedBridge.log(
                            "$TAG runtime config active -> ${runtimeConfig.engine.storageValue}, " +
                                "skip Unity target setup"
                        )
                        return
                    }

                    try {
                        val targetsJson = HookConfigStore.loadTargetsJsonForHook(ctx)
                        NativeBridge.setTargetsJson(targetsJson)
                        XposedBridge.log("$TAG setTargetsJson -> length=${targetsJson.length}")
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG setTargetsJson failed: $e")
                    }

                    val dumpMode = try {
                        HookConfigStore.loadDumpModeForHook(ctx)
                    } catch (e: Throwable) {
                        XposedBridge.log("$TAG loadDumpMode failed: $e")
                        false
                    }

                    if (dumpMode) {
                        // Dump 模式：启动 il2cpp dumper，不做文本拦截
                        try {
                            val dir = ctx.dataDir?.absolutePath ?: ""
                            NativeBridge.startDump(dir)
                            XposedBridge.log("$TAG startDump -> $dir")
                        } catch (e: Throwable) {
                            XposedBridge.log("$TAG startDump failed: $e")
                        }
                    } else {
                        // 文本拦截模式：下发目标方法列表，开启文本日志
                        try {
                            val targets = HookConfigStore.loadTargetsForHook(ctx).toTypedArray()
                            NativeBridge.setTargets(targets)
                            XposedBridge.log("$TAG setTargets -> ${targets.joinToString()}")
                            // 如果当前列表为空，延迟重试几次，避免主进程早于配置写入完成
                            if (targets.isEmpty()) {
                                retryLoadTargets(ctx, 3, 500L)
                            }
                        } catch (e: Throwable) {
                            XposedBridge.log("$TAG setTargets failed: $e")
                        }
                    }
                }
            }
        )
    }

    private fun retryLoadTargets(ctx: Context, times: Int, delayMs: Long) {
        if (times <= 0) return
        Thread {
            repeat(times) { idx ->
                try {
                    Thread.sleep(delayMs)
                    val targetsJson = HookConfigStore.loadTargetsJsonForHook(ctx)
                    if (targetsJson.isNotBlank()) {
                        NativeBridge.setTargetsJson(targetsJson)
                    }
                    val targets = HookConfigStore.loadTargetsForHook(ctx).toTypedArray()
                    if (targets.isNotEmpty()) {
                        NativeBridge.setTargets(targets)
                        XposedBridge.log("$TAG retry#$idx setTargets -> ${targets.joinToString()}")
                        return@Thread
                    }
                } catch (t: Throwable) {
                    XposedBridge.log("$TAG retryLoadTargets failed: $t")
                }
            }
        }.start()
    }
}
