package com.tools.il2fusion.feature.parse

import com.tools.il2fusion.config.RuntimeHookConfig

data class ParseUiState(
    val isLoading: Boolean = false,
    val methods: List<String> = emptyList(),
    val savedCount: Int = 0,
    val hasTargetsJson: Boolean = false,
    val runtimeConfig: RuntimeHookConfig = RuntimeHookConfig(),
    val editingLuaRuleIndex: Int = -1,
    val luaRuleScriptName: String = "",
    val luaRuleReplacedCode: String = "",
    val luaRuleReplaceCode: String = "",
    val luaRulePrependCode: String = ""
)
