package com.tools.il2fusion.config

import org.json.JSONObject

data class TextDbExportRequest(
    val requestId: String = "",
    val targetPackage: String = "",
    val fileName: String = ""
) {
    val isValid: Boolean get() = requestId.isNotBlank() && targetPackage.isNotBlank()
}

data class TextDbExportStatus(
    val requestId: String = "",
    val targetPackage: String = "",
    val state: String = "",
    val message: String = "",
    val exportPath: String = "",
    val updatedAt: Long = 0L
)

object TextDbExportJson {
    const val STATE_PENDING = "pending"
    const val STATE_RUNNING = "running"
    const val STATE_SUCCESS = "success"
    const val STATE_FAILURE = "failure"

    fun encodeRequest(request: TextDbExportRequest): String {
        return JSONObject()
            .put("requestId", request.requestId)
            .put("targetPackage", request.targetPackage)
            .put("fileName", request.fileName)
            .toString()
    }

    fun decodeRequest(json: String): TextDbExportRequest {
        if (json.isBlank()) {
            return TextDbExportRequest()
        }
        return runCatching {
            val root = JSONObject(json)
            TextDbExportRequest(
                requestId = root.optString("requestId", ""),
                targetPackage = root.optString("targetPackage", ""),
                fileName = root.optString("fileName", "")
            )
        }.getOrDefault(TextDbExportRequest())
    }

    fun encodeStatus(status: TextDbExportStatus): String {
        return JSONObject()
            .put("requestId", status.requestId)
            .put("targetPackage", status.targetPackage)
            .put("state", status.state)
            .put("message", status.message)
            .put("exportPath", status.exportPath)
            .put("updatedAt", status.updatedAt)
            .toString()
    }

    fun decodeStatus(json: String): TextDbExportStatus {
        if (json.isBlank()) {
            return TextDbExportStatus()
        }
        return runCatching {
            val root = JSONObject(json)
            TextDbExportStatus(
                requestId = root.optString("requestId", ""),
                targetPackage = root.optString("targetPackage", ""),
                state = root.optString("state", ""),
                message = root.optString("message", ""),
                exportPath = root.optString("exportPath", ""),
                updatedAt = root.optLong("updatedAt", 0L)
            )
        }.getOrDefault(TextDbExportStatus())
    }
}

object TextDbExportPaths {
    const val EXPORT_DIR = "exports"

    fun defaultFileName(targetPackage: String): String {
        val safePackage = sanitizeFileStem(targetPackage, fallback = "target", maxLength = 92)
        return "$safePackage.db"
    }

    fun sanitizeFileName(raw: String): String {
        val cleaned = sanitizeFileStem(raw, fallback = "", maxLength = 96)
        return cleaned.ifBlank { "text.db" }
    }

    private fun sanitizeFileStem(raw: String, fallback: String, maxLength: Int): String {
        val cleaned = raw.trim()
            .map { ch ->
                if (ch.isLetterOrDigit() || ch == '.' || ch == '_' || ch == '-') ch else '_'
            }
            .joinToString("")
            .trim('_')
            .take(maxLength)
        return cleaned.ifBlank { fallback }
    }

    fun expectedExternalPath(fileName: String): String {
        return "/sdcard/Android/data/com.tools.il2fusion/files/$EXPORT_DIR/${sanitizeFileName(fileName)}"
    }
}
