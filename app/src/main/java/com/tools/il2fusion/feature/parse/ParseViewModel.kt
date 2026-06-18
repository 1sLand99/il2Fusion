package com.tools.il2fusion.feature.parse

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.tools.il2fusion.config.CocosLuaReplacementRule
import com.tools.il2fusion.R
import com.tools.il2fusion.config.CocosLuaRulesImporter
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.config.HookConfigChangeBus
import com.tools.il2fusion.config.HookConfigRepository
import com.tools.il2fusion.utils.DumpFileParser
import com.tools.il2fusion.utils.HookTargetUtils
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch

class ParseViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = HookConfigRepository(application)
    private val dumpFileParser = DumpFileParser()

    private val _uiState = MutableStateFlow(ParseUiState())
    val uiState: StateFlow<ParseUiState> = _uiState.asStateFlow()

    private val _events = Channel<String>(Channel.BUFFERED)
    val events = _events.receiveAsFlow()

    init {
        refresh()
        observeConfigChanges()
    }

    private fun observeConfigChanges() {
        viewModelScope.launch {
            HookConfigChangeBus.changes.collect {
                refresh()
            }
        }
    }

    fun onFilePicked(uri: Uri?) {
        if (uri == null) {
            viewModelScope.launch {
                _events.send(getApplication<Application>().getString(R.string.message_no_file_selected))
            }
            return
        }
        viewModelScope.launch {
            val payload = repository.loadConfig()
            when (payload.runtimeConfig.engine) {
                GameEngine.UnityIl2Cpp -> Unit
                GameEngine.Cocos2dxLua -> {
                    importCocosLuaRules(uri)
                    return@launch
                }
            }
            _uiState.value = _uiState.value.copy(isLoading = true)
            try {
                val result = dumpFileParser.extractTargets(getApplication(), uri, Int.MAX_VALUE)
                val methods = HookTargetUtils.normalizeInputs(result.entries.map { it.functionName })
                if (methods.isEmpty()) {
                    _events.send(getApplication<Application>().getString(R.string.message_parse_empty))
                    return@launch
                }
                repository.saveTargets(methods)
                if (result.jsonText.isNotBlank()) {
                    repository.saveTargetsJson(result.jsonText)
                }
                _events.send(
                    getApplication<Application>().getString(R.string.message_parse_success, methods.size)
                )
                result.savedJsonPath?.let { path ->
                    _events.send(
                        getApplication<Application>().getString(R.string.message_json_saved, path)
                    )
                }
                refresh()
            } finally {
                _uiState.value = _uiState.value.copy(isLoading = false)
            }
        }
    }

    private suspend fun importCocosLuaRules(uri: Uri) {
        _uiState.value = _uiState.value.copy(isLoading = true)
        try {
            val raw = getApplication<Application>()
                .contentResolver
                .openInputStream(uri)
                ?.bufferedReader()
                ?.use { it.readText() }
                .orEmpty()
            val importedRules = CocosLuaRulesImporter.extractLuaReplacementRules(raw)
            if (importedRules.isEmpty()) {
                _events.send(getApplication<Application>().getString(R.string.message_cocos_lua_rules_import_failed))
                return
            }
            val payload = repository.loadConfig()
            val rules = payload.runtimeConfig.cocosLuaReplacementRules + importedRules
            saveLuaRules(rules)
            _events.send(
                getApplication<Application>().getString(
                    R.string.message_cocos_lua_rules_import_success,
                    importedRules.size
                )
            )
            refresh()
        } finally {
            _uiState.value = _uiState.value.copy(isLoading = false)
        }
    }

    fun onLuaRuleScriptNameChanged(value: String) {
        _uiState.value = _uiState.value.copy(luaRuleScriptName = value)
    }

    fun onLuaRuleReplacedCodeChanged(value: String) {
        _uiState.value = _uiState.value.copy(luaRuleReplacedCode = value)
    }

    fun onLuaRuleReplaceCodeChanged(value: String) {
        _uiState.value = _uiState.value.copy(luaRuleReplaceCode = value)
    }

    fun onLuaRulePrependCodeChanged(value: String) {
        _uiState.value = _uiState.value.copy(luaRulePrependCode = value)
    }

    fun onEditLuaRule(index: Int) {
        val rule = _uiState.value.runtimeConfig.cocosLuaReplacementRules.getOrNull(index) ?: return
        _uiState.value = _uiState.value.copy(
            editingLuaRuleIndex = index,
            luaRuleScriptName = rule.scriptName,
            luaRuleReplacedCode = rule.replacedCode,
            luaRuleReplaceCode = rule.replaceCode,
            luaRulePrependCode = rule.prependCode
        )
    }

    fun onCancelLuaRuleEdit() {
        clearLuaRuleForm()
    }

    fun onSaveLuaRule() {
        viewModelScope.launch {
            val state = _uiState.value
            val rule = CocosLuaReplacementRule(
                enabled = true,
                scriptName = state.luaRuleScriptName.trim(),
                replacedCode = state.luaRuleReplacedCode,
                replaceCode = state.luaRuleReplaceCode,
                prependCode = state.luaRulePrependCode
            )
            if (!isValidRule(rule)) {
                _events.send(getApplication<Application>().getString(R.string.message_cocos_lua_rule_invalid))
                return@launch
            }
            val payload = repository.loadConfig()
            val rules = payload.runtimeConfig.cocosLuaReplacementRules.toMutableList()
            if (state.editingLuaRuleIndex in rules.indices) {
                val previous = rules[state.editingLuaRuleIndex]
                rules[state.editingLuaRuleIndex] = rule.copy(enabled = previous.enabled)
            } else {
                rules.add(rule)
            }
            saveLuaRules(rules)
            clearLuaRuleForm()
            _events.send(getApplication<Application>().getString(R.string.message_cocos_lua_rule_saved))
            refresh()
        }
    }

    fun onToggleLuaRule(index: Int, enabled: Boolean) {
        viewModelScope.launch {
            val payload = repository.loadConfig()
            val rules = payload.runtimeConfig.cocosLuaReplacementRules.toMutableList()
            val current = rules.getOrNull(index) ?: return@launch
            rules[index] = current.copy(enabled = enabled)
            saveLuaRules(rules)
            refresh()
        }
    }

    fun onDeleteLuaRule(index: Int) {
        viewModelScope.launch {
            val payload = repository.loadConfig()
            val rules = payload.runtimeConfig.cocosLuaReplacementRules.toMutableList()
            if (index !in rules.indices) {
                return@launch
            }
            rules.removeAt(index)
            saveLuaRules(rules)
            clearLuaRuleForm()
            _events.send(getApplication<Application>().getString(R.string.message_cocos_lua_rule_deleted))
            refresh()
        }
    }

    private suspend fun saveLuaRules(rules: List<CocosLuaReplacementRule>) {
        repository.saveCocosLuaReplacementRules(rules)
    }

    private fun isValidRule(rule: CocosLuaReplacementRule): Boolean {
        return rule.scriptName.isNotBlank() &&
            (rule.replacedCode.isNotBlank() || rule.prependCode.isNotBlank())
    }

    private fun clearLuaRuleForm() {
        _uiState.value = _uiState.value.copy(
            editingLuaRuleIndex = -1,
            luaRuleScriptName = "",
            luaRuleReplacedCode = "",
            luaRuleReplaceCode = "",
            luaRulePrependCode = ""
        )
    }

    fun onSave() {
        viewModelScope.launch {
            val cleaned = HookTargetUtils.normalizeInputs(_uiState.value.methods)
            if (cleaned.isEmpty()) {
                _events.send(getApplication<Application>().getString(R.string.message_parse_save_empty))
                return@launch
            }
            repository.saveTargets(cleaned)
            _events.send(
                getApplication<Application>().getString(R.string.message_save_success, cleaned.size)
            )
            refresh()
        }
    }

    private fun refresh() {
        viewModelScope.launch {
            val payload = repository.loadConfig()
            _uiState.value = ParseUiState(
                methods = HookTargetUtils.formatInputs(payload.targets),
                savedCount = payload.targets.size,
                hasTargetsJson = payload.targetsJson.isNotBlank(),
                runtimeConfig = payload.runtimeConfig
            )
        }
    }
}
