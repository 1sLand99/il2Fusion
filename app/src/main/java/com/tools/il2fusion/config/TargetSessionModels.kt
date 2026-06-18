package com.tools.il2fusion.config

import org.json.JSONArray
import org.json.JSONObject

data class TargetSession(
    val packageName: String = "",
    val processName: String = "",
    val pid: Int = 0,
    val dataDir: String = "",
    val engine: GameEngine = GameEngine.UnityIl2Cpp,
    val startedAt: Long = 0L,
    val lastHeartbeatAt: Long = 0L,
    val textDbExists: Boolean = false,
    val dumpCsExists: Boolean = false
) {
    val isValid: Boolean get() = packageName.isNotBlank() && processName.isNotBlank()

    fun statusAt(nowMillis: Long): TargetSessionStatus {
        val delta = nowMillis - lastHeartbeatAt
        return when {
            delta <= TargetSessionTiming.STALE_AFTER_MS -> TargetSessionStatus.Online
            delta <= TargetSessionTiming.OFFLINE_AFTER_MS -> TargetSessionStatus.Stale
            else -> TargetSessionStatus.Offline
        }
    }
}

enum class TargetSessionStatus {
    Online,
    Stale,
    Offline
}

object TargetSessionTiming {
    const val HEARTBEAT_INTERVAL_MS = 3_000L
    const val STALE_AFTER_MS = 10_000L
    const val OFFLINE_AFTER_MS = 30_000L
    const val PRUNE_AFTER_MS = 10 * 60_000L
}

object TargetSessionJson {
    fun encodeSession(session: TargetSession): String {
        return sessionToJson(session).toString()
    }

    fun decodeSession(json: String): TargetSession {
        if (json.isBlank()) {
            return TargetSession()
        }
        return runCatching {
            jsonToSession(JSONObject(json))
        }.getOrDefault(TargetSession())
    }

    fun encodeSessions(sessions: List<TargetSession>): String {
        return JSONArray(sessions.map(::sessionToJson)).toString()
    }

    fun decodeSessions(json: String): List<TargetSession> {
        if (json.isBlank()) {
            return emptyList()
        }
        return runCatching {
            val array = JSONArray(json)
            buildList {
                for (index in 0 until array.length()) {
                    val session = array.optJSONObject(index)?.let(::jsonToSession) ?: continue
                    if (session.isValid) {
                        add(session)
                    }
                }
            }
        }.getOrDefault(emptyList())
    }

    fun upsertSession(
        sessions: List<TargetSession>,
        incoming: TargetSession,
        nowMillis: Long
    ): List<TargetSession> {
        val cutoff = nowMillis - TargetSessionTiming.PRUNE_AFTER_MS
        return (sessions
            .filterNot { it.packageName == incoming.packageName && it.processName == incoming.processName }
            .filter { it.lastHeartbeatAt >= cutoff } + incoming)
            .sortedByDescending { it.lastHeartbeatAt }
    }

    private fun sessionToJson(session: TargetSession): JSONObject {
        return JSONObject()
            .put("packageName", session.packageName)
            .put("processName", session.processName)
            .put("pid", session.pid)
            .put("dataDir", session.dataDir)
            .put("engine", session.engine.storageValue)
            .put("startedAt", session.startedAt)
            .put("lastHeartbeatAt", session.lastHeartbeatAt)
            .put("textDbExists", session.textDbExists)
            .put("dumpCsExists", session.dumpCsExists)
    }

    private fun jsonToSession(root: JSONObject): TargetSession {
        return TargetSession(
            packageName = root.optString("packageName", ""),
            processName = root.optString("processName", ""),
            pid = root.optInt("pid", 0),
            dataDir = root.optString("dataDir", ""),
            engine = GameEngine.fromStorageValue(root.optString("engine", "")),
            startedAt = root.optLong("startedAt", 0L),
            lastHeartbeatAt = root.optLong("lastHeartbeatAt", 0L),
            textDbExists = root.optBoolean("textDbExists", false),
            dumpCsExists = root.optBoolean("dumpCsExists", false)
        )
    }
}
