package com.tools.module

/**
 * <pre>
 *     author: PenguinAndy
 *     time  : 2025/11/28 18:10
 *     desc  :
 * </pre>
 */
object NativeBridge {
    private var appContext: android.content.Context? = null

    internal fun setContext(ctx: android.content.Context) {
        appContext = ctx.applicationContext ?: ctx
    }

    @JvmStatic
    fun onDumpFinished(success: Boolean, message: String) {
        val ctx = appContext ?: return
        val text = if (success) "Dump 成功：$message" else "Dump 失败：$message"
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            android.widget.Toast.makeText(ctx, text, android.widget.Toast.LENGTH_SHORT).show()
        }
    }

    @JvmStatic external fun init(processName: String)
    @JvmStatic external fun setHookFramework(framework: Int)
    @JvmStatic external fun setGameEngine(engine: Int)
    @JvmStatic external fun setTextReplacement(enabled: Boolean, text: String)
    @JvmStatic external fun setTextDbResetOnStart(enabled: Boolean)
    @JvmStatic external fun setCocosTextCaptureEnabled(enabled: Boolean)
    @JvmStatic external fun setCocosTextPersistence(enabled: Boolean, chineseOnly: Boolean)
    @JvmStatic external fun setCocosTextReplacementDelay(enabled: Boolean, delayMs: Int)
    @JvmStatic external fun setCocosBurstDelayGuard(enabled: Boolean, holdMs: Int)
    @JvmStatic external fun setCocosFontReplacement(
        enabled: Boolean,
        ttfPath: String,
        bmfontFntPath: String
    )
    @JvmStatic external fun setCocosLuaReplacementRules(
        enabledValues: BooleanArray,
        scriptNames: Array<String>,
        replacedCodes: Array<String>,
        replaceCodes: Array<String>,
        prependCodes: Array<String>
    )
    @JvmStatic external fun startCocosRuntimeIfNeeded()
    @JvmStatic external fun setTargetsJson(json: String)
    @JvmStatic external fun setTargets(targets: Array<String>)
    @JvmStatic external fun startDump(dataDir: String)
}
