package com.tools.il2fusion.config

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Mediates data access between the UI and HookConfigStore to keep logic centralized.
 */
class HookConfigRepository(
    context: Context
) {
    private val appContext = context.applicationContext

    /**
     * Loads stored target method list and dump mode flag from the shared content provider.
     */
    suspend fun loadConfig(): HookConfigPayload = withContext(Dispatchers.IO) {
        val savedTargets = HookConfigStore.loadTargetsForApp(appContext)
        val dumpMode = HookConfigStore.loadDumpModeForApp(appContext)
        val hookFramework = HookConfigStore.loadHookFrameworkForApp(appContext)
        val targetsJson = HookConfigStore.loadTargetsJsonForApp(appContext)
        HookConfigPayload(
            targets = savedTargets,
            dumpModeEnabled = dumpMode,
            hookFramework = hookFramework,
            targetsJson = targetsJson,
            runtimeConfig = HookConfigStore.loadRuntimeConfigForApp(appContext),
            textDbExportRequest = HookConfigStore.loadTextDbExportRequestForApp(appContext),
            textDbExportStatus = HookConfigStore.loadTextDbExportStatusForApp(appContext)
        )
    }

    /**
     * Persists the dump mode flag through the content provider.
     */
    suspend fun saveDumpMode(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveDumpMode(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveHookFramework(framework: HookFramework) = withContext(Dispatchers.IO) {
        HookConfigStore.saveHookFramework(appContext, framework)
        HookConfigChangeBus.notifyChanged()
    }

    /**
     * Persists the target method list through the content provider.
     */
    suspend fun saveTargets(targets: List<String>) = withContext(Dispatchers.IO) {
        HookConfigStore.saveTargets(appContext, targets)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveTargetsJson(json: String) = withContext(Dispatchers.IO) {
        HookConfigStore.saveTargetsJson(appContext, json)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveGameEngine(engine: GameEngine) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, engine)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveTextReplacementEnabled(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveTextReplacementEnabled(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveTextReplacementValue(value: String) = withContext(Dispatchers.IO) {
        HookConfigStore.saveTextReplacementValue(appContext, value)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveTextDbResetOnStart(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveTextDbResetOnStart(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTextCaptureEnabled(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTextCaptureEnabled(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTextPersistEnabled(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTextPersistEnabled(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTextPersistChineseOnly(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTextPersistChineseOnly(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTextReplacementDelayEnabled(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTextReplacementDelayEnabled(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTextReplacementDelayMs(delayMs: Int) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTextReplacementDelayMs(appContext, delayMs)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTypewriterOptimizationEnabled(enabled: Boolean) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTypewriterOptimizationEnabled(appContext, enabled)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosTypewriterIdleFinalizeMs(value: Int) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosTypewriterIdleFinalizeMs(appContext, value)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosFontId(value: String) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosFontId(appContext, value)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveCocosLuaReplacementRules(rules: List<CocosLuaReplacementRule>) = withContext(Dispatchers.IO) {
        HookConfigStore.saveGameEngine(appContext, GameEngine.Cocos2dxLua)
        HookConfigStore.saveCocosLuaReplacementRules(appContext, rules)
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun saveTextDbExportRequest(request: TextDbExportRequest) = withContext(Dispatchers.IO) {
        HookConfigStore.saveTextDbExportRequest(appContext, request)
        HookConfigStore.saveTextDbExportStatus(
            appContext,
            TextDbExportStatus(
                requestId = request.requestId,
                targetPackage = request.targetPackage,
                state = TextDbExportJson.STATE_PENDING,
                message = "Waiting for target process to handle export. Restart the target app if it was already running after module install/update.",
                exportPath = "",
                updatedAt = System.currentTimeMillis()
            )
        )
        HookConfigChangeBus.notifyChanged()
    }

    suspend fun loadTextDbExportStatus(): TextDbExportStatus = withContext(Dispatchers.IO) {
        HookConfigStore.loadTextDbExportStatusForApp(appContext)
    }

    suspend fun loadTargetSessions(): List<TargetSession> = withContext(Dispatchers.IO) {
        HookConfigStore.loadTargetSessionsForApp(appContext)
    }
}

/**
 * Represents stored configuration values used by both the UI and native hook.
 */
data class HookConfigPayload(
    val targets: List<String>,
    val dumpModeEnabled: Boolean,
    val hookFramework: HookFramework,
    val targetsJson: String,
    val runtimeConfig: RuntimeHookConfig,
    val textDbExportRequest: TextDbExportRequest,
    val textDbExportStatus: TextDbExportStatus
)
