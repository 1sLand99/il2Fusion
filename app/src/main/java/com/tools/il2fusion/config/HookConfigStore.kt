package com.tools.il2fusion.config

import android.content.Context
import android.util.Log
import android.content.ContentValues
import com.tools.il2fusion.config.ConfigContentProvider.Companion.CONTENT_URI
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_LUA_RULES
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TEXT_CAPTURE_ENABLED
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_FONT_ID
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TEXT_REPLACEMENT_DELAY_ENABLED
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TEXT_REPLACEMENT_DELAY_MS
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TEXT_PERSIST_CHINESE_ONLY
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TEXT_PERSIST_ENABLED
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TYPEWRITER_IDLE_FINALIZE_MS
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_COCOS_TYPEWRITER_OPTIMIZATION_ENABLED
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_DUMP_MODE
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_GAME_ENGINE
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_HOOK_FRAMEWORK
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TARGETS
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TARGETS_JSON
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TARGET_SESSIONS
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TARGET_SESSION_REPORT
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TEXT_DB_RESET_ON_START
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TEXT_REPLACEMENT_ENABLED
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TEXT_REPLACEMENT_VALUE
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TEXT_DB_EXPORT_REQUEST
import com.tools.il2fusion.config.ConfigContentProvider.Companion.KEY_TEXT_DB_EXPORT_STATUS

object HookConfigStore {
    private const val TAG = "[il2Fusion]"

    fun saveTargets(ctx: Context, targets: List<String>) {
        val storeCtx = ctx.applicationContext ?: ctx
        val text = targets.joinToString(separator = ",")
        val values = ContentValues().apply {
            put("key", KEY_TARGETS)
            put("value", text)
        }
        storeCtx.contentResolver.insert(CONTENT_URI, values)
        Log.i(TAG, "saveTargets(): stored ${targets.size} items -> $text via provider")
    }

    fun saveDumpMode(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        val values = ContentValues().apply {
            put("key", KEY_DUMP_MODE)
            put("value", if (enabled) "1" else "0")
        }
        storeCtx.contentResolver.insert(CONTENT_URI, values)
        Log.i(TAG, "saveDumpMode(): $enabled")
    }

    fun saveHookFramework(ctx: Context, framework: HookFramework) {
        val storeCtx = ctx.applicationContext ?: ctx
        val values = ContentValues().apply {
            put("key", KEY_HOOK_FRAMEWORK)
            put("value", framework.storageValue)
        }
        storeCtx.contentResolver.insert(CONTENT_URI, values)
        Log.i(TAG, "saveHookFramework(): ${framework.storageValue}")
    }

    fun saveTargetsJson(ctx: Context, json: String) {
        val storeCtx = ctx.applicationContext ?: ctx
        val values = ContentValues().apply {
            put("key", KEY_TARGETS_JSON)
            put("value", json)
        }
        storeCtx.contentResolver.insert(CONTENT_URI, values)
        Log.i(TAG, "saveTargetsJson(): length=${json.length}")
    }

    fun saveGameEngine(ctx: Context, engine: GameEngine) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_GAME_ENGINE, engine.storageValue)
        Log.i(TAG, "saveGameEngine(): ${engine.storageValue}")
    }

    fun saveTextReplacementEnabled(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_TEXT_REPLACEMENT_ENABLED, if (enabled) "1" else "0")
        Log.i(TAG, "saveTextReplacementEnabled(): $enabled")
    }

    fun saveTextReplacementValue(ctx: Context, value: String) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_TEXT_REPLACEMENT_VALUE, value)
        Log.i(TAG, "saveTextReplacementValue(): length=${value.length}")
    }

    fun saveTextDbResetOnStart(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_TEXT_DB_RESET_ON_START, if (enabled) "1" else "0")
        Log.i(TAG, "saveTextDbResetOnStart(): $enabled")
    }

    fun saveCocosTextCaptureEnabled(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_TEXT_CAPTURE_ENABLED, if (enabled) "1" else "0")
        Log.i(TAG, "saveCocosTextCaptureEnabled(): $enabled")
    }

    fun saveCocosTextPersistEnabled(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_TEXT_PERSIST_ENABLED, if (enabled) "1" else "0")
        Log.i(TAG, "saveCocosTextPersistEnabled(): $enabled")
    }

    fun saveCocosTextPersistChineseOnly(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_TEXT_PERSIST_CHINESE_ONLY, if (enabled) "1" else "0")
        Log.i(TAG, "saveCocosTextPersistChineseOnly(): $enabled")
    }

    fun saveCocosTextReplacementDelayEnabled(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_TEXT_REPLACEMENT_DELAY_ENABLED, if (enabled) "1" else "0")
        Log.i(TAG, "saveCocosTextReplacementDelayEnabled(): $enabled")
    }

    fun saveCocosTextReplacementDelayMs(ctx: Context, delayMs: Int) {
        val storeCtx = ctx.applicationContext ?: ctx
        val clamped = RuntimeTextReplacementDefaults.clampCocosDelayMs(delayMs)
        saveValue(storeCtx, KEY_COCOS_TEXT_REPLACEMENT_DELAY_MS, clamped.toString())
        Log.i(TAG, "saveCocosTextReplacementDelayMs(): $clamped")
    }

    fun saveCocosTypewriterOptimizationEnabled(ctx: Context, enabled: Boolean) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_TYPEWRITER_OPTIMIZATION_ENABLED, if (enabled) "1" else "0")
        Log.i(TAG, "saveCocosTypewriterOptimizationEnabled(): $enabled")
    }

    fun saveCocosTypewriterIdleFinalizeMs(ctx: Context, value: Int) {
        val storeCtx = ctx.applicationContext ?: ctx
        val clamped = RuntimeTextReplacementDefaults.clampTypewriterIdleFinalizeMs(value)
        saveValue(storeCtx, KEY_COCOS_TYPEWRITER_IDLE_FINALIZE_MS, clamped.toString())
        Log.i(TAG, "saveCocosTypewriterIdleFinalizeMs(): $clamped")
    }

    fun saveCocosFontId(ctx: Context, value: String) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_FONT_ID, CocosBundledFont.fromId(value)?.id ?: "")
        Log.i(TAG, "saveCocosFontId(): $value")
    }

    fun saveCocosLuaReplacementRules(ctx: Context, rules: List<CocosLuaReplacementRule>) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_COCOS_LUA_RULES, CocosLuaRulesCodec.encodeRules(rules))
        Log.i(TAG, "saveCocosLuaReplacementRules(): ${rules.size}")
    }

    fun saveTextDbExportRequest(ctx: Context, request: TextDbExportRequest) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_TEXT_DB_EXPORT_REQUEST, TextDbExportJson.encodeRequest(request))
        Log.i(TAG, "saveTextDbExportRequest(): ${request.requestId} target=${request.targetPackage}")
    }

    fun saveTextDbExportStatus(ctx: Context, status: TextDbExportStatus) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_TEXT_DB_EXPORT_STATUS, TextDbExportJson.encodeStatus(status))
        Log.i(TAG, "saveTextDbExportStatus(): ${status.requestId} state=${status.state}")
    }

    fun reportTargetSession(ctx: Context, session: TargetSession) {
        val storeCtx = ctx.applicationContext ?: ctx
        saveValue(storeCtx, KEY_TARGET_SESSION_REPORT, TargetSessionJson.encodeSession(session))
        Log.i(TAG, "reportTargetSession(): ${session.packageName}/${session.processName} pid=${session.pid}")
    }

    fun loadTargetsForApp(ctx: Context): List<String> {
        val storeCtx = ctx.applicationContext ?: ctx
        return queryTargets(storeCtx)
    }

    fun loadTargetsForHook(ctx: Context): List<String> {
        return queryTargets(ctx)
    }

    fun loadDumpModeForApp(ctx: Context): Boolean {
        val storeCtx = ctx.applicationContext ?: ctx
        return queryDumpMode(storeCtx)
    }

    fun loadDumpModeForHook(ctx: Context): Boolean {
        return queryDumpMode(ctx)
    }

    fun loadHookFrameworkForApp(ctx: Context): HookFramework {
        val storeCtx = ctx.applicationContext ?: ctx
        return queryHookFramework(storeCtx)
    }

    fun loadHookFrameworkForHook(ctx: Context): HookFramework {
        return queryHookFramework(ctx)
    }

    fun loadTargetsJsonForApp(ctx: Context): String {
        val storeCtx = ctx.applicationContext ?: ctx
        return queryValue(storeCtx, KEY_TARGETS_JSON)
    }

    fun loadTargetsJsonForHook(ctx: Context): String {
        return queryValue(ctx, KEY_TARGETS_JSON)
    }

    fun loadRuntimeConfigForApp(ctx: Context): RuntimeHookConfig {
        val storeCtx = ctx.applicationContext ?: ctx
        return queryRuntimeConfig(storeCtx)
    }

    fun loadRuntimeConfigForHook(ctx: Context): RuntimeHookConfig {
        return queryRuntimeConfig(ctx)
    }

    fun loadTextDbExportRequestForApp(ctx: Context): TextDbExportRequest {
        val storeCtx = ctx.applicationContext ?: ctx
        return TextDbExportJson.decodeRequest(queryValue(storeCtx, KEY_TEXT_DB_EXPORT_REQUEST))
    }

    fun loadTextDbExportRequestForHook(ctx: Context): TextDbExportRequest {
        return TextDbExportJson.decodeRequest(queryValue(ctx, KEY_TEXT_DB_EXPORT_REQUEST))
    }

    fun loadTextDbExportStatusForApp(ctx: Context): TextDbExportStatus {
        val storeCtx = ctx.applicationContext ?: ctx
        return TextDbExportJson.decodeStatus(queryValue(storeCtx, KEY_TEXT_DB_EXPORT_STATUS))
    }

    fun loadTextDbExportStatusForHook(ctx: Context): TextDbExportStatus {
        return TextDbExportJson.decodeStatus(queryValue(ctx, KEY_TEXT_DB_EXPORT_STATUS))
    }

    fun loadTargetSessionsForApp(ctx: Context): List<TargetSession> {
        val storeCtx = ctx.applicationContext ?: ctx
        return queryTargetSessions(storeCtx)
    }

    private fun parseRaw(raw: String): List<String> {
        return raw.split(',', '\n')
            .mapNotNull {
                val trimmed = it.trim()
                if (trimmed.isEmpty()) null else trimmed
            }
    }

    private fun queryDumpMode(ctx: Context): Boolean {
        val cursor = try {
            ctx.contentResolver.query(CONTENT_URI, null, null, null, null)
        } catch (t: Throwable) {
            Log.w(TAG, "queryDumpMode() failed: ${t.message}")
            null
        } ?: return false

        cursor.use { c ->
            if (!c.moveToFirst()) return false
            val keyIdx = c.getColumnIndex("key")
            val valIdx = c.getColumnIndex("value")
            do {
                val key = if (keyIdx >= 0) c.getString(keyIdx) else ""
                val value = if (valIdx >= 0) c.getString(valIdx) else ""
                if (key == KEY_DUMP_MODE) {
                    return value == "1" || value.equals("true", ignoreCase = true)
                }
            } while (c.moveToNext())
        }
        return false
    }

    private fun saveValue(ctx: Context, key: String, value: String) {
        val values = ContentValues().apply {
            put("key", key)
            put("value", value)
        }
        ctx.contentResolver.insert(CONTENT_URI, values)
    }

    private fun queryHookFramework(ctx: Context): HookFramework {
        return HookFramework.fromStorageValue(queryValue(ctx, KEY_HOOK_FRAMEWORK))
    }

    private fun queryTargets(ctx: Context): List<String> {
        val raw = queryValue(ctx, KEY_TARGETS)
        if (raw.isBlank()) {
            Log.i(TAG, "queryTargets(): empty result")
            return emptyList()
        }
        val parsed = parseRaw(raw)
        Log.i(TAG, "queryTargets(): raw=$raw -> ${parsed.size} items")
        return parsed
    }

    private fun queryRuntimeConfig(ctx: Context): RuntimeHookConfig {
        return RuntimeHookConfig(
            engine = GameEngine.fromStorageValue(queryValue(ctx, KEY_GAME_ENGINE)),
            textReplacementEnabled = queryBoolean(ctx, KEY_TEXT_REPLACEMENT_ENABLED),
            textReplacementValue = queryValueOrNull(ctx, KEY_TEXT_REPLACEMENT_VALUE)
                ?: RuntimeTextReplacementDefaults.DEFAULT_TEXT,
            textDbResetOnStart = queryBoolean(ctx, KEY_TEXT_DB_RESET_ON_START),
            cocosTextCaptureEnabled = queryBoolean(ctx, KEY_COCOS_TEXT_CAPTURE_ENABLED, defaultValue = true),
            cocosTextPersistEnabled = queryBoolean(ctx, KEY_COCOS_TEXT_PERSIST_ENABLED, defaultValue = true),
            cocosTextPersistChineseOnly = queryBoolean(ctx, KEY_COCOS_TEXT_PERSIST_CHINESE_ONLY, defaultValue = true),
            cocosTextReplacementDelayEnabled = queryBoolean(ctx, KEY_COCOS_TEXT_REPLACEMENT_DELAY_ENABLED),
            cocosTextReplacementDelayMs = RuntimeTextReplacementDefaults.clampCocosDelayMs(
                queryInt(ctx, KEY_COCOS_TEXT_REPLACEMENT_DELAY_MS, RuntimeTextReplacementDefaults.DEFAULT_COCOS_DELAY_MS)
            ),
            cocosTypewriterOptimizationEnabled = queryBoolean(ctx, KEY_COCOS_TYPEWRITER_OPTIMIZATION_ENABLED),
            cocosTypewriterIdleFinalizeMs = RuntimeTextReplacementDefaults.clampTypewriterIdleFinalizeMs(
                queryInt(
                    ctx,
                    KEY_COCOS_TYPEWRITER_IDLE_FINALIZE_MS,
                    RuntimeTextReplacementDefaults.DEFAULT_TYPEWRITER_IDLE_FINALIZE_MS
                )
            ),
            cocosFontId = CocosBundledFont.fromId(queryValue(ctx, KEY_COCOS_FONT_ID))?.id ?: "",
            cocosLuaReplacementRules = CocosLuaRulesCodec.decodeRules(queryValue(ctx, KEY_COCOS_LUA_RULES))
        )
    }

    private fun queryTargetSessions(ctx: Context): List<TargetSession> {
        return TargetSessionJson.decodeSessions(queryValue(ctx, KEY_TARGET_SESSIONS))
            .sortedByDescending { it.lastHeartbeatAt }
    }

    private fun queryBoolean(ctx: Context, key: String, defaultValue: Boolean = false): Boolean {
        return queryValueOrNull(ctx, key)?.let { raw ->
            raw == "1" || raw.equals("true", ignoreCase = true)
        } ?: defaultValue
    }

    private fun queryInt(ctx: Context, key: String, defaultValue: Int): Int {
        return queryValueOrNull(ctx, key)?.toIntOrNull() ?: defaultValue
    }

    private fun queryValue(ctx: Context, targetKey: String): String {
        return queryValueOrNull(ctx, targetKey) ?: ""
    }

    private fun queryValueOrNull(ctx: Context, targetKey: String): String? {
        val cursor = try {
            ctx.contentResolver.query(CONTENT_URI, null, null, null, null)
        } catch (t: Throwable) {
            Log.w(TAG, "queryValue($targetKey) failed: ${t.message}")
            null
        } ?: return null

        cursor.use { c ->
            if (!c.moveToFirst()) return null
            val keyIdx = c.getColumnIndex("key")
            val valIdx = c.getColumnIndex("value")
            do {
                val key = if (keyIdx >= 0) c.getString(keyIdx) else ""
                val value = if (valIdx >= 0) c.getString(valIdx) else ""
                if (key == targetKey) {
                    return value ?: ""
                }
            } while (c.moveToNext())
        }
        return null
    }
}
