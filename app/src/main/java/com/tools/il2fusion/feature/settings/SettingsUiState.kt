package com.tools.il2fusion.feature.settings

import com.tools.il2fusion.core.i18n.AppLanguage
import com.tools.il2fusion.core.i18n.LanguageMode
import com.tools.il2fusion.core.update.model.UpdateInfo
import com.tools.il2fusion.config.TargetSession
import com.tools.il2fusion.config.TextDbExportStatus

data class SettingsUiState(
    val languageMode: LanguageMode = LanguageMode.Auto,
    val manualLanguage: AppLanguage = AppLanguage.English,
    val effectiveLanguage: AppLanguage = AppLanguage.English,
    val versionName: String = "",
    val versionCode: Int = 0,
    val isCheckingUpdate: Boolean = false,
    val isDownloadingUpdate: Boolean = false,
    val downloadProgress: Int = 0,
    val updateStatusText: String? = null,
    val availableUpdate: UpdateInfo? = null,
    val textDbExportTargetPackage: String = "",
    val autoShareTextDbExport: Boolean = true,
    val pendingAutoShareTextDbExportRequestId: String = "",
    val textDbExportStatus: TextDbExportStatus = TextDbExportStatus(),
    val targetSessions: List<TargetSession> = emptyList(),
    val targetSessionNowMillis: Long = 0L
)
