package com.tools.il2fusion.config

import androidx.annotation.StringRes
import com.tools.il2fusion.R
import org.json.JSONArray
import org.json.JSONObject

data class CocosLuaReplacementRule(
    val enabled: Boolean = true,
    val scriptName: String = "",
    val replacedCode: String = "",
    val replaceCode: String = "",
    val prependCode: String = ""
)

enum class CocosBundledFont(
    val id: String,
    @StringRes val displayNameRes: Int,
    val assetPath: String,
    val fileName: String
) {
    NotoSans(
        "noto_sans",
        R.string.cocos_font_noto_sans,
        "fonts/noto_sans_regular.ttf",
        "noto_sans_regular.ttf"
    ),
    NotoSansThai(
        "noto_sans_thai",
        R.string.cocos_font_noto_sans_thai,
        "fonts/noto_sans_thai_regular.ttf",
        "noto_sans_thai_regular.ttf"
    ),
    NotoSansSC(
        "noto_sans_sc",
        R.string.cocos_font_noto_sans_sc,
        "fonts/noto_sans_sc_regular.ttf",
        "noto_sans_sc_regular.ttf"
    ),
    NotoSansTC(
        "noto_sans_tc",
        R.string.cocos_font_noto_sans_tc,
        "fonts/noto_sans_tc_regular.ttf",
        "noto_sans_tc_regular.ttf"
    ),
    NotoSansJP(
        "noto_sans_jp",
        R.string.cocos_font_noto_sans_jp,
        "fonts/noto_sans_jp_regular.ttf",
        "noto_sans_jp_regular.ttf"
    ),
    NotoSansKR(
        "noto_sans_kr",
        R.string.cocos_font_noto_sans_kr,
        "fonts/noto_sans_kr_regular.ttf",
        "noto_sans_kr_regular.ttf"
    );

    companion object {
        fun fromId(id: String): CocosBundledFont? {
            return entries.firstOrNull { it.id == id }
        }
    }
}

data class RuntimeHookConfig(
    val engine: GameEngine = GameEngine.UnityIl2Cpp,
    val textReplacementEnabled: Boolean = false,
    val textReplacementValue: String = RuntimeTextReplacementDefaults.DEFAULT_TEXT,
    val textDbResetOnStart: Boolean = false,
    val cocosTextCaptureEnabled: Boolean = true,
    val cocosTextPersistEnabled: Boolean = true,
    val cocosTextPersistChineseOnly: Boolean = true,
    val cocosTextReplacementDelayEnabled: Boolean = false,
    val cocosTextReplacementDelayMs: Int = RuntimeTextReplacementDefaults.DEFAULT_COCOS_DELAY_MS,
    val cocosTypewriterOptimizationEnabled: Boolean = false,
    val cocosTypewriterIdleFinalizeMs: Int = RuntimeTextReplacementDefaults.DEFAULT_TYPEWRITER_IDLE_FINALIZE_MS,
    val cocosFontId: String = "",
    val cocosLuaReplacementRules: List<CocosLuaReplacementRule> = emptyList()
) {
    val luaReplacementRuleCount: Int get() = cocosLuaReplacementRules.size
}

object RuntimeTextReplacementDefaults {
    const val DEFAULT_TEXT = "测试testการทดสอบ"
    const val DEFAULT_COCOS_DELAY_MS = 300
    const val MAX_COCOS_DELAY_MS = 5_000
    const val DEFAULT_TYPEWRITER_IDLE_FINALIZE_MS = 250

    fun clampCocosDelayMs(value: Int): Int {
        return value.coerceIn(0, MAX_COCOS_DELAY_MS)
    }

    fun clampTypewriterIdleFinalizeMs(value: Int): Int {
        return value.coerceIn(50, 5_000)
    }
}

object CocosLuaRulesCodec {
    fun parseLuaReplacementRules(json: String): List<CocosLuaReplacementRule> {
        if (json.isBlank()) {
            return emptyList()
        }
        val trimmed = json.trim()
        return runCatching {
            if (trimmed.startsWith("[")) {
                parseRuleArray(JSONArray(trimmed))
            } else {
                val normalized = if (trimmed.startsWith("{")) trimmed else "{$trimmed}"
                parseRulesFromObject(JSONObject(normalized))
            }
        }.getOrDefault(emptyList())
    }

    fun decodeRules(json: String): List<CocosLuaReplacementRule> {
        if (json.isBlank()) {
            return emptyList()
        }
        return runCatching {
            parseRuleArray(JSONArray(json))
        }.getOrDefault(emptyList())
    }

    fun encodeRules(rules: List<CocosLuaReplacementRule>): String {
        return JSONArray(rules.map(::ruleToJson)).toString()
    }

    fun ruleToJson(rule: CocosLuaReplacementRule): JSONObject {
        return JSONObject()
            .put("enabled", rule.enabled)
            .put("scriptName", rule.scriptName)
            .put("replacedCode", rule.replacedCode)
            .put("replaceCode", rule.replaceCode)
            .put("prependCode", rule.prependCode)
    }
}

private fun parseRulesFromObject(root: JSONObject): List<CocosLuaReplacementRule> {
    val cocos = root.optJSONObject("cocos")
    val lua = cocos?.optJSONObject("lua")
    val source = cocos?.optJSONArray("luaReplacementRules")
        ?: lua?.optJSONArray("replacementRules")
        ?: root.optJSONObject("config")
            ?.optJSONObject("lua_replace")
            ?.optJSONArray("replace_items")
        ?: root.optJSONObject("lua_replace")
            ?.optJSONArray("replace_items")
        ?: root.optJSONArray("replace_items")
        ?: return emptyList()
    return parseRuleArray(source)
}

private fun parseRuleArray(source: JSONArray): List<CocosLuaReplacementRule> {
    val rules = mutableListOf<CocosLuaReplacementRule>()
    for (index in 0 until source.length()) {
        val obj = source.optJSONObject(index) ?: continue
        val rule = CocosLuaReplacementRule(
            enabled = optBoolean(obj, "enabled") ?: true,
            scriptName = obj.optString("scriptName", obj.optString("lua_script_name", "")).trim(),
            replacedCode = obj.optString("replacedCode", obj.optString("replaced_code", "")),
            replaceCode = obj.optString("replaceCode", obj.optString("replace_code", "")),
            prependCode = obj.optString("prependCode", obj.optString("prepend_code", ""))
        )
        if (rule.scriptName.isNotBlank() ||
            rule.replacedCode.isNotBlank() ||
            rule.replaceCode.isNotBlank() ||
            rule.prependCode.isNotBlank()
        ) {
            rules.add(rule)
        }
    }
    return rules
}

private fun optBoolean(obj: JSONObject?, key: String): Boolean? {
    if (obj == null || !obj.has(key)) {
        return null
    }
    return when (val value = obj.opt(key)) {
        is Boolean -> value
        is Number -> value.toInt() != 0
        is String -> value == "1" || value.equals("true", ignoreCase = true)
        else -> null
    }
}

object CocosLuaRulesImporter {
    fun extractLuaReplacementRules(text: String): List<CocosLuaReplacementRule> {
        return CocosLuaRulesCodec.parseLuaReplacementRules(text)
    }
}
