package com.tools.il2fusion.feature.mode

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.tools.il2fusion.R
import com.tools.il2fusion.config.CocosBundledFont
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.config.HookConfigChangeBus
import com.tools.il2fusion.config.HookConfigRepository
import com.tools.il2fusion.config.HookFramework
import com.tools.il2fusion.config.RuntimeTextReplacementDefaults
import com.tools.il2fusion.feature.common.toSummaryUiModel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class ModeViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = HookConfigRepository(application)

    private val _uiState = MutableStateFlow(ModeUiState())
    val uiState: StateFlow<ModeUiState> = _uiState.asStateFlow()

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

    fun onDumpModeChanged(enabled: Boolean) {
        viewModelScope.launch {
            repository.saveDumpMode(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) R.string.message_dump_mode_enabled else R.string.message_dump_mode_disabled
                )
            )
        }
    }

    fun onHookFrameworkChanged(useDobby: Boolean) {
        viewModelScope.launch {
            val framework = if (useDobby) HookFramework.Dobby else HookFramework.And64InlineHook
            repository.saveHookFramework(framework)
            _events.send(
                getApplication<Application>().getString(
                    if (framework == HookFramework.Dobby) {
                        R.string.message_hook_framework_dobby
                    } else {
                        R.string.message_hook_framework_and64
                    }
                )
            )
        }
    }

    fun onEngineChanged(engine: GameEngine) {
        viewModelScope.launch {
            repository.saveGameEngine(engine)
            _events.send(
                getApplication<Application>().getString(
                    R.string.message_runtime_engine_switched,
                    getApplication<Application>().getString(engine.displayNameRes)
                )
            )
        }
    }

    fun onTextReplacementChanged(enabled: Boolean) {
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(textReplacementEnabled = enabled))
        }
        viewModelScope.launch {
            repository.saveTextReplacementEnabled(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) R.string.message_text_replacement_enabled else R.string.message_text_replacement_disabled
                )
            )
        }
    }

    fun onTextReplacementValueChanged(value: String) {
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(textReplacementValue = value))
        }
        viewModelScope.launch {
            repository.saveTextReplacementValue(value)
        }
    }

    fun onTextDbResetOnStartChanged(enabled: Boolean) {
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(textDbResetOnStart = enabled))
        }
        viewModelScope.launch {
            repository.saveTextDbResetOnStart(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) {
                        R.string.message_text_db_reset_on_start_enabled
                    } else {
                        R.string.message_text_db_reset_on_start_disabled
                    }
                )
            )
        }
    }

    fun onCocosRuntimeTextCaptureChanged(enabled: Boolean) {
        viewModelScope.launch {
            repository.saveCocosTextCaptureEnabled(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) {
                        R.string.message_cocos_runtime_capture_enabled
                    } else {
                        R.string.message_cocos_runtime_capture_disabled
                    }
                )
            )
        }
    }

    fun onCocosTextPersistChanged(enabled: Boolean) {
        viewModelScope.launch {
            repository.saveCocosTextPersistEnabled(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) {
                        R.string.message_cocos_text_persist_enabled
                    } else {
                        R.string.message_cocos_text_persist_disabled
                    }
                )
            )
        }
    }

    fun onCocosTextPersistChineseOnlyChanged(enabled: Boolean) {
        viewModelScope.launch {
            repository.saveCocosTextPersistChineseOnly(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) {
                        R.string.message_cocos_text_persist_chinese_only_enabled
                    } else {
                        R.string.message_cocos_text_persist_chinese_only_disabled
                    }
                )
            )
        }
    }

    fun onCocosTextReplacementDelayChanged(enabled: Boolean) {
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(cocosTextReplacementDelayEnabled = enabled))
        }
        viewModelScope.launch {
            repository.saveCocosTextReplacementDelayEnabled(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) {
                        R.string.message_cocos_text_replacement_delay_enabled
                    } else {
                        R.string.message_cocos_text_replacement_delay_disabled
                    }
                )
            )
        }
    }

    fun onCocosTextReplacementDelayMsChanged(value: String) {
        val delayMs = RuntimeTextReplacementDefaults.clampCocosDelayMs(value.toIntOrNull() ?: 0)
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(cocosTextReplacementDelayMs = delayMs))
        }
        viewModelScope.launch {
            repository.saveCocosTextReplacementDelayMs(delayMs)
        }
    }

    fun onCocosTypewriterOptimizationChanged(enabled: Boolean) {
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(cocosTypewriterOptimizationEnabled = enabled))
        }
        viewModelScope.launch {
            repository.saveCocosTypewriterOptimizationEnabled(enabled)
            _events.send(
                getApplication<Application>().getString(
                    if (enabled) {
                        R.string.message_cocos_typewriter_enabled
                    } else {
                        R.string.message_cocos_typewriter_disabled
                    }
                )
            )
        }
    }

    fun onCocosTypewriterIdleFinalizeMsChanged(value: String) {
        val clamped = RuntimeTextReplacementDefaults.clampTypewriterIdleFinalizeMs(
            value.toIntOrNull() ?: RuntimeTextReplacementDefaults.DEFAULT_TYPEWRITER_IDLE_FINALIZE_MS
        )
        _uiState.update {
            it.copy(runtimeConfig = it.runtimeConfig.copy(cocosTypewriterIdleFinalizeMs = clamped))
        }
        viewModelScope.launch {
            repository.saveCocosTypewriterIdleFinalizeMs(clamped)
        }
    }

    fun onCocosFontSelected(fontId: String) {
        val validId = CocosBundledFont.fromId(fontId)?.id ?: ""
        _uiState.update {
            it.copy(
                runtimeConfig = it.runtimeConfig.copy(
                    cocosFontId = validId
                )
            )
        }
        viewModelScope.launch {
            repository.saveCocosFontId(validId)
            _events.send(
                getApplication<Application>().getString(
                    if (validId.isBlank()) {
                        R.string.message_cocos_font_disabled
                    } else {
                        R.string.message_cocos_font_selected
                    }
                )
            )
        }
    }

    private fun refresh() {
        viewModelScope.launch {
            val payload = repository.loadConfig()
            _uiState.value = ModeUiState(
                isLoading = false,
                summary = payload.toSummaryUiModel(),
                dumpModeEnabled = payload.dumpModeEnabled,
                hookFramework = payload.hookFramework,
                runtimeConfig = payload.runtimeConfig
            )
        }
    }
}
