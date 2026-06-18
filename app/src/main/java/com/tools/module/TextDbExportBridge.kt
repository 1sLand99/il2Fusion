package com.tools.module

import android.content.Context
import android.util.Log
import com.tools.il2fusion.config.ConfigContentProvider
import com.tools.il2fusion.config.HookConfigStore
import com.tools.il2fusion.config.TextDbExportJson
import com.tools.il2fusion.config.TextDbExportPaths
import com.tools.il2fusion.config.TextDbExportRequest
import com.tools.il2fusion.config.TextDbExportStatus
import de.robv.android.xposed.XposedBridge
import java.io.File
import java.io.FileInputStream
import java.util.concurrent.atomic.AtomicBoolean

object TextDbExportBridge {
    private const val TAG = "[il2Fusion]"
    private const val POLL_INTERVAL_MS = 2_000L
    private val started = AtomicBoolean(false)

    fun start(ctx: Context, packageName: String, processName: String) {
        if (!isMainProcess(packageName, processName)) {
            XposedBridge.log("$TAG text.db export bridge skipped for non-main process $processName")
            return
        }
        if (!started.compareAndSet(false, true)) {
            return
        }
        val appContext = ctx.applicationContext ?: ctx
        Thread({
            Log.i(TAG, "text.db export bridge started for $packageName")
            XposedBridge.log("$TAG text.db export bridge started for $packageName")
            var lastHandledRequestId = ""
            while (true) {
                try {
                    val request = HookConfigStore.loadTextDbExportRequestForHook(appContext)
                    val status = HookConfigStore.loadTextDbExportStatusForHook(appContext)
                    if (shouldHandle(request, status, packageName, lastHandledRequestId)) {
                        lastHandledRequestId = request.requestId
                        handleExport(appContext, packageName, request)
                    }
                    Thread.sleep(POLL_INTERVAL_MS)
                } catch (t: Throwable) {
                    Log.w(TAG, "text.db export bridge error: ${t.message}", t)
                    XposedBridge.log("$TAG text.db export bridge error: $t")
                    Thread.sleep(POLL_INTERVAL_MS)
                }
            }
        }, "il2fusion-db-export").apply {
            isDaemon = true
            start()
        }
    }

    private fun isMainProcess(packageName: String, processName: String): Boolean {
        return processName.isBlank() || processName == packageName
    }

    private fun shouldHandle(
        request: TextDbExportRequest,
        status: TextDbExportStatus,
        packageName: String,
        lastHandledRequestId: String
    ): Boolean {
        if (!request.isValid || request.requestId == lastHandledRequestId) {
            return false
        }
        if (status.requestId == request.requestId && isTerminalStatus(status.state)) {
            return false
        }
        return request.targetPackage == packageName
    }

    private fun isTerminalStatus(state: String): Boolean {
        return state == TextDbExportJson.STATE_SUCCESS ||
            state == TextDbExportJson.STATE_FAILURE
    }

    private fun handleExport(ctx: Context, packageName: String, request: TextDbExportRequest) {
        saveStatus(
            ctx,
            request,
            TextDbExportJson.STATE_RUNNING,
            "Target process is exporting text.db.",
            ""
        )

        val source = File(ctx.dataDir, "text.db")
        if (!source.exists() || !source.isFile) {
            saveStatus(
                ctx,
                request,
                TextDbExportJson.STATE_FAILURE,
                "text.db not found in /data/data/$packageName.",
                ""
            )
            return
        }

        val fileName = request.fileName.ifBlank {
            TextDbExportPaths.defaultFileName(packageName)
        }
        val safeName = TextDbExportPaths.sanitizeFileName(fileName)
        val exportUri = ConfigContentProvider.exportFileUri(request.requestId, safeName)

        try {
            FileInputStream(source).use { input ->
                val output = ctx.contentResolver.openOutputStream(exportUri, "w")
                    ?: throw IllegalStateException("open export output stream returned null")
                output.use { out ->
                    input.copyTo(out)
                }
            }
            val exportPath = TextDbExportPaths.expectedExternalPath(safeName)
            saveStatus(
                ctx,
                request,
                TextDbExportJson.STATE_SUCCESS,
                "Exported text.db.",
                exportPath
            )
            Log.i(TAG, "text.db exported for $packageName -> $exportPath")
            XposedBridge.log("$TAG text.db exported for $packageName -> $exportPath")
        } catch (t: Throwable) {
            saveStatus(
                ctx,
                request,
                TextDbExportJson.STATE_FAILURE,
                "Export failed: ${t.message ?: t.javaClass.simpleName}",
                ""
            )
            Log.w(TAG, "text.db export failed for $packageName: ${t.message}", t)
            XposedBridge.log("$TAG text.db export failed for $packageName: $t")
        }
    }

    private fun saveStatus(
        ctx: Context,
        request: TextDbExportRequest,
        state: String,
        message: String,
        exportPath: String
    ) {
        HookConfigStore.saveTextDbExportStatus(
            ctx,
            TextDbExportStatus(
                requestId = request.requestId,
                targetPackage = request.targetPackage,
                state = state,
                message = message,
                exportPath = exportPath,
                updatedAt = System.currentTimeMillis()
            )
        )
    }
}
