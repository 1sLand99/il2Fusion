package com.tools.il2fusion.feature.common

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.tools.il2fusion.R
import com.tools.il2fusion.config.GameEngine
import com.tools.il2fusion.core.design.SectionCard
import com.tools.il2fusion.core.design.StatusBadge

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun HookSummaryCard(
    summary: HookSummaryUiModel,
    modifier: Modifier = Modifier
) {
    SectionCard(
        title = stringResource(R.string.overview_runtime_title),
        subtitle = runtimeSubtitle(summary),
        modifier = modifier
    ) {
        Text(
            text = runtimeBody(summary),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            StatusBadge(text = stringResource(R.string.summary_engine, stringResource(summary.engine.displayNameRes)))
            if (summary.engine == GameEngine.UnityIl2Cpp) {
                StatusBadge(text = stringResource(R.string.summary_saved_methods, summary.savedCount))
            }
            if (summary.engine == GameEngine.Cocos2dxLua) {
                StatusBadge(
                    text = if (summary.runtimeTextCaptureEnabled) {
                        stringResource(R.string.summary_cocos_runtime_capture)
                    } else {
                        stringResource(R.string.summary_cocos_runtime_capture_off)
                    }
                )
            }
            if (summary.engine == GameEngine.UnityIl2Cpp) {
                StatusBadge(text = if (summary.dumpModeEnabled) {
                    stringResource(R.string.summary_mode_dump_only)
                } else {
                    stringResource(R.string.summary_mode_intercept)
                })
            }
            StatusBadge(text = stringResource(summary.hookFramework.displayNameRes))
            when (summary.engine) {
                GameEngine.UnityIl2Cpp -> {
                    StatusBadge(
                        text = if (summary.hasTargetsJson) {
                            stringResource(R.string.summary_json_cached)
                        } else {
                            stringResource(R.string.summary_json_missing)
                        }
                    )
                }

                GameEngine.Cocos2dxLua -> {
                    StatusBadge(text = stringResource(R.string.summary_lua_rules, summary.luaReplacementRuleCount))
                }
            }
        }
        Text(
            text = stringResource(R.string.overview_runtime_footer),
            style = MaterialTheme.typography.bodySmall.copy(fontWeight = FontWeight.Medium),
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun runtimeSubtitle(summary: HookSummaryUiModel): String {
    return when (summary.engine) {
        GameEngine.Cocos2dxLua -> stringResource(R.string.overview_runtime_cocos_lua_subtitle)
        GameEngine.UnityIl2Cpp -> if (summary.dumpModeEnabled) {
            stringResource(R.string.overview_runtime_dump_subtitle)
        } else {
            stringResource(R.string.overview_runtime_hook_subtitle)
        }
    }
}

@Composable
private fun runtimeBody(summary: HookSummaryUiModel): String {
    return when (summary.engine) {
        GameEngine.Cocos2dxLua -> stringResource(R.string.overview_runtime_cocos_lua_body)
        GameEngine.UnityIl2Cpp -> if (summary.dumpModeEnabled) {
            stringResource(R.string.overview_runtime_dump_body)
        } else {
            stringResource(R.string.overview_runtime_hook_body)
        }
    }
}
