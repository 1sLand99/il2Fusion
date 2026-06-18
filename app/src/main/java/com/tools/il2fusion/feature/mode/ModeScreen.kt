package com.tools.il2fusion.feature.mode

import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.tools.il2fusion.R
import com.tools.il2fusion.config.CocosBundledFont
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.config.HookFramework
import com.tools.il2fusion.core.design.FeatureScreenSurface
import com.tools.il2fusion.core.design.SectionCard
import com.tools.il2fusion.feature.common.HookSummaryCard

@Composable
fun ModeRoute(
    modifier: Modifier = Modifier,
    viewModel: ModeViewModel = viewModel()
) {
    val state = viewModel.uiState.collectAsStateWithLifecycle().value
    val context = LocalContext.current

    LaunchedEffect(viewModel) {
        viewModel.events.collect { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    FeatureScreenSurface(modifier = modifier) {
        if (state.isLoading) {
            CircularProgressIndicator()
        }
        HookSummaryCard(summary = state.summary)
        EngineSelectorCard(
            selected = state.runtimeConfig.engine,
            onSelected = viewModel::onEngineChanged
        )
        TextReplacementCard(
            enabled = state.runtimeConfig.textReplacementEnabled,
            value = state.runtimeConfig.textReplacementValue,
            onEnabledChanged = viewModel::onTextReplacementChanged,
            onValueChanged = viewModel::onTextReplacementValueChanged
        )
        ModeToggleCard(
            title = stringResource(R.string.mode_text_db_reset_title),
            description = if (state.runtimeConfig.textDbResetOnStart) {
                stringResource(R.string.mode_text_db_reset_enabled_desc)
            } else {
                stringResource(R.string.mode_text_db_reset_disabled_desc)
            },
            checked = state.runtimeConfig.textDbResetOnStart,
            onCheckedChange = viewModel::onTextDbResetOnStartChanged
        )
        when (state.runtimeConfig.engine) {
            GameEngine.UnityIl2Cpp -> {
                ModeToggleCard(
                    title = stringResource(R.string.mode_dump_title),
                    description = if (state.dumpModeEnabled) {
                        stringResource(R.string.mode_dump_enabled_desc)
                    } else {
                        stringResource(R.string.mode_dump_disabled_desc)
                    },
                    checked = state.dumpModeEnabled,
                    onCheckedChange = viewModel::onDumpModeChanged
                )
            }

            GameEngine.Cocos2dxLua -> {
                CocosWorkflowCard(
                    runtimeTextCaptureEnabled = state.runtimeConfig.cocosTextCaptureEnabled,
                    textPersistEnabled = state.runtimeConfig.cocosTextPersistEnabled,
                    textPersistChineseOnly = state.runtimeConfig.cocosTextPersistChineseOnly,
                    textReplacementDelayEnabled = state.runtimeConfig.cocosTextReplacementDelayEnabled,
                    textReplacementDelayMs = state.runtimeConfig.cocosTextReplacementDelayMs,
                    typewriterOptimizationEnabled = state.runtimeConfig.cocosTypewriterOptimizationEnabled,
                    typewriterIdleFinalizeMs = state.runtimeConfig.cocosTypewriterIdleFinalizeMs,
                    fontId = state.runtimeConfig.cocosFontId,
                    onRuntimeTextCaptureChanged = viewModel::onCocosRuntimeTextCaptureChanged,
                    onTextPersistChanged = viewModel::onCocosTextPersistChanged,
                    onTextPersistChineseOnlyChanged = viewModel::onCocosTextPersistChineseOnlyChanged,
                    onTextReplacementDelayChanged = viewModel::onCocosTextReplacementDelayChanged,
                    onTextReplacementDelayMsChanged = viewModel::onCocosTextReplacementDelayMsChanged,
                    onTypewriterOptimizationChanged = viewModel::onCocosTypewriterOptimizationChanged,
                    onTypewriterIdleFinalizeMsChanged = viewModel::onCocosTypewriterIdleFinalizeMsChanged,
                    onFontSelected = viewModel::onCocosFontSelected
                )
            }
        }
        ModeToggleCard(
            title = stringResource(R.string.mode_framework_title),
            description = if (state.hookFramework == HookFramework.Dobby) {
                stringResource(R.string.mode_framework_dobby_desc)
            } else {
                stringResource(R.string.mode_framework_and64_desc)
            },
            checked = state.hookFramework == HookFramework.Dobby,
            onCheckedChange = viewModel::onHookFrameworkChanged
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun EngineSelectorCard(
    selected: GameEngine,
    onSelected: (GameEngine) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.mode_engine_title),
        subtitle = stringResource(R.string.mode_engine_subtitle)
    ) {
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            GameEngine.entries.forEach { engine ->
                FilterChip(
                    selected = selected == engine,
                    onClick = { onSelected(engine) },
                    label = {
                        Text(
                            text = stringResource(engine.displayNameRes),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    }
                )
            }
        }
    }
}

@Composable
private fun TextReplacementCard(
    enabled: Boolean,
    value: String,
    onEnabledChanged: (Boolean) -> Unit,
    onValueChanged: (String) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.mode_text_replacement_title),
        subtitle = if (enabled) {
            stringResource(R.string.mode_text_replacement_enabled_desc)
        } else {
            stringResource(R.string.mode_text_replacement_disabled_desc)
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = if (enabled) stringResource(R.string.common_enabled) else stringResource(R.string.common_disabled),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Switch(checked = enabled, onCheckedChange = onEnabledChanged)
        }
        OutlinedTextField(
            value = value,
            onValueChange = onValueChanged,
            label = { Text(text = stringResource(R.string.mode_text_replacement_value)) },
            singleLine = true,
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun CocosWorkflowCard(
    runtimeTextCaptureEnabled: Boolean,
    textPersistEnabled: Boolean,
    textPersistChineseOnly: Boolean,
    textReplacementDelayEnabled: Boolean,
    textReplacementDelayMs: Int,
    typewriterOptimizationEnabled: Boolean,
    typewriterIdleFinalizeMs: Int,
    fontId: String,
    onRuntimeTextCaptureChanged: (Boolean) -> Unit,
    onTextPersistChanged: (Boolean) -> Unit,
    onTextPersistChineseOnlyChanged: (Boolean) -> Unit,
    onTextReplacementDelayChanged: (Boolean) -> Unit,
    onTextReplacementDelayMsChanged: (String) -> Unit,
    onTypewriterOptimizationChanged: (Boolean) -> Unit,
    onTypewriterIdleFinalizeMsChanged: (String) -> Unit,
    onFontSelected: (String) -> Unit
) {
    SectionCard(
        title = stringResource(R.string.mode_cocos_lua_workflow_title),
        subtitle = stringResource(R.string.mode_cocos_lua_workflow_desc)
    ) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            ModeSwitchRow(
                title = stringResource(R.string.mode_cocos_runtime_capture_title),
                description = if (runtimeTextCaptureEnabled) {
                    stringResource(R.string.mode_cocos_runtime_capture_enabled_desc)
                } else {
                    stringResource(R.string.mode_cocos_runtime_capture_disabled_desc)
                },
                checked = runtimeTextCaptureEnabled,
                onCheckedChange = onRuntimeTextCaptureChanged
            )
            ModeSwitchRow(
                title = stringResource(R.string.mode_cocos_text_persist_title),
                description = if (textPersistEnabled) {
                    stringResource(R.string.mode_cocos_text_persist_enabled_desc)
                } else {
                    stringResource(R.string.mode_cocos_text_persist_disabled_desc)
                },
                checked = textPersistEnabled,
                onCheckedChange = onTextPersistChanged
            )
            ModeSwitchRow(
                title = stringResource(R.string.mode_cocos_text_persist_chinese_only_title),
                description = if (textPersistChineseOnly) {
                    stringResource(R.string.mode_cocos_text_persist_chinese_only_enabled_desc)
                } else {
                    stringResource(R.string.mode_cocos_text_persist_chinese_only_disabled_desc)
                },
                checked = textPersistChineseOnly,
                onCheckedChange = onTextPersistChineseOnlyChanged
            )
            ModeSwitchRow(
                title = stringResource(R.string.mode_cocos_text_replacement_delay_title),
                description = if (textReplacementDelayEnabled) {
                    stringResource(R.string.mode_cocos_text_replacement_delay_enabled_desc)
                } else {
                    stringResource(R.string.mode_cocos_text_replacement_delay_disabled_desc)
                },
                checked = textReplacementDelayEnabled,
                onCheckedChange = onTextReplacementDelayChanged
            )
            OutlinedTextField(
                value = textReplacementDelayMs.toString(),
                onValueChange = onTextReplacementDelayMsChanged,
                label = { Text(text = stringResource(R.string.mode_cocos_text_replacement_delay_value)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth()
            )
            ModeSwitchRow(
                title = stringResource(R.string.mode_cocos_typewriter_title),
                description = if (typewriterOptimizationEnabled) {
                    stringResource(R.string.mode_cocos_typewriter_enabled_desc)
                } else {
                    stringResource(R.string.mode_cocos_typewriter_disabled_desc)
                },
                checked = typewriterOptimizationEnabled,
                onCheckedChange = onTypewriterOptimizationChanged
            )
            OutlinedTextField(
                value = typewriterIdleFinalizeMs.toString(),
                onValueChange = onTypewriterIdleFinalizeMsChanged,
                label = { Text(text = stringResource(R.string.mode_cocos_typewriter_idle_finalize_ms)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                modifier = Modifier.fillMaxWidth()
            )
            CocosFontSelector(
                selectedFontId = fontId,
                onSelected = onFontSelected
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CocosFontSelector(
    selectedFontId: String,
    onSelected: (String) -> Unit
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            text = stringResource(R.string.mode_cocos_font_title),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface
        )
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            FilterChip(
                selected = selectedFontId.isBlank(),
                onClick = { onSelected("") },
                label = { Text(text = stringResource(R.string.mode_cocos_font_none)) }
            )
            CocosBundledFont.entries.forEach { font ->
                FilterChip(
                    selected = selectedFontId == font.id,
                    onClick = { onSelected(font.id) },
                    label = { Text(text = stringResource(font.displayNameRes)) }
                )
            }
        }
    }
}

@Composable
private fun ModeSwitchRow(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            Text(
                text = description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
        Spacer(modifier = Modifier.width(12.dp))
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

@Composable
private fun ModeToggleCard(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    SectionCard(title = title, subtitle = description) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = if (checked) stringResource(R.string.common_enabled) else stringResource(R.string.common_disabled),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f)
            )
            Spacer(modifier = Modifier.width(12.dp))
            Switch(checked = checked, onCheckedChange = onCheckedChange)
        }
    }
}
