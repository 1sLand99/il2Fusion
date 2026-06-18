package com.tools.module

import android.content.Context
import android.os.Process
import android.util.Log
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.config.HookConfigStore
import com.tools.il2fusion.config.TargetSession
import com.tools.il2fusion.config.TargetSessionTiming
import de.robv.android.xposed.XposedBridge
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

object TargetSessionBridge {
    private const val TAG = "[il2Fusion]"
    private val started = AtomicBoolean(false)

    @Volatile
    private var currentEngine: GameEngine = GameEngine.UnityIl2Cpp

    @Volatile
    private var startedAt: Long = 0L

    fun start(
        ctx: Context,
        packageName: String,
        processName: String,
        engine: GameEngine
    ) {
        currentEngine = engine
        val appContext = ctx.applicationContext ?: ctx
        if (startedAt == 0L) {
            startedAt = System.currentTimeMillis()
        }
        report(appContext, packageName, processName)
        if (!started.compareAndSet(false, true)) {
            return
        }
        Thread({
            Log.i(TAG, "target session heartbeat started for $packageName/$processName")
            XposedBridge.log("$TAG target session heartbeat started for $packageName/$processName")
            while (true) {
                try {
                    report(appContext, packageName, processName)
                    Thread.sleep(TargetSessionTiming.HEARTBEAT_INTERVAL_MS)
                } catch (t: Throwable) {
                    Log.w(TAG, "target session heartbeat failed: ${t.message}", t)
                    XposedBridge.log("$TAG target session heartbeat failed: $t")
                    Thread.sleep(TargetSessionTiming.HEARTBEAT_INTERVAL_MS)
                }
            }
        }, "il2fusion-target-session").apply {
            isDaemon = true
            start()
        }
    }

    private fun report(ctx: Context, packageName: String, processName: String) {
        val dataDir = ctx.dataDir?.absolutePath ?: ""
        val now = System.currentTimeMillis()
        HookConfigStore.reportTargetSession(
            ctx,
            TargetSession(
                packageName = packageName,
                processName = processName.ifBlank { packageName },
                pid = Process.myPid(),
                dataDir = dataDir,
                engine = currentEngine,
                startedAt = startedAt.takeIf { it > 0L } ?: now,
                lastHeartbeatAt = now,
                textDbExists = File(dataDir, "text.db").isFile,
                dumpCsExists = File(File(dataDir, "files"), "dump.cs").isFile
            )
        )
    }
}
