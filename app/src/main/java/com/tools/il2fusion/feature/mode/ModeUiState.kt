package com.tools.il2fusion.feature.mode

import com.tools.il2fusion.config.HookFramework
import com.tools.il2fusion.config.RuntimeHookConfig
import com.tools.il2fusion.feature.common.HookSummaryUiModel

data class ModeUiState(
    val isLoading: Boolean = true,
    val summary: HookSummaryUiModel = HookSummaryUiModel(),
    val dumpModeEnabled: Boolean = false,
    val hookFramework: HookFramework = HookFramework.And64InlineHook,
    val runtimeConfig: RuntimeHookConfig = RuntimeHookConfig()
)
