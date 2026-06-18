package com.tools.il2fusion.feature.parse

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.tools.il2fusion.R
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.core.design.FeatureScreenSurface
import com.tools.il2fusion.core.design.SectionCard
import com.tools.il2fusion.core.design.StatusBadge
import com.tools.il2fusion.feature.common.UsageNoteCard

@Composable
fun ParseRoute(
    modifier: Modifier = Modifier,
    viewModel: ParseViewModel = viewModel()
) {
    val state = viewModel.uiState.collectAsStateWithLifecycle().value
    val context = LocalContext.current
    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
        onResult = viewModel::onFilePicked
    )

    LaunchedEffect(viewModel) {
        viewModel.events.collect { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    FeatureScreenSurface(modifier = modifier) {
        when (state.runtimeConfig.engine) {
            GameEngine.UnityIl2Cpp -> {
                UnityParseContent(
                    state = state,
                    onPickFile = { filePickerLauncher.launch(arrayOf("*/*")) },
                    onSave = viewModel::onSave
                )
            }

            GameEngine.Cocos2dxLua -> {
                CocosConfigContent(
                    state = state,
                    onPickConfig = { filePickerLauncher.launch(arrayOf("*/*")) },
                    onScriptNameChanged = viewModel::onLuaRuleScriptNameChanged,
                    onReplacedCodeChanged = viewModel::onLuaRuleReplacedCodeChanged,
                    onReplaceCodeChanged = viewModel::onLuaRuleReplaceCodeChanged,
                    onPrependCodeChanged = viewModel::onLuaRulePrependCodeChanged,
                    onSaveRule = viewModel::onSaveLuaRule,
                    onCancelRuleEdit = viewModel::onCancelLuaRuleEdit,
                    onEditRule = viewModel::onEditLuaRule,
                    onToggleRule = viewModel::onToggleLuaRule,
                    onDeleteRule = viewModel::onDeleteLuaRule
                )
            }
        }
        UsageNoteCard()
    }
}

@Composable
private fun UnityParseContent(
    state: ParseUiState,
    onPickFile: () -> Unit,
    onSave: () -> Unit
) {
    SectionCard(
        title = stringResource(R.string.parse_import_title),
        subtitle = stringResource(R.string.parse_import_subtitle)
    ) {
        Button(
            onClick = onPickFile,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(text = stringResource(R.string.parse_pick_file))
        }
        AnimatedVisibility(visible = state.isLoading) {
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }
    SectionCard(
        title = stringResource(R.string.parse_results_title),
        subtitle = stringResource(R.string.parse_results_subtitle, state.savedCount)
    ) {
        StatusBadge(
            text = if (state.hasTargetsJson) {
                stringResource(R.string.summary_json_cached)
            } else {
                stringResource(R.string.summary_json_missing)
            }
        )
        if (state.methods.isEmpty()) {
            Text(
                text = stringResource(R.string.parse_results_empty),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                state.methods.forEachIndexed { index, method ->
                    Text(
                        text = "${index + 1}. $method",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurface,
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
                                shape = RoundedCornerShape(14.dp)
                            )
                            .padding(horizontal = 12.dp, vertical = 10.dp)
                    )
                }
            }
        }
        Button(
            onClick = onSave,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(text = stringResource(R.string.parse_save))
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CocosConfigContent(
    state: ParseUiState,
    onPickConfig: () -> Unit,
    onScriptNameChanged: (String) -> Unit,
    onReplacedCodeChanged: (String) -> Unit,
    onReplaceCodeChanged: (String) -> Unit,
    onPrependCodeChanged: (String) -> Unit,
    onSaveRule: () -> Unit,
    onCancelRuleEdit: () -> Unit,
    onEditRule: (Int) -> Unit,
    onToggleRule: (Int, Boolean) -> Unit,
    onDeleteRule: (Int) -> Unit
) {
    val config = state.runtimeConfig
    SectionCard(
        title = stringResource(R.string.parse_cocos_config_title),
        subtitle = stringResource(R.string.parse_cocos_lua_config_subtitle)
    ) {
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            StatusBadge(text = stringResource(R.string.summary_engine, stringResource(config.engine.displayNameRes)))
            StatusBadge(
                text = if (config.cocosTextCaptureEnabled) {
                    stringResource(R.string.summary_cocos_runtime_capture)
                } else {
                    stringResource(R.string.summary_cocos_runtime_capture_off)
                }
            )
            StatusBadge(text = stringResource(R.string.summary_lua_rules, config.luaReplacementRuleCount))
        }
        AnimatedVisibility(visible = state.isLoading) {
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }
    }
    CocosLuaRulesSection(
        state = state,
        onPickConfig = onPickConfig,
        onScriptNameChanged = onScriptNameChanged,
        onReplacedCodeChanged = onReplacedCodeChanged,
        onReplaceCodeChanged = onReplaceCodeChanged,
        onPrependCodeChanged = onPrependCodeChanged,
        onSaveRule = onSaveRule,
        onCancelRuleEdit = onCancelRuleEdit,
        onEditRule = onEditRule,
        onToggleRule = onToggleRule,
        onDeleteRule = onDeleteRule
    )
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CocosLuaRulesSection(
    state: ParseUiState,
    onPickConfig: () -> Unit,
    onScriptNameChanged: (String) -> Unit,
    onReplacedCodeChanged: (String) -> Unit,
    onReplaceCodeChanged: (String) -> Unit,
    onPrependCodeChanged: (String) -> Unit,
    onSaveRule: () -> Unit,
    onCancelRuleEdit: () -> Unit,
    onEditRule: (Int) -> Unit,
    onToggleRule: (Int, Boolean) -> Unit,
    onDeleteRule: (Int) -> Unit
) {
    val rules = state.runtimeConfig.cocosLuaReplacementRules
    SectionCard(
        title = stringResource(R.string.parse_cocos_lua_rules_title),
        subtitle = stringResource(R.string.parse_cocos_lua_rules_subtitle)
    ) {
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            StatusBadge(text = stringResource(R.string.summary_lua_rules, rules.size))
            StatusBadge(text = stringResource(R.string.summary_lua_rules_enabled, rules.count { it.enabled }))
        }
        if (rules.isEmpty()) {
            Text(
                text = stringResource(R.string.parse_cocos_lua_rules_empty),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        } else {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                rules.forEachIndexed { index, rule ->
                    Column(
                        verticalArrangement = Arrangement.spacedBy(6.dp),
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.55f),
                                shape = RoundedCornerShape(12.dp)
                            )
                            .padding(horizontal = 12.dp, vertical = 10.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "${index + 1}. ${rule.scriptName}",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurface,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f)
                            )
                            Spacer(modifier = Modifier.width(12.dp))
                            Switch(
                                checked = rule.enabled,
                                onCheckedChange = { checked -> onToggleRule(index, checked) }
                            )
                        }
                        if (rule.replacedCode.isNotBlank()) {
                            Text(
                                text = stringResource(R.string.parse_cocos_lua_rule_match, rule.replacedCode),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                        FlowRow(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            TextButton(onClick = { onEditRule(index) }) {
                                Text(text = stringResource(R.string.common_edit))
                            }
                            TextButton(onClick = { onDeleteRule(index) }) {
                                Text(text = stringResource(R.string.common_delete))
                            }
                        }
                    }
                }
            }
        }
        OutlinedTextField(
            value = state.luaRuleScriptName,
            onValueChange = onScriptNameChanged,
            label = { Text(text = stringResource(R.string.parse_cocos_lua_rule_script_name)) },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true
        )
        OutlinedTextField(
            value = state.luaRuleReplacedCode,
            onValueChange = onReplacedCodeChanged,
            label = { Text(text = stringResource(R.string.parse_cocos_lua_rule_replaced_code)) },
            modifier = Modifier.fillMaxWidth(),
            minLines = 2,
            maxLines = 6
        )
        OutlinedTextField(
            value = state.luaRuleReplaceCode,
            onValueChange = onReplaceCodeChanged,
            label = { Text(text = stringResource(R.string.parse_cocos_lua_rule_replace_code)) },
            modifier = Modifier.fillMaxWidth(),
            minLines = 2,
            maxLines = 8
        )
        OutlinedTextField(
            value = state.luaRulePrependCode,
            onValueChange = onPrependCodeChanged,
            label = { Text(text = stringResource(R.string.parse_cocos_lua_rule_prepend_code)) },
            modifier = Modifier.fillMaxWidth(),
            minLines = 2,
            maxLines = 8
        )
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Button(onClick = onSaveRule) {
                Text(
                    text = stringResource(
                        if (state.editingLuaRuleIndex >= 0) {
                            R.string.parse_cocos_lua_rule_update
                        } else {
                            R.string.parse_cocos_lua_rule_add
                        }
                    )
                )
            }
            TextButton(onClick = onCancelRuleEdit) {
                Text(text = stringResource(R.string.common_cancel))
            }
            TextButton(onClick = onPickConfig) {
                Text(text = stringResource(R.string.parse_cocos_pick_config))
            }
        }
    }
}
