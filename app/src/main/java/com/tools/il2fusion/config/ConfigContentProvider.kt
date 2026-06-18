package com.tools.il2fusion.config

import android.content.ContentProvider
import android.content.ContentValues
import android.content.UriMatcher
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.Binder
import android.os.ParcelFileDescriptor
import android.os.Process
import android.util.Log
import java.io.File

class ConfigContentProvider : ContentProvider() {

    companion object {
        private const val TAG = "[il2Fusion]"
        const val AUTHORITY = "com.tools.il2fusion.provider"
        private const val PATH_CONFIG = "config"
        private const val PATH_EXPORTS = "exports"
        const val KEY_TARGETS = "targets"
        const val KEY_DUMP_MODE = "dump_mode"
        const val KEY_HOOK_FRAMEWORK = "hook_framework"
        const val KEY_TARGETS_JSON = "targets_json"
        const val KEY_GAME_ENGINE = "game_engine"
        const val KEY_TEXT_REPLACEMENT_ENABLED = "text_replacement_enabled"
        const val KEY_TEXT_REPLACEMENT_VALUE = "text_replacement_value"
        const val KEY_TEXT_DB_RESET_ON_START = "text_db_reset_on_start"
        const val KEY_COCOS_TEXT_CAPTURE_ENABLED = "cocos_text_capture_enabled"
        const val KEY_COCOS_TEXT_PERSIST_ENABLED = "cocos_text_persist_enabled"
        const val KEY_COCOS_TEXT_PERSIST_CHINESE_ONLY = "cocos_text_persist_chinese_only"
        const val KEY_COCOS_TEXT_REPLACEMENT_DELAY_ENABLED = "cocos_text_replacement_delay_enabled"
        const val KEY_COCOS_TEXT_REPLACEMENT_DELAY_MS = "cocos_text_replacement_delay_ms"
        const val KEY_COCOS_TYPEWRITER_OPTIMIZATION_ENABLED = "cocos_typewriter_optimization_enabled"
        const val KEY_COCOS_TYPEWRITER_IDLE_FINALIZE_MS = "cocos_typewriter_idle_finalize_ms"
        const val KEY_COCOS_FONT_ID = "cocos_font_id"
        const val KEY_COCOS_LUA_RULES = "cocos_lua_rules"
        const val KEY_TEXT_DB_EXPORT_REQUEST = "text_db_export_request"
        const val KEY_TEXT_DB_EXPORT_STATUS = "text_db_export_status"
        const val KEY_TARGET_SESSIONS = "target_sessions"
        const val KEY_TARGET_SESSION_REPORT = "target_session_report"
        private const val CODE_CONFIG = 1
        private const val CODE_EXPORT_FILE = 2
        val CONTENT_URI: Uri = Uri.parse("content://$AUTHORITY/$PATH_CONFIG")
        val EXPORTS_URI: Uri = Uri.parse("content://$AUTHORITY/$PATH_EXPORTS")

        private val uriMatcher = UriMatcher(UriMatcher.NO_MATCH).apply {
            addURI(AUTHORITY, PATH_CONFIG, CODE_CONFIG)
            addURI(AUTHORITY, "$PATH_EXPORTS/*", CODE_EXPORT_FILE)
        }

        fun exportFileUri(requestId: String, fileName: String): Uri {
            return EXPORTS_URI.buildUpon()
                .appendPath(requestId)
                .appendQueryParameter("name", fileName)
                .build()
        }
    }

    private val columns = arrayOf("key", "value")

    override fun onCreate(): Boolean {
        return true
    }

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?
    ): Cursor? {
        if (uriMatcher.match(uri) != CODE_CONFIG) return null
        val prefs = context?.getSharedPreferences(PATH_CONFIG, android.content.Context.MODE_PRIVATE) ?: return null
        val all = prefs.getAll()
        val cursor = MatrixCursor(columns)
        all.forEach { (k, v) ->
            cursor.addRow(arrayOf(k, v?.toString() ?: ""))
        }
        return cursor
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? {
        if (uriMatcher.match(uri) != CODE_CONFIG) return null
        val prefs = context?.getSharedPreferences(PATH_CONFIG, android.content.Context.MODE_PRIVATE) ?: return null
        val key = values?.getAsString("key") ?: KEY_TARGETS
        val value = values?.getAsString("value") ?: ""
        if (key == KEY_TARGET_SESSION_REPORT) {
            return handleTargetSessionReport(value)
        }
        prefs.edit().putString(key, value).apply()
        Log.i(TAG, "ConfigContentProvider insert key=$key")
        return Uri.withAppendedPath(CONTENT_URI, key)
    }

    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<out String>?
    ): Int {
        if (uriMatcher.match(uri) != CODE_CONFIG) return 0
        val prefs = context?.getSharedPreferences(PATH_CONFIG, android.content.Context.MODE_PRIVATE) ?: return 0
        val key = values?.getAsString("key") ?: KEY_TARGETS
        val value = values?.getAsString("value") ?: ""
        if (key == KEY_TARGET_SESSION_REPORT) {
            return if (handleTargetSessionReport(value) != null) 1 else 0
        }
        prefs.edit().putString(key, value).apply()
        Log.i(TAG, "ConfigContentProvider update key=$key")
        return 1
    }

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0

    override fun getType(uri: Uri): String? = null

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor? {
        if (uriMatcher.match(uri) != CODE_EXPORT_FILE) return null
        if (!mode.contains("w")) return null
        val ctx = context ?: return null
        val requestId = uri.lastPathSegment?.takeIf { it.isNotBlank() } ?: return null
        val prefs = ctx.getSharedPreferences(PATH_CONFIG, android.content.Context.MODE_PRIVATE)
        val request = TextDbExportJson.decodeRequest(
            prefs.getString(KEY_TEXT_DB_EXPORT_REQUEST, "") ?: ""
        )
        if (!request.isValid || request.requestId != requestId || !isCallerAllowedForPackage(request.targetPackage)) {
            Log.w(TAG, "ConfigContentProvider rejected export file request=$requestId")
            return null
        }
        val fileName = TextDbExportPaths.sanitizeFileName(
            request.fileName.ifBlank {
                TextDbExportPaths.defaultFileName(request.targetPackage)
            }
        )
        val exportDir = ctx.getExternalFilesDir(TextDbExportPaths.EXPORT_DIR)
            ?: return null
        if (!exportDir.exists() && !exportDir.mkdirs()) {
            Log.w(TAG, "ConfigContentProvider failed to create export dir=${exportDir.absolutePath}")
            return null
        }
        val outFile = File(exportDir, fileName)
        Log.i(TAG, "ConfigContentProvider open export file=${outFile.absolutePath}")
        return ParcelFileDescriptor.open(
            outFile,
            ParcelFileDescriptor.MODE_CREATE or
                ParcelFileDescriptor.MODE_TRUNCATE or
                ParcelFileDescriptor.MODE_WRITE_ONLY
        )
    }

    private fun handleTargetSessionReport(value: String): Uri? {
        val ctx = context ?: return null
        val prefs = ctx.getSharedPreferences(PATH_CONFIG, android.content.Context.MODE_PRIVATE)
        val session = TargetSessionJson.decodeSession(value)
        if (!session.isValid || !isCallerAllowedForSession(session)) {
            Log.w(TAG, "ConfigContentProvider rejected target session package=${session.packageName}")
            return null
        }
        val existing = TargetSessionJson.decodeSessions(prefs.getString(KEY_TARGET_SESSIONS, "") ?: "")
        val now = System.currentTimeMillis()
        val updated = TargetSessionJson.upsertSession(existing, session, now)
        prefs.edit()
            .putString(KEY_TARGET_SESSIONS, TargetSessionJson.encodeSessions(updated))
            .apply()
        Log.i(TAG, "ConfigContentProvider target session ${session.packageName}/${session.processName}")
        return Uri.withAppendedPath(CONTENT_URI, KEY_TARGET_SESSIONS)
    }

    private fun isCallerAllowedForSession(session: TargetSession): Boolean {
        return isCallerAllowedForPackage(session.packageName)
    }

    private fun isCallerAllowedForPackage(packageName: String): Boolean {
        val ctx = context ?: return false
        val callingUid = Binder.getCallingUid()
        if (callingUid == Process.myUid()) {
            return true
        }
        val packages = ctx.packageManager.getPackagesForUid(callingUid) ?: return false
        return packages.contains(packageName)
    }
}
